import { useEffect, useId, useState } from 'react'
import { cn } from '@/lib/utils'
import { MARK_H, MARK_PATH, MARK_W } from '@/ui/ascii/mark'

/**
 * kryo.to's K// mark - the site's own, traced from its image (see
 * scripts/trace-mark.mjs), so it is the same mark to the pixel and sharp at
 * any size.
 *
 * - `reveal`: wiped in from the left in pixel steps, once.
 * - `shimmer`: a light band sweeps across it on a loop - the loading state.
 * - `reveal-shimmer`: both.
 *
 * Size it by height; the width follows.
 */
export function KryoMark({
  mode = 'still',
  className,
  label = 'Kryoto',
  onRevealed,
}: {
  mode?: 'still' | 'reveal' | 'shimmer' | 'reveal-shimmer'
  className?: string
  label?: string
  onRevealed?: () => void
}) {
  const id = useId().replace(/:/g, '')
  const reduced = typeof window !== 'undefined' && window.matchMedia?.('(prefers-reduced-motion: reduce)').matches
  const reveals = !reduced && (mode === 'reveal' || mode === 'reveal-shimmer')
  const shimmers = !reduced && (mode === 'shimmer' || mode === 'reveal-shimmer')
  const STEPS = 14
  const [step, setStep] = useState(reveals ? 0 : STEPS)

  useEffect(() => {
    if (!reveals) {
      onRevealed?.()
      return
    }
    setStep(0)
    let n = 0
    const t = window.setInterval(() => {
      n++
      setStep(n)
      if (n >= STEPS) {
        window.clearInterval(t)
        onRevealed?.()
      }
    }, 48)
    return () => window.clearInterval(t)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [reveals])

  const pad = 8
  const vw = MARK_W + pad * 2
  const vh = MARK_H + pad * 2
  const revealed = step >= STEPS
  return (
    <svg
      role="img"
      aria-label={label}
      viewBox={`${-pad} ${-pad} ${vw} ${vh}`}
      className={cn('block w-auto shrink-0 text-foreground', className)}
      style={{ aspectRatio: `${vw} / ${vh}` }}
    >
      <defs>
        <clipPath id={`${id}-wipe`}>
          <rect x={-pad} y={-pad} width={(vw * step) / STEPS} height={vh} />
        </clipPath>
        {shimmers ? (
          <linearGradient id={`${id}-band`} gradientUnits="userSpaceOnUse" x1={0} y1={0} x2={MARK_W * 0.35} y2={MARK_H * 0.35}>
            <stop offset="0" stopColor="var(--background)" stopOpacity="0" />
            <stop offset="0.5" stopColor="var(--background)" stopOpacity="0.55" />
            <stop offset="1" stopColor="var(--background)" stopOpacity="0" />
            <animateTransform
              attributeName="gradientTransform"
              type="translate"
              from={`${-MARK_W * 0.6} 0`}
              to={`${MARK_W * 1.2} 0`}
              dur="1.7s"
              repeatCount="indefinite"
            />
          </linearGradient>
        ) : null}
      </defs>
      <g clipPath={revealed ? undefined : `url(#${id}-wipe)`}>
        <path d={MARK_PATH} fill="currentColor" />
        {shimmers && revealed ? <path d={MARK_PATH} fill={`url(#${id}-band)`} /> : null}
      </g>
    </svg>
  )
}
