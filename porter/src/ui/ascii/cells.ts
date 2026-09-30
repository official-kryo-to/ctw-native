/**
 * Kryoto's block-and-double-line lettering, drawn as vector shapes.
 *
 * kryo.to sets its wordmark as text: block and box-drawing characters in a
 * monospace font. At small sizes, and on some fonts, the rows gap, the double
 * lines drift off the blocks and the whole thing blurs. Here every character
 * is a cell drawn from rectangles instead, so the art is exact at any size -
 * in the app (an SVG), and in the brand files `scripts/brand.mjs` renders from
 * this same module.
 *
 * Kept free of imports so Node can load it directly.
 */

export const CELL_W = 12
export const CELL_H = 20

// The two strokes of a double line: vertical at x = A and B, horizontal at
// y = T and U, each 1.6 units thick.
const A: [number, number] = [2.9, 4.5]
const B: [number, number] = [7.5, 9.1]
const T: [number, number] = [7.0, 8.6]
const U: [number, number] = [11.4, 13.0]
const W = CELL_W
const H = CELL_H
// A single line, centred in the cell.
const SX: [number, number] = [5.2, 6.8]
const SY: [number, number] = [9.2, 10.8]

type Rect = [x: number, y: number, w: number, h: number]
const r = (x0: number, y0: number, x1: number, y1: number): Rect => [x0, y0, x1 - x0, y1 - y0]
const hbar = (band: [number, number], x0: number, x1: number) => r(x0, band[0], x1, band[1])
const vbar = (band: [number, number], y0: number, y1: number) => r(band[0], y0, band[1], y1)

/** Each character as rectangles in a W x H cell, with an opacity for shades. */
const SHAPES: Record<string, { rects: Rect[]; alpha?: number }> = {
  '█': { rects: [r(0, 0, W, H)] },
  '▓': { rects: [r(0, 0, W, H)], alpha: 0.72 },
  '▒': { rects: [r(0, 0, W, H)], alpha: 0.45 },
  '░': { rects: [r(0, 0, W, H)], alpha: 0.2 },
  '▀': { rects: [r(0, 0, W, H / 2)] },
  '▄': { rects: [r(0, H / 2, W, H)] },
  '▌': { rects: [r(0, 0, W / 2, H)] },
  '▐': { rects: [r(W / 2, 0, W, H)] },
  '■': { rects: [r(2, 6, W - 2, H - 6)] },
  '▪': { rects: [r(3.5, 7.5, W - 3.5, H - 7.5)] },
  '·': { rects: [r(5, 9, 7, 11)] },
  '─': { rects: [hbar(SY, 0, W)] },
  '│': { rects: [vbar(SX, 0, H)] },
  '┌': { rects: [hbar(SY, SX[0], W), vbar(SX, SY[0], H)] },
  '┐': { rects: [hbar(SY, 0, SX[1]), vbar(SX, SY[0], H)] },
  '└': { rects: [hbar(SY, SX[0], W), vbar(SX, 0, SY[1])] },
  '┘': { rects: [hbar(SY, 0, SX[1]), vbar(SX, 0, SY[1])] },
  '▼': { rects: [r(1, 6, W - 1, 8.5), r(3, 8.5, W - 3, 11), r(5, 11, W - 5, 13.5)] },
  '═': { rects: [hbar(T, 0, W), hbar(U, 0, W)] },
  '║': { rects: [vbar(A, 0, H), vbar(B, 0, H)] },
  '╗': { rects: [hbar(T, 0, B[1]), vbar(B, T[0], H), hbar(U, 0, A[1]), vbar(A, U[0], H)] },
  '╔': { rects: [hbar(T, A[0], W), vbar(A, T[0], H), hbar(U, B[0], W), vbar(B, U[0], H)] },
  '╝': { rects: [hbar(U, 0, B[1]), vbar(B, 0, U[1]), hbar(T, 0, A[1]), vbar(A, 0, T[1])] },
  '╚': { rects: [hbar(U, A[0], W), vbar(A, 0, U[1]), hbar(T, B[0], W), vbar(B, 0, T[1])] },
  '╠': { rects: [vbar(A, 0, H), vbar(B, 0, T[1]), hbar(T, B[0], W), vbar(B, U[0], H), hbar(U, B[0], W)] },
  '╣': { rects: [vbar(B, 0, H), vbar(A, 0, T[1]), hbar(T, 0, A[1]), vbar(A, U[0], H), hbar(U, 0, A[1])] },
}

/** Characters the scramble passes through: all drawable, all on-brand. */
export const SCRAMBLE = '░▒▓█╗╔╝╚═║▀▄'

export function drawable(ch: string) {
  return ch in SHAPES
}

/**
 * SVG path data for a grid of lines, one path per opacity so shades come out
 * as shades. Rows may differ in length; anything not drawable is a space.
 */
export function asciiPaths(lines: readonly string[]): { d: string; alpha: number }[] {
  const byAlpha = new Map<number, Rect[]>()
  lines.forEach((line, row) => {
    Array.from(line).forEach((ch, col) => {
      const shape = SHAPES[ch]
      if (!shape) return
      const alpha = shape.alpha ?? 1
      const rects = byAlpha.get(alpha) ?? []
      for (const [x, y, w, h] of shape.rects) rects.push([+(col * W + x).toFixed(2), +(row * H + y).toFixed(2), +w.toFixed(2), +h.toFixed(2)])
      byAlpha.set(alpha, rects)
    })
  })
  return [...byAlpha.entries()].map(([alpha, rects]) => ({
    alpha,
    d: merge(rects)
      .map(([x, y, w, h]) => `M${x} ${y}h${w}v${h}h${-w}z`)
      .join(''),
  }))
}

/**
 * Join rectangles that touch edge to edge into one - first along each row,
 * then down the columns - so a run of blocks is one shape. Separate shapes
 * that merely touch leave a hairline seam where anti-aliasing meets itself.
 */
function merge(rects: Rect[]): Rect[] {
  const same = (a: number, b: number) => Math.abs(a - b) < 0.01
  const rows = [...rects].sort((a, b) => a[1] - b[1] || a[3] - b[3] || a[0] - b[0])
  const across: Rect[] = []
  for (const r of rows) {
    const last = across[across.length - 1]
    if (last && same(last[1], r[1]) && same(last[3], r[3]) && same(last[0] + last[2], r[0])) last[2] = +(last[2] + r[2]).toFixed(2)
    else across.push([...r] as Rect)
  }
  const cols = across.sort((a, b) => a[0] - b[0] || a[2] - b[2] || a[1] - b[1])
  const down: Rect[] = []
  for (const r of cols) {
    const last = down[down.length - 1]
    if (last && same(last[0], r[0]) && same(last[2], r[2]) && same(last[1] + last[3], r[1])) last[3] = +(last[3] + r[3]).toFixed(2)
    else down.push([...r] as Rect)
  }
  return down
}

export function gridSize(lines: readonly string[]) {
  const cols = Math.max(0, ...lines.map((l) => Array.from(l).length))
  return { cols, rows: lines.length, width: cols * W, height: lines.length * H }
}

/** A complete SVG document, for the brand renders. */
export function asciiSvg(
  lines: readonly string[],
  { color = '#ededed', pad = 0, background = null as string | null, radius = 0 } = {},
): string {
  const { width, height } = gridSize(lines)
  const vw = width + pad * 2
  const vh = height + pad * 2
  const bg = background ? `<rect width="${vw}" height="${vh}" rx="${radius}" fill="${background}"/>` : ''
  const paths = asciiPaths(lines)
    .map((p) => `<path d="${p.d}" fill="${color}"${p.alpha < 1 ? ` fill-opacity="${p.alpha}"` : ''}/>`)
    .join('')
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${vw} ${vh}" width="${vw}" height="${vh}">${bg}<g transform="translate(${pad} ${pad})">${paths}</g></svg>`
}

/* ── Lettering ───────────────────────────────────────────── */

/** kryo.to's own letterforms (six rows), as the site's footer logo draws them. */
export const GLYPHS: Record<string, string[]> = {
  K: ['██╗  ██╗', '██║ ██╔╝', '█████╔╝ ', '██╔═██╗ ', '██║  ██╗', '╚═╝  ╚═╝'],
  r: ['██████╗ ', '██╔══██╗', '██║  ╚═╝', '██║     ', '██║     ', '╚═╝     '],
  y: ['██╗  ██╗', '╚██╗██╔╝', ' ╚███╔╝ ', '  ██║   ', '  ██║   ', '  ╚═╝   '],
  o: [' ██████╗', '██╔══██║', '██║  ██║', '██║  ██║', '╚█████╔╝', ' ╚════╝ '],
  '.': ['   ', '   ', '   ', '   ', '██╗', '╚═╝'],
  t: ['  ██╗   ', '██████╗ ', '╚═██╔═╝ ', '  ██║   ', '  ██║   ', '  ╚═╝   '],
  '/': ['     ██ ', '    ██  ', '   ██   ', '  ██    ', ' ██     ', '██      '],
  ' ': ['  ', '  ', '  ', '  ', '  ', '  '],
}

/** Set a word in the lettering: six lines. */
export function word(text: string): string[] {
  const rows = ['', '', '', '', '', '']
  for (const ch of text) {
    const g = GLYPHS[ch] ?? GLYPHS[' ']!
    g.forEach((line, i) => (rows[i] += line))
  }
  return rows.map((l) => l.replace(/\s+$/, ''))
}

/** The full wordmark. (The K// mark is the site's own image, traced: see mark.ts.) */
export const WORDMARK = word('Kryo.to')
