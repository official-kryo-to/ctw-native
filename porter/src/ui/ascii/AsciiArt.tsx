import { useEffect, useMemo, useState } from 'react'
import { cn } from '@/lib/utils'
import { asciiPaths, gridSize, SCRAMBLE } from '@/ui/ascii/cells'

/**
 * Block-and-double-line art as crisp vectors (see `cells.ts`), optionally
 * animated in the site's scramble voice.
 *
 * - `still`: drawn as is.
 * - `reveal`: scrambles in from the left and settles, once.
 * - `shimmer`: a light band sweeps across the blocks, forever - the loading
 *   state.
 * - `reveal-shimmer`: both, in that order.
 *
 * Size it by height (`className="h-12"`); the width follows the art.
 */
export type AsciiMode = 'still' | 'reveal' | 'shimmer' | 'reveal-shimmer'

const FRAME_MS = 34

function reducedMotion() {
  return typeof window !== 'undefined' && window.matchMedia?.('(prefers-reduced-motion: reduce)').matches
}

/** A stable pseudo-random pick for one cell on one frame. */
function pick(col: number, row: number, frame: number) {
  const n = Math.sin(col * 12.9898 + row * 78.233 + frame * 3.7) * 43758.5453
  return SCRAMBLE[Math.floor((n - Math.floor(n)) * SCRAMBLE.length)] ?? '█'
}

export function AsciiArt({
  lines,
  mode = 'still',
  revealMs = 900,
  className,
  label,
  onRevealed,
  from,
}: {
  lines: readonly string[]
  /** Art to scramble out of instead of nothing: a morph from `from` to `lines`. */
  from?: readonly string[]
  mode?: AsciiMode
  revealMs?: number
  className?: string
  label?: string
  onRevealed?: () => void
}) {
  const reveals = mode === 'reveal' || mode === 'reveal-shimmer'
  const shimmers = mode === 'shimmer' || mode === 'reveal-shimmer'
  const still = mode === 'still' || reducedMotion()
  const [t, setT] = useState(0)

  useEffect(() => {
    if (still) {
      onRevealed?.()
      return
    }
    const start = performance.now()
    let told = false
    const id = window.setInterval(() => {
      const elapsed = performance.now() - start
      setT(elapsed)
      if (reveals && !told && elapsed >= revealMs) {
        told = true
        onRevealed?.()
        if (!shimmers) window.clearInterval(id)
      }
    }, FRAME_MS)
    return () => window.clearInterval(id)
    // The art and mode define the animation; a new callback does not restart it.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [lines, from, mode, revealMs, still])

  const size = gridSize(from && gridSize(from).cols > gridSize(lines).cols ? from : lines)
  const { cols, width } = size
  const rows = Math.max(lines.length, from?.length ?? 0)
  const height = rows * gridSize(['']).height
  const frame = Math.floor(t / FRAME_MS)
  const shown = useMemo(() => {
    if (still) return lines
    const grid = Array.from({ length: rows }, (_, r) => Array.from((lines[r] ?? '').padEnd(cols)))
    const before = from ? Array.from({ length: rows }, (_, r) => Array.from((from[r] ?? '').padEnd(cols))) : null
    // Reveal: a front sweeps left to right; a few columns behind it are
    // still scrambling, everything past it is empty.
    if (reveals && t < revealMs) {
      const band = 6
      const front = (t / revealMs) * (cols + band)
      for (let r = 0; r < rows; r++) {
        for (let c = 0; c < cols; c++) {
          const target = grid[r]![c]!
          const was = before?.[r]?.[c] ?? ' '
          const lag = front - c - (r % 2) * 0.5
          if (lag <= 0) grid[r]![c] = was
          else if (lag < band && (target !== ' ' || was !== ' ')) grid[r]![c] = pick(c, r, frame)
        }
      }
      return grid.map((g) => g.join(''))
    }
    // Shimmer: a diagonal band of lighter blocks, once every ~1.6s.
    if (shimmers) {
      const since = reveals ? t - revealMs : t
      const period = 1600
      const span = cols + rows + 10
      const pos = ((since % period) / period) * span - 5
      for (let r = 0; r < rows; r++) {
        for (let c = 0; c < cols; c++) {
          if (grid[r]![c] !== '█') continue
          const d = Math.abs(c + r * 0.8 - pos)
          if (d < 1) grid[r]![c] = '▒'
          else if (d < 2.2) grid[r]![c] = '▓'
        }
      }
      return grid.map((g) => g.join(''))
    }
    return lines
  }, [lines, from, still, reveals, shimmers, t, revealMs, cols, rows, frame])

  const paths = useMemo(() => asciiPaths(shown), [shown])
  return (
    <svg
      role={label ? 'img' : undefined}
      aria-label={label}
      aria-hidden={label ? undefined : true}
      viewBox={`0 0 ${width} ${height}`}
      className={cn('block w-auto shrink-0 text-foreground', className)}
      style={{ aspectRatio: `${width} / ${height}` }}
    >
      {paths.map((p) => (
        <path key={p.alpha} d={p.d} fill="currentColor" fillOpacity={p.alpha < 1 ? p.alpha : undefined} />
      ))}
    </svg>
  )
}
