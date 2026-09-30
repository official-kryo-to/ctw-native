import { useCallback, useEffect, useRef, useState, type ReactNode } from 'react'
import { AlertTriangle, ArrowLeft, Check as CheckIcon, FileArchive, FolderOpen, Gamepad2, Minus, Play, RotateCcw, X } from 'lucide-react'
import { AsciiBar, Button, Caption, Check, Label } from '@/ui'
import { AsciiArt } from '@/ui/ascii/AsciiArt'
import { WORDMARK } from '@/ui/ascii/cells'
import { KryoMark } from '@/ui/ascii/KryoMark'
import { cn } from '@/lib/utils'
import {
  bundledGame,
  cancelPort,
  freeFolder,
  inspectApk,
  minimize,
  onFileDrop,
  onPortEvents,
  openLink,
  overall,
  pickApk,
  pickFolder,
  play,
  quit,
  showFolder,
  startPort,
  type ApkSummary,
  type PortProgress,
  type PortResult,
  type Stage,
} from '@/lib/porter'

const REPO = 'https://github.com/official-kryo-to/ctw-native'
const DISCORD = 'https://discord.gg/ZXTvWB7gnb'

type Screen =
  | { kind: 'idle' }
  | { kind: 'checking'; path: string }
  | { kind: 'ready'; apk: ApkSummary; folder: string }
  | { kind: 'working'; apk: ApkSummary; folder: string; progress: PortProgress | null }
  | { kind: 'done'; result: PortResult }
  | { kind: 'error'; message: string; apk: ApkSummary | null; folder: string | null }

function errorText(e: unknown): string {
  if (typeof e === 'string') return e
  if (e instanceof Error) return e.message
  return String(e)
}

export function App() {
  const [screen, setScreen] = useState<Screen>({ kind: 'idle' })
  const [dragging, setDragging] = useState(false)
  const [hasGame, setHasGame] = useState(true)
  const [withModkit, setWithModkit] = useState(true)
  const [shortcut, setShortcut] = useState(true)
  const screenRef = useRef(screen)
  screenRef.current = screen

  useEffect(() => {
    void bundledGame().then(setHasGame)
  }, [])

  const choose = useCallback(async (path: string | null) => {
    if (!path) return
    const busy = screenRef.current.kind
    if (busy === 'working' || busy === 'checking') return
    setScreen({ kind: 'checking', path })
    try {
      const apk = await inspectApk(path)
      setScreen({ kind: 'ready', apk, folder: await freeFolder(apk.suggestedFolder) })
    } catch (e) {
      setScreen({ kind: 'error', message: errorText(e), apk: null, folder: null })
    }
  }, [])

  useEffect(() => {
    const off = onFileDrop(setDragging, (paths) => void choose(paths[0] ?? null))
    return () => void off.then((f) => f())
  }, [choose])

  useEffect(() => {
    const off = onPortEvents({
      progress: (progress) =>
        setScreen((s) => (s.kind === 'working' ? { ...s, progress } : s)),
      finished: (result) => setScreen({ kind: 'done', result }),
      failed: (message) => {
        setScreen((s) =>
          s.kind === 'working'
            ? message === 'Cancelled.'
              ? { kind: 'ready', apk: s.apk, folder: s.folder }
              : { kind: 'error', message, apk: s.apk, folder: s.folder }
            : s,
        )
      },
    })
    return () => void off.then((f) => f())
  }, [])

  const port = async (apk: ApkSummary, folder: string) => {
    setScreen({ kind: 'working', apk, folder, progress: null })
    try {
      await startPort({ apk: apk.path, folder, withModkit, shortcut })
    } catch (e) {
      setScreen({ kind: 'error', message: errorText(e), apk, folder })
    }
  }

  return (
    <div className="relative grid h-full grid-rows-[auto_1fr] bg-background text-foreground">
      <TitleBar onClose={() => void quit()} />
      <main className="relative grid min-h-0 grid-rows-[minmax(0,1fr)_auto]">
        <Hero />
        <section className="relative z-10 -mt-24 px-10 pb-5">
          {!hasGame ? <NoGameNotice /> : null}
          <div className="kryo-radius border border-border bg-card/95 p-6 shadow-2xl shadow-black/70 backdrop-blur">
            <Body
              screen={screen}
              withModkit={withModkit}
              shortcut={shortcut}
              onModkit={setWithModkit}
              onShortcut={setShortcut}
              onPick={() => void pickApk().then(choose)}
              onFolder={(folder) => setScreen((s) => (s.kind === 'ready' ? { ...s, folder } : s))}
              onPort={port}
              onReset={() => setScreen({ kind: 'idle' })}
            />
          </div>
          <Footer />
        </section>
      </main>
      {dragging && screen.kind !== 'working' ? <DropOverlay /> : null}
    </div>
  )
}

/* ── Chrome ─────────────────────────────────────────────── */

function TitleBar({ onClose }: { onClose: () => void }) {
  const btn =
    'no-drag kryo-pill grid size-7 place-items-center text-muted-foreground transition-colors hover:bg-secondary hover:text-foreground'
  return (
    <header className="drag relative z-30 flex h-10 shrink-0 items-center gap-3 border-b border-border bg-background pl-4 pr-2">
      <KryoMark className="pointer-events-none h-3.5" />
      <span className="pointer-events-none text-[11px] font-bold uppercase tracking-[0.3em]">CTW-Native Porter</span>
      <span className="pointer-events-none kryo-pill border border-border px-2 py-0.5 text-[10px] tracking-wider text-muted-foreground">
        v{__APP_VERSION__}
      </span>
      <div className="grow" />
      <button type="button" aria-label="Minimize" title="Minimize" className={btn} onClick={() => void minimize()}>
        <Minus className="size-3.5" />
      </button>
      <button type="button" aria-label="Close" title="Close" className={btn} onClick={onClose}>
        <X className="size-3.5" />
      </button>
    </header>
  )
}

/** The key art, fading into the page, with the port's logo on it. */
function Hero() {
  return (
    <div className="relative min-h-0 overflow-hidden bg-black">
      <img
        src="/art/banner.webp"
        alt=""
        draggable={false}
        className="kryo-hero-art pointer-events-none absolute inset-0 size-full object-cover object-[50%_30%]"
      />
      <div className="pointer-events-none absolute inset-0 bg-[radial-gradient(ellipse_at_70%_30%,transparent_20%,rgba(0,0,0,0.45)_80%)]" />
      <div className="pointer-events-none absolute inset-x-0 bottom-0 h-3/4 bg-gradient-to-t from-background via-background/70 to-transparent" />
      <img
        src="/art/logo.webp"
        alt="CTW-Native, the PC port"
        draggable={false}
        className="kryo-in pointer-events-none absolute bottom-28 left-10 w-40 drop-shadow-[0_6px_30px_rgba(0,0,0,0.9)]"
        style={{ animationDelay: '150ms' }}
      />
    </div>
  )
}

function DropOverlay() {
  return (
    <div className="kryo-fade pointer-events-none absolute inset-0 z-40 grid place-items-center bg-black/75 backdrop-blur-sm">
      <div className="kryo-radius grid justify-items-center gap-3 border-2 border-dashed border-primary px-16 py-12">
        <FileArchive className="size-10" />
        <p className="text-sm font-bold uppercase tracking-[0.3em]">Drop to port it</p>
      </div>
    </div>
  )
}

function NoGameNotice() {
  return (
    <p className="kryo-radius mb-3 flex items-center gap-2 border border-warning/50 bg-warning/10 px-4 py-2 text-[11px] text-warning">
      <AlertTriangle className="size-3.5 shrink-0" />
      This is a development build without the game inside. Get the Porter from the Releases page to port an APK.
    </p>
  )
}

function Footer() {
  const link = 'no-drag text-muted-foreground underline-offset-4 transition-colors hover:text-foreground hover:underline'
  return (
    <footer className="mt-4 flex items-end justify-between gap-6 text-[10px] leading-relaxed text-muted-foreground">
      <p className="max-w-xl">
        Uses your own copy of the game: nothing from Rockstar is downloaded or included. A fan project, not affiliated with
        Rockstar Games or Take-Two.{' '}
        <button type="button" className={link} onClick={() => void openLink(REPO)}>
          Source
        </button>
        {' · '}
        <button type="button" className={link} onClick={() => void openLink(DISCORD)}>
          Discord
        </button>
      </p>
      <button type="button" aria-label="kryo.to" className="no-drag shrink-0" onClick={() => void openLink('https://kryo.to')}>
        <KryoMark className="h-4 text-muted-foreground transition-colors hover:text-foreground" />
      </button>
    </footer>
  )
}

/* ── The panel ──────────────────────────────────────────── */

function Body(props: {
  screen: Screen
  withModkit: boolean
  shortcut: boolean
  onModkit: (v: boolean) => void
  onShortcut: (v: boolean) => void
  onPick: () => void
  onFolder: (folder: string) => void
  onPort: (apk: ApkSummary, folder: string) => void
  onReset: () => void
}) {
  const { screen } = props
  switch (screen.kind) {
    case 'idle':
      return <DropZone onPick={props.onPick} />
    case 'checking':
      return (
        <Centered>
          <AsciiBar fraction={null} cells={28} showPct={false} className="text-foreground" />
          <Caption>Checking {fileName(screen.path)}</Caption>
        </Centered>
      )
    case 'ready':
      return (
        <Ready
          apk={screen.apk}
          folder={screen.folder}
          withModkit={props.withModkit}
          shortcut={props.shortcut}
          onModkit={props.onModkit}
          onShortcut={props.onShortcut}
          onFolder={props.onFolder}
          onBack={props.onReset}
          onPort={() => props.onPort(screen.apk, screen.folder)}
        />
      )
    case 'working':
      return <Working progress={screen.progress} folder={screen.folder} />
    case 'done':
      return <Done result={screen.result} onReset={props.onReset} />
    case 'error':
      return (
        <div className="kryo-in grid gap-5">
          <div className="flex items-start gap-3">
            <AlertTriangle className="mt-0.5 size-5 shrink-0 text-destructive" />
            <div className="grid gap-1.5">
              <Label className="text-destructive">That did not work</Label>
              <p className="select-text text-sm leading-relaxed">{screen.message}</p>
            </div>
          </div>
          <div className="flex gap-2">
            {screen.apk && screen.folder ? (
              <Button variant="primary" onClick={() => props.onPort(screen.apk!, screen.folder!)}>
                <RotateCcw className="size-3.5" />
                Try again
              </Button>
            ) : null}
            <Button onClick={props.onPick}>Choose another APK</Button>
            <Button variant="ghost" onClick={props.onReset}>
              Start over
            </Button>
          </div>
        </div>
      )
  }
}

function Centered({ children }: { children: ReactNode }) {
  return <div className="kryo-fade grid min-h-40 place-content-center justify-items-center gap-4">{children}</div>
}

const STEPS: { n: string; title: string; text: string }[] = [
  { n: '1', title: 'Drop your APK', text: 'GTA: Chinatown Wars for Android, version 4.4.243.' },
  { n: '2', title: 'Port it', text: 'The game files go into a new folder. Takes a minute.' },
  { n: '3', title: 'Play', text: 'Start GTACTW.exe, or the shortcut on your desktop.' },
]

function DropZone({ onPick }: { onPick: () => void }) {
  return (
    <div className="kryo-in grid grid-cols-[1.3fr_1fr] gap-6">
      <button
        type="button"
        onClick={onPick}
        className="kryo-radius group grid min-h-40 place-content-center justify-items-center gap-3 border-2 border-dashed border-border bg-background/40 px-6 py-8 text-center transition-colors hover:border-foreground hover:bg-background/70"
      >
        <FileArchive className="size-8 text-muted-foreground transition-colors group-hover:text-foreground" />
        <span className="text-sm font-bold uppercase tracking-[0.3em]">Drop your APK here</span>
        <span className="text-[11px] text-muted-foreground">or click to choose it</span>
      </button>
      <ol className="grid content-center gap-4">
        {STEPS.map((s) => (
          <li key={s.n} className="flex gap-3">
            <span className="kryo-pill grid size-6 shrink-0 place-items-center border border-border text-[10px] font-bold">{s.n}</span>
            <span className="grid gap-0.5">
              <span className="text-[11px] font-bold uppercase tracking-wider">{s.title}</span>
              <span className="text-[11px] leading-relaxed text-muted-foreground">{s.text}</span>
            </span>
          </li>
        ))}
      </ol>
    </div>
  )
}

function Ready(props: {
  apk: ApkSummary
  folder: string
  withModkit: boolean
  shortcut: boolean
  onModkit: (v: boolean) => void
  onShortcut: (v: boolean) => void
  onFolder: (f: string) => void
  onBack: () => void
  onPort: () => void
}) {
  const { apk } = props
  return (
    <div className="kryo-in grid gap-5">
      <div className="flex items-center gap-4">
        <span className="kryo-pill grid size-10 shrink-0 place-items-center bg-success/15 text-success">
          <CheckIcon className="size-5" strokeWidth={3} />
        </span>
        <div className="grid min-w-0 grow gap-0.5">
          <span className="truncate text-sm font-bold" title={apk.path}>
            {apk.fileName}
          </span>
          <Caption>
            GTA: Chinatown Wars {apk.version} · {apk.files.toLocaleString()} files · needs {size(apk.bytes)}
          </Caption>
        </div>
        <Button variant="ghost" size="sm" onClick={props.onBack}>
          <ArrowLeft className="size-3" />
          Other APK
        </Button>
      </div>
      <div className="grid gap-2">
        <Caption className="font-bold text-foreground/80">Install to</Caption>
        <div className="flex gap-2">
          <div
            className="kryo-pill flex h-9 min-w-0 grow items-center border border-border bg-background px-4 text-xs"
            title={props.folder}
          >
            <span className="truncate">{props.folder}</span>
          </div>
          <Button
            onClick={() =>
              void pickFolder(props.folder).then(async (dir) => {
                if (dir) props.onFolder(await freeFolder(joinPath(dir, 'GTA Chinatown Wars PC')))
              })
            }
          >
            <FolderOpen className="size-3.5" />
            Change
          </Button>
        </div>
      </div>
      <div className="flex items-center justify-between gap-6">
        <div className="grid gap-2.5">
          <Check checked={props.withModkit} onChange={props.onModkit} label="Mod menu and example mods (F4 in game)" />
          <Check checked={props.shortcut} onChange={props.onShortcut} label="Shortcut on the desktop" />
        </div>
        <Button variant="primary" size="lg" onClick={props.onPort}>
          <Gamepad2 className="size-4" />
          Port it
        </Button>
      </div>
    </div>
  )
}

const STAGES: { stage: Stage; label: string }[] = [
  { stage: 'checking', label: 'Checking the APK' },
  { stage: 'tables', label: "Reading the game's tables" },
  { stage: 'assets', label: 'Copying the game files' },
  { stage: 'installing', label: 'Installing the game' },
]

function Working({ progress, folder }: { progress: PortProgress | null; folder: string }) {
  const fraction = overall(progress)
  const eta = useEta(progress)
  const current = STAGES.findIndex((s) => s.stage === (progress?.stage ?? 'checking'))
  return (
    <div className="kryo-fade grid grid-cols-[auto_1fr] items-center gap-10">
      {/* Kryoto Desktop's sign-in animation: the wordmark scrambles in, then shimmers while the work runs. */}
      <div className="grid w-56 justify-items-center gap-3 py-2">
        <AsciiArt lines={WORDMARK} mode="reveal-shimmer" revealMs={1300} className="w-full" label="kryo.to" />
        <p className="text-[10px] uppercase tracking-[0.5em] text-foreground/70">Porting</p>
      </div>
      <div className="grid gap-4">
        <ol className="grid gap-1.5">
          {STAGES.map((s, i) => (
            <li
              key={s.stage}
              className={cn(
                'flex items-center gap-2.5 text-[11px] uppercase tracking-wider',
                i < current ? 'text-muted-foreground' : i === current ? 'font-bold text-foreground' : 'text-muted-foreground/50',
              )}
            >
              <span className="grid size-3.5 place-items-center">
                {i < current ? <CheckIcon className="size-3.5 text-success" strokeWidth={3} /> : i === current ? <span className="kryo-blink">▸</span> : '·'}
              </span>
              {s.label}
              {s.stage === 'assets' && i === current && progress && progress.filesTotal > 0 ? (
                <span className="font-normal tabular-nums text-muted-foreground">
                  {progress.filesDone.toLocaleString()} / {progress.filesTotal.toLocaleString()}
                </span>
              ) : null}
            </li>
          ))}
        </ol>
        <AsciiBar fraction={progress ? fraction : null} cells={40} className="text-sm text-foreground" />
        <div className="flex items-center justify-between gap-4">
          <Caption className="truncate normal-case tracking-normal" >
            {progress?.stage === 'assets'
              ? `${size(progress.bytesDone)} of ${size(progress.bytesTotal)}${eta ? ` · about ${eta} left` : ''}`
              : `Into ${folder}`}
          </Caption>
          <Button variant="ghost" size="sm" onClick={() => void cancelPort()}>
            Cancel
          </Button>
        </div>
      </div>
    </div>
  )
}

function Done({ result, onReset }: { result: PortResult; onReset: () => void }) {
  return (
    <div className="kryo-pop grid gap-5">
      <div className="flex items-center gap-4">
        <span className="kryo-pill grid size-10 shrink-0 place-items-center bg-success/15 text-success">
          <CheckIcon className="size-5" strokeWidth={3} />
        </span>
        <div className="grid min-w-0 gap-0.5">
          <Label>Ready to play</Label>
          <span className="truncate text-[11px] text-muted-foreground" title={result.folder}>
            {result.folder}
            {result.shortcut ? ' · shortcut on your desktop' : ''}
          </span>
        </div>
      </div>
      <div className="flex items-center gap-2">
        <Button variant="primary" size="lg" onClick={() => void play(result.exe)}>
          <Play className="size-4" />
          Play
        </Button>
        <Button onClick={() => void showFolder(result.exe)}>
          <FolderOpen className="size-3.5" />
          Show folder
        </Button>
        <div className="grow" />
        <Button variant="ghost" size="sm" onClick={onReset}>
          Port another APK
        </Button>
      </div>
    </div>
  )
}

/* ── Helpers ────────────────────────────────────────────── */

function fileName(path: string): string {
  return path.split(/[\\/]/).pop() ?? path
}

function joinPath(dir: string, name: string): string {
  const sep = dir.includes('\\') ? '\\' : '/'
  return dir.endsWith(sep) ? dir + name : dir + sep + name
}

function size(bytes: number): string {
  if (bytes >= 1e9) return `${(bytes / 1e9).toFixed(1)} GB`
  if (bytes >= 1e6) return `${Math.round(bytes / 1e6)} MB`
  return `${Math.round(bytes / 1e3)} KB`
}

/** Time left for the copying, from the rate over the last few seconds. */
function useEta(progress: PortProgress | null): string | null {
  const samples = useRef<{ t: number; bytes: number }[]>([])
  const [eta, setEta] = useState<string | null>(null)
  useEffect(() => {
    if (!progress || progress.stage !== 'assets') {
      samples.current = []
      setEta(null)
      return
    }
    const now = performance.now()
    const list = samples.current
    list.push({ t: now, bytes: progress.bytesDone })
    while (list.length > 2 && now - list[0]!.t > 5000) list.shift()
    const first = list[0]!
    const rate = (progress.bytesDone - first.bytes) / Math.max(1, now - first.t)
    if (now - first.t < 1500 || rate <= 0) return setEta(null)
    const seconds = (progress.bytesTotal - progress.bytesDone) / rate / 1000
    setEta(seconds < 60 ? `${Math.max(1, Math.round(seconds))}s` : `${Math.round(seconds / 60)} min`)
  }, [progress])
  return eta
}
