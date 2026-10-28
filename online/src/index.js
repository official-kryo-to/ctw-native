// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
//
// CTW Online: the list of public games and a relay between their players, as one Cloudflare Worker.
//
//   GET    /v1/servers                       the public games (heard from in the last minute)
//   POST   /v1/servers                       {name, players, max, version, public} -> {id, token}
//   POST   /v1/servers/<id>                  heartbeat {token, players, name}: keeps the game listed
//   DELETE /v1/servers/<id>?token=           the host stops
//   GET    /v1/rooms/<id>?token=   (WebSocket) the host's end of the relay
//   GET    /v1/rooms/<id>          (WebSocket) a player's end
//
// Relay frames are binary. A player's datagram reaches the host as [u16 player][payload]; the host sends
// [u16 player][payload] to one player or [0xFFFF][payload] to all of them, who receive the payload alone. An empty
// payload from the relay to the host says that player has gone. The game's own messages are carried unchanged.
import { DurableObject } from "cloudflare:workers";

const MAX_PLAYERS = 16;     // relay connections per game (the mod allows 8)
const MAX_FRAME = 2048;
const STALE_MS = 60_000;    // a game unheard of this long leaves the list

function json(body, status = 200) {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "content-type": "application/json", "access-control-allow-origin": "*" },
  });
}

function clean(text, max) {
  return String(text ?? "").replace(/[\u0000-\u001f\u007f]/g, " ").trim().slice(0, max);
}

function randomId(length) {
  const alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   // easy to read out to a friend
  const bytes = crypto.getRandomValues(new Uint8Array(length));
  return Array.from(bytes, (b) => alphabet[b % alphabet.length]).join("");
}

export class Lobby extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    ctx.blockConcurrencyWhile(async () => {
      ctx.storage.sql.exec(`CREATE TABLE IF NOT EXISTS servers (
        id TEXT PRIMARY KEY, token TEXT NOT NULL, name TEXT NOT NULL, players INTEGER NOT NULL,
        max INTEGER NOT NULL, version TEXT NOT NULL, public INTEGER NOT NULL, updated INTEGER NOT NULL)`);
    });
  }

  list() {
    this.ctx.storage.sql.exec("DELETE FROM servers WHERE updated < ?", Date.now() - STALE_MS);
    return this.ctx.storage.sql
      .exec("SELECT id, name, players, max, version FROM servers WHERE public = 1 ORDER BY players DESC, updated DESC LIMIT 200")
      .toArray();
  }

  register(info) {
    const id = randomId(6), token = randomId(24);
    this.ctx.storage.sql.exec(
      "INSERT INTO servers (id, token, name, players, max, version, public, updated) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
      id, token, info.name, info.players, info.max, info.version, info.public ? 1 : 0, Date.now());
    return { id, token };
  }

  heartbeat(id, token, info) {
    const row = this.ctx.storage.sql.exec("SELECT token FROM servers WHERE id = ?", id).toArray()[0];
    if (!row || row.token !== token) return false;
    this.ctx.storage.sql.exec("UPDATE servers SET players = ?, name = ?, updated = ? WHERE id = ?",
      info.players, info.name, Date.now(), id);
    return true;
  }

  remove(id, token) {
    this.ctx.storage.sql.exec("DELETE FROM servers WHERE id = ? AND token = ?", id, token);
  }

  check(id, token) {   // the relay asks whether the game exists (and, for its host, the token)
    const row = this.ctx.storage.sql.exec("SELECT token, updated FROM servers WHERE id = ?", id).toArray()[0];
    return !!row && row.updated >= Date.now() - STALE_MS && (token === undefined || row.token === token);
  }
}

export class Room extends DurableObject {
  host() { return this.ctx.getWebSockets().find((ws) => ws.deserializeAttachment()?.host); }

  players() { return this.ctx.getWebSockets().filter((ws) => !ws.deserializeAttachment()?.host); }

  async fetch(request) {
    const isHost = new URL(request.url).searchParams.get("role") === "host";
    if (isHost && this.host()) return new Response("this game already has a host", { status: 409 });
    if (!isHost && !this.host()) return new Response("no such game", { status: 404 });
    if (!isHost && this.players().length >= MAX_PLAYERS) return new Response("full", { status: 503 });
    const [client, server] = Object.values(new WebSocketPair());
    this.ctx.acceptWebSocket(server);
    let player = 0;
    if (!isHost) {
      const used = new Set(this.players().map((ws) => ws.deserializeAttachment()?.player));
      player = 1;
      while (used.has(player)) ++player;
    }
    server.serializeAttachment({ host: isHost, player });
    return new Response(null, { status: 101, webSocket: client });
  }

  webSocketMessage(ws, message) {
    if (typeof message === "string" || message.byteLength > MAX_FRAME) return;
    const me = ws.deserializeAttachment();
    if (me.host) {
      if (message.byteLength < 3) return;
      const to = new DataView(message).getUint16(0, true), payload = message.slice(2);
      for (const p of this.players())
        if (to === 0xffff || p.deserializeAttachment().player === to) p.send(payload);
    } else {
      const host = this.host();
      if (!host || message.byteLength < 1) return;
      const frame = new Uint8Array(message.byteLength + 2);
      new DataView(frame.buffer).setUint16(0, me.player, true);
      frame.set(new Uint8Array(message), 2);
      host.send(frame);
    }
  }

  webSocketClose(ws, code, reason) {
    const me = ws.deserializeAttachment();
    try { ws.close(code, reason); } catch {}
    if (me?.host) {
      for (const p of this.players()) try { p.close(1001, "the host left"); } catch {}
    } else if (me) {
      const host = this.host();
      if (host) {
        const frame = new Uint8Array(2);
        new DataView(frame.buffer).setUint16(0, me.player, true);
        host.send(frame);
      }
    }
  }

  webSocketError(ws) { this.webSocketClose(ws, 1011, "error"); }
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const parts = url.pathname.split("/").filter(Boolean);
    if (parts[0] !== "v1") return json({ service: "CTW Online", version: 1 });
    const lobby = env.LOBBY.getByName("lobby");
    if (parts[1] === "servers") {
      if (parts.length === 2 && request.method === "GET") return json(await lobby.list());
      const body = request.method === "POST" ? await request.json().catch(() => ({})) : {};
      const info = {
        name: clean(body.name, 40) || "CTW game",
        players: Math.max(0, Math.min(Number(body.players) | 0, MAX_PLAYERS)),
        max: Math.max(2, Math.min(Number(body.max) | 0 || 8, MAX_PLAYERS)),
        version: clean(body.version, 16),
        public: body.public !== false,
      };
      if (parts.length === 2 && request.method === "POST") return json(await lobby.register(info));
      if (parts.length === 3 && request.method === "POST")
        return (await lobby.heartbeat(parts[2], String(body.token ?? ""), info)) ? json({ ok: true }) : json({ error: "unknown game" }, 404);
      if (parts.length === 3 && request.method === "DELETE") {
        await lobby.remove(parts[2], url.searchParams.get("token") ?? "");
        return json({ ok: true });
      }
    }
    if (parts[1] === "rooms" && parts.length === 3) {
      if (request.headers.get("Upgrade") !== "websocket") return json({ error: "expected a WebSocket" }, 426);
      const id = parts[2].toUpperCase(), token = url.searchParams.get("token");
      if (!(await lobby.check(id, token ?? undefined))) return json({ error: "no such game" }, 404);
      const target = new URL(request.url);
      target.search = token ? "?role=host" : "";
      return env.ROOM.getByName(id).fetch(new Request(target, request));
    }
    return json({ error: "not found" }, 404);
  },
};
