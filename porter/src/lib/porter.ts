import { invoke } from '@tauri-apps/api/core'
import { listen, type UnlistenFn } from '@tauri-apps/api/event'

/**
 * The Porter's side of the Rust commands (src-tauri/src/lib.rs).
 *
 * Outside the app (`pnpm dev` in a browser) every call is answered by a
 * stand-in, so each screen can be looked at and tested without an APK.
 */

export function isTauri(): boolean {
  return typeof window !== 'undefined' && '__TAURI_INTERNALS__' in window
}

/** What a dropped APK turned out to be. */
export type ApkSummary = {
  path: string
  fileName: string
  version: string
  files: number
  bytes: number
  /** A folder next to the APK that does not exist yet. */
  suggestedFolder: string
}

export type Stage = 'checking' | 'tables' | 'assets' | 'installing'

export type PortProgress = {
  stage: Stage
  filesDone: number
  filesTotal: number
  bytesDone: number
  bytesTotal: number
}

export type PortResult = { exe: string; folder: string; shortcut: string | null }

export type PortOptions = { apk: string; folder: string; withModkit: boolean; shortcut: boolean }

/** 0..1 for the whole run: the copying is nearly all of the time. */
export function overall(p: PortProgress | null): number {
  if (!p) return 0
  switch (p.stage) {
    case 'checking':
      return 0.01
    case 'tables':
      return 0.03
    case 'assets':
      return 0.05 + 0.92 * (p.bytesTotal > 0 ? p.bytesDone / p.bytesTotal : 0)
    case 'installing':
      return 0.98
  }
}

export async function bundledGame(): Promise<boolean> {
  if (!isTauri()) return true
  return invoke<boolean>('bundled_game')
}

export async function inspectApk(path: string): Promise<ApkSummary> {
  if (!isTauri()) return mockInspect(path)
  return invoke<ApkSummary>('inspect_apk', { path })
}

/** A free folder name at `folder` or next to it ("... (2)"). */
export async function freeFolder(folder: string): Promise<string> {
  if (!isTauri()) return folder
  return invoke<string>('free_folder', { folder })
}

export async function startPort(options: PortOptions): Promise<void> {
  if (!isTauri()) return mockPort(options)
  return invoke('start_port', { ...options })
}

export async function cancelPort(): Promise<void> {
  if (!isTauri()) {
    mockCancelled = true
    return
  }
  return invoke('cancel_port')
}

export async function play(exe: string): Promise<void> {
  if (!isTauri()) return
  return invoke('play', { exe })
}

export async function showFolder(path: string): Promise<void> {
  if (!isTauri()) return
  const { revealItemInDir } = await import('@tauri-apps/plugin-opener')
  return revealItemInDir(path)
}

export async function openLink(url: string): Promise<void> {
  if (!isTauri()) {
    window.open(url, '_blank')
    return
  }
  const { openUrl } = await import('@tauri-apps/plugin-opener')
  return openUrl(url)
}

export async function pickApk(): Promise<string | null> {
  if (!isTauri()) return 'C:\\Users\\you\\Downloads\\GTA Chinatown Wars 4.4.243.apk'
  const { open } = await import('@tauri-apps/plugin-dialog')
  const picked = await open({
    title: 'Choose your GTA: Chinatown Wars APK',
    multiple: false,
    directory: false,
    filters: [{ name: 'Android app', extensions: ['apk'] }],
  })
  return typeof picked === 'string' ? picked : null
}

export async function pickFolder(current: string): Promise<string | null> {
  if (!isTauri()) return current
  const { open } = await import('@tauri-apps/plugin-dialog')
  const picked = await open({ title: 'Where should the game go?', directory: true, multiple: false })
  return typeof picked === 'string' ? picked : null
}

export type PortEvents = {
  progress: (p: PortProgress) => void
  finished: (r: PortResult) => void
  failed: (message: string) => void
}

export async function onPortEvents(handlers: PortEvents): Promise<UnlistenFn> {
  if (!isTauri()) {
    mockHandlers = handlers
    return () => {
      if (mockHandlers === handlers) mockHandlers = null
    }
  }
  const off = await Promise.all([
    listen<PortProgress>('porter://progress', (e) => handlers.progress(e.payload)),
    listen<PortResult>('porter://finished', (e) => handlers.finished(e.payload)),
    listen<string>('porter://failed', (e) => handlers.failed(e.payload)),
  ])
  return () => off.forEach((f) => f())
}

/** Files dragged over the window: `over` while they hover, then the dropped paths. */
export async function onFileDrop(over: (hovering: boolean) => void, dropped: (paths: string[]) => void): Promise<UnlistenFn> {
  if (!isTauri()) return () => {}
  const { getCurrentWebview } = await import('@tauri-apps/api/webview')
  return getCurrentWebview().onDragDropEvent((event) => {
    const p = event.payload
    if (p.type === 'enter' || p.type === 'over') over(true)
    else if (p.type === 'leave') over(false)
    else if (p.type === 'drop') {
      over(false)
      dropped(p.paths)
    }
  })
}

export async function minimize(): Promise<void> {
  if (!isTauri()) return
  const { getCurrentWindow } = await import('@tauri-apps/api/window')
  return getCurrentWindow().minimize()
}

export async function quit(): Promise<void> {
  if (!isTauri()) return
  const { getCurrentWindow } = await import('@tauri-apps/api/window')
  return getCurrentWindow().close()
}

/* ── The browser stand-in ─────────────────────────────────── */

let mockHandlers: PortEvents | null = null
let mockCancelled = false
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms))

async function mockInspect(path: string): Promise<ApkSummary> {
  await sleep(900)
  const fileName = path.split(/[\\/]/).pop() ?? path
  if (!fileName.toLowerCase().endsWith('.apk')) throw 'This file could not be opened as an APK (invalid Zip archive).'
  const dir = path.slice(0, path.length - fileName.length)
  return { path, fileName, version: '4.4.243', files: 1534, bytes: 1_392_000_000, suggestedFolder: `${dir}GTA Chinatown Wars PC` }
}

async function mockPort(options: PortOptions): Promise<void> {
  mockCancelled = false
  const bytesTotal = 1_328_000_000
  const filesTotal = 1534
  void (async () => {
    const emit = (p: PortProgress) => mockHandlers?.progress(p)
    emit({ stage: 'checking', filesDone: 0, filesTotal: 0, bytesDone: 0, bytesTotal: 0 })
    await sleep(500)
    emit({ stage: 'tables', filesDone: 0, filesTotal: 0, bytesDone: 0, bytesTotal: 0 })
    await sleep(900)
    for (let i = 0; i <= 100; i++) {
      if (mockCancelled) return mockHandlers?.failed('Cancelled.')
      emit({ stage: 'assets', filesDone: Math.round((filesTotal * i) / 100), filesTotal, bytesDone: (bytesTotal * i) / 100, bytesTotal })
      await sleep(80)
    }
    emit({ stage: 'installing', filesDone: filesTotal, filesTotal, bytesDone: bytesTotal, bytesTotal })
    await sleep(600)
    mockHandlers?.finished({
      exe: `${options.folder}\\GTACTW.exe`,
      folder: options.folder,
      shortcut: options.shortcut ? 'C:\\Users\\you\\Desktop\\GTA Chinatown Wars.url' : null,
    })
  })()
}
