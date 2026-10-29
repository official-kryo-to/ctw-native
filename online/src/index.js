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

const MAX_PLAYERS = 32;     // includes the host; bounded to match the mod's slot and packet limits
const DEFAULT_PLAYERS = 16;
const MAX_FRAME = 2048;
const STALE_MS = 60_000;    // a game unheard of this long leaves the list

// Abuse limits, per client address (Cloudflare's CF-Connecting-IP). Counters are kept in memory and reset
// if the Lobby is evicted, which is fine for a courtesy limit. Raise them for a service behind a shared network.
const MAX_GAMES_PER_IP = 5;          // games one address may keep listed at once
const REGISTRATIONS_PER_MINUTE = 20; // new games one address may list per minute
const MISSES_PER_MINUTE = 30;        // unknown-game lookups one address may make per minute (stops code guessing)
const PLAYER_FRAMES_PER_SECOND = 200; // frames a relay player may send; the excess is dropped

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
      try { ctx.storage.sql.exec("ALTER TABLE servers ADD COLUMN ip TEXT NOT NULL DEFAULT ''"); } catch {}   // older tables
    });
    this.windows = new Map();   // "kind:ip" -> {start, count}
  }

  over(key, limit) {   // counts one event; true once the address has used up this minute's allowance
    const now = Date.now();
    if (this.windows.size > 5000)
      for (const [k, w] of this.windows) if (now - w.start >= 60_000) this.windows.delete(k);
    let w = this.windows.get(key);
    if (!w || now - w.start >= 60_000) this.windows.set(key, w = { start: now, count: 0 });
    return ++w.count > limit;
  }

  list() {
    this.ctx.storage.sql.exec("DELETE FROM servers WHERE updated < ?", Date.now() - STALE_MS);
    return this.ctx.storage.sql
      .exec("SELECT id, name, players, max, version FROM servers WHERE public = 1 ORDER BY players DESC, updated DESC LIMIT 200")
      .toArray();
  }

  register(info, ip) {
    const now = Date.now();
    const listed = this.ctx.storage.sql.exec(
      "SELECT COUNT(*) AS n FROM servers WHERE ip = ? AND updated >= ?", ip, now - STALE_MS).one().n;
    if (listed >= MAX_GAMES_PER_IP) return { error: "too many games from this address" };
    if (this.over(`register:${ip}`, REGISTRATIONS_PER_MINUTE)) return { error: "slow down" };
    const id = randomId(6), token = randomId(24);
    this.ctx.storage.sql.exec(
      "INSERT INTO servers (id, token, name, players, max, version, public, updated, ip) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
      id, token, info.name, info.players, info.max, info.version, info.public ? 1 : 0, now, ip);
    return { id, token };
  }

  heartbeat(id, token, info) {
    const row = this.ctx.storage.sql.exec("SELECT token, max FROM servers WHERE id = ?", id).toArray()[0];
    if (!row || row.token !== token) return false;
    this.ctx.storage.sql.exec("UPDATE servers SET players = ?, name = ?, updated = ? WHERE id = ?",
      Math.min(info.players, row.max), info.name, Date.now(), id);
    return true;
  }

  remove(id, token) {
    this.ctx.storage.sql.exec("DELETE FROM servers WHERE id = ? AND token = ?", id, token);
  }

  check(id, token, ip) {   // the relay asks whether the game exists (and, for its host, the token): max, 0 or -1 (too many misses)
    const key = `miss:${ip}`, seen = this.windows.get(key);
    if (seen && Date.now() - seen.start < 60_000 && seen.count >= MISSES_PER_MINUTE) return -1;
    const row = this.ctx.storage.sql.exec("SELECT token, updated, max FROM servers WHERE id = ?", id).toArray()[0];
    if (row && row.updated >= Date.now() - STALE_MS && (token === undefined || row.token === token)) return row.max;
    this.over(key, MISSES_PER_MINUTE);
    return 0;
  }
}

export class Room extends DurableObject {
  frames = new WeakMap();   // socket -> {second, count}: a player's frames in the current second

  flooding(ws) {
    const second = Math.floor(Date.now() / 1000);
    let f = this.frames.get(ws);
    if (!f || f.second !== second) this.frames.set(ws, f = { second, count: 0 });
    return ++f.count > PLAYER_FRAMES_PER_SECOND;
  }

  host() { return this.ctx.getWebSockets().find((ws) => ws.deserializeAttachment()?.host); }

  players() { return this.ctx.getWebSockets().filter((ws) => !ws.deserializeAttachment()?.host); }

  async fetch(request) {
    const query = new URL(request.url).searchParams;
    const isHost = query.get("role") === "host";
    if (isHost && this.host()) return new Response("this game already has a host", { status: 409 });
    if (!isHost && !this.host()) return new Response("no such game", { status: 404 });
    const max = isHost ? Math.max(2, Math.min(Number(query.get("max")) | 0 || DEFAULT_PLAYERS, MAX_PLAYERS))
      : this.host().deserializeAttachment().max ?? DEFAULT_PLAYERS;
    if (!isHost && this.players().length >= max - 1) return new Response("full", { status: 503 });
    let player = 0;
    if (!isHost) {
      const used = new Set(this.players().map((ws) => ws.deserializeAttachment()?.player));
      player = 1;
      while (used.has(player)) ++player;
    }
    const [client, server] = Object.values(new WebSocketPair());
    this.ctx.acceptWebSocket(server);
    server.serializeAttachment({ host: isHost, player, max });
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
      if (!host || message.byteLength < 1 || this.flooding(ws)) return;
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
    const ip = request.headers.get("CF-Connecting-IP") ?? "";
    if (parts[1] === "servers") {
      if (parts.length === 2 && request.method === "GET") return json(await lobby.list());
      const body = request.method === "POST" ? await request.json().catch(() => ({})) : {};
      const info = {
        name: clean(body.name, 40) || "CTW game",
        players: Math.max(0, Math.min(Number(body.players) | 0, MAX_PLAYERS)),
        max: Math.max(2, Math.min(Number(body.max) | 0 || DEFAULT_PLAYERS, MAX_PLAYERS)),
        version: clean(body.version, 16),
        public: body.public !== false,
      };
      if (parts.length === 2) info.players = Math.min(info.players, info.max);
      if (parts.length === 2 && request.method === "POST") {
        const made = await lobby.register(info, ip);
        return made.error ? json(made, 429) : json(made);
      }
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
      const max = await lobby.check(id, token ?? undefined, ip);
      if (max < 0) return json({ error: "too many unknown games; slow down" }, 429);
      if (!max) return json({ error: "no such game" }, 404);
      const target = new URL(request.url);
      target.search = token ? `?role=host&max=${max}` : "";
      return env.ROOM.getByName(id).fetch(new Request(target, request));
    }
    return json({ error: "not found" }, 404);
  },
};
