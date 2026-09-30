import { useEffect, useState, type ReactNode } from 'react'
import { Check as CheckIcon } from 'lucide-react'
import { cn } from '@/lib/utils'

/**
 * Kryoto's primitives, copied from Kryoto Desktop (src/ui/index.tsx): tracked
 * uppercase labels, hairline borders on near-black cards, pill chrome, a white
 * pill for the one thing that matters on a surface.
 */

/* ── Type ────────────────────────────────────────────────── */

/** The site's section heading: `text-xs uppercase tracking-[0.25em]`. */
export function Label({ children, className }: { children: ReactNode; className?: string }) {
  return <h2 className={cn('text-xs uppercase tracking-[0.25em] text-primary', className)}>{children}</h2>
}

/** The small caption under or beside a value. */
export function Caption({ children, className }: { children: ReactNode; className?: string }) {
  return (
    <span className={cn('text-[10px] uppercase tracking-wider text-muted-foreground', className)}>{children}</span>
  )
}

/* ── Buttons ─────────────────────────────────────────────── */

type ButtonProps = React.ComponentProps<'button'> & {
  variant?: 'primary' | 'outline' | 'ghost' | 'danger'
  size?: 'sm' | 'md' | 'lg'
}

/** Pill buttons. `primary` is the white pill - one per surface. */
export function Button({ variant = 'outline', size = 'md', className, ...rest }: ButtonProps) {
  return (
    <button
      type="button"
      {...rest}
      className={cn(
        'kryo-pill kryo-press inline-flex shrink-0 items-center justify-center gap-2 font-bold uppercase tracking-wider disabled:opacity-40',
        size === 'sm' && 'h-7 px-3 text-[10px]',
        size === 'md' && 'h-9 px-4 text-[11px]',
        size === 'lg' && 'h-12 px-8 text-sm tracking-[0.2em]',
        variant === 'primary' && 'bg-primary text-primary-foreground hover:opacity-90',
        variant === 'outline' &&
          'border border-border text-muted-foreground hover:border-foreground hover:text-foreground',
        variant === 'ghost' && 'text-muted-foreground hover:bg-secondary hover:text-foreground',
        variant === 'danger' &&
          'border border-destructive/50 text-destructive hover:bg-destructive hover:text-destructive-foreground',
        className,
      )}
    />
  )
}

/** A checkbox drawn by the client, never the OS widget. */
export function Check({
  checked,
  onChange,
  label,
  disabled = false,
}: {
  checked: boolean
  onChange: (next: boolean) => void
  label: ReactNode
  disabled?: boolean
}) {
  return (
    <button
      type="button"
      role="checkbox"
      aria-checked={checked}
      disabled={disabled}
      onClick={() => onChange(!checked)}
      className="kryo-square flex items-center gap-2.5 text-left text-xs text-foreground disabled:opacity-40"
    >
      <span
        className={cn(
          'grid size-4 shrink-0 place-items-center border transition-colors',
          checked ? 'border-primary bg-primary text-primary-foreground' : 'border-border',
        )}
        style={{ borderRadius: 'min(var(--kryo-radius), 5px)' }}
      >
        {checked ? <CheckIcon className="size-3" strokeWidth={3} /> : null}
      </span>
      <span>{label}</span>
    </button>
  )
}

/* ── ASCII ───────────────────────────────────────────────── */

const FILLED = '█'
const PARTIAL = ['', '▏', '▎', '▍', '▌', '▋', '▊', '▉']
const EMPTY = '·'

/** kryo.to's AsciiBar track: whole blocks, an eighth-block head, dotted rest. */
export function asciiTrack(fraction: number, cells: number): string {
  const f = Math.max(0, Math.min(1, fraction))
  const exact = f * cells
  const whole = Math.floor(exact)
  const head = whole >= cells ? '' : (PARTIAL[Math.floor((exact - whole) * 8)] ?? '')
  return FILLED.repeat(Math.min(whole, cells)) + head + EMPTY.repeat(Math.max(0, cells - whole - (head ? 1 : 0)))
}

function sweep(tick: number, cells: number): string {
  const width = 5
  const max = Math.max(0, cells - width)
  const step = max ? Math.abs(tick) % (max * 2) : 0
  const pos = step <= max ? step : max * 2 - step
  let out = ''
  for (let i = 0; i < cells; i++) out += i >= pos && i < pos + width ? FILLED : EMPTY
  return out
}

/** Progress as the site draws it. `null` sweeps, for work with no total. */
export function AsciiBar({
  fraction,
  cells = 28,
  className,
  showPct = true,
}: {
  fraction: number | null
  cells?: number
  className?: string
  showPct?: boolean
}) {
  const [tick, setTick] = useState(0)
  useEffect(() => {
    if (fraction !== null) return
    const t = window.setInterval(() => setTick((n) => n + 1), 110)
    return () => window.clearInterval(t)
  }, [fraction])
  return (
    <span
      role="progressbar"
      aria-valuenow={fraction === null ? undefined : Math.round(fraction * 100)}
      aria-valuemin={0}
      aria-valuemax={100}
      className={cn('kryo-ascii-art inline-flex items-baseline gap-2 whitespace-pre text-xs leading-none', className)}
    >
      <span>{fraction === null ? sweep(tick, cells) : asciiTrack(fraction, cells)}</span>
      {showPct ? (
        <span className="w-12 text-right tabular-nums text-muted-foreground">
          {fraction === null ? '···' : `${(fraction * 100).toFixed(fraction >= 0.995 ? 0 : 1)}%`}
        </span>
      ) : null}
    </span>
  )
}
