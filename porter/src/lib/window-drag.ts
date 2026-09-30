import { getCurrentWindow } from '@tauri-apps/api/window'
import { isTauri } from '@/lib/porter'

/**
 * Moving the frameless window by its `.drag` areas.
 *
 * Not `-webkit-app-region: drag`: in WebView2 a control inside such an area
 * never gets its cursor. A press on a `.drag` area that is not on a control
 * arms a move, which starts once the pointer actually moves a few pixels.
 * Starting it on the press itself (as before) handed the pointer to the
 * system's move loop straight away, which on Windows swallowed the second
 * click of a double-click, so double-click to maximize rarely worked, and a
 * plain click on the bar could leave the page thinking the button was still
 * down. A double press on an area marked `data-maximize` maximizes or
 * restores, as a real title bar does.
 */
const CONTROL = '.drag, .no-drag, button, a[href], input, textarea, select, [role="button"], [contenteditable="true"]'
/** How far the pointer moves with the button down before the window follows. */
const SLOP = 4

export function installWindowDrag() {
  if (!isTauri()) return
  let armed: { x: number; y: number } | null = null

  window.addEventListener('mousedown', (e) => {
    armed = null
    if (e.button !== 0 || !(e.target instanceof Element)) return
    const zone = e.target.closest(CONTROL)
    if (!zone?.classList.contains('drag')) return
    e.preventDefault()
    if (e.detail === 2) {
      if (zone.closest('[data-maximize]')) void getCurrentWindow().toggleMaximize()
      return
    }
    armed = { x: e.screenX, y: e.screenY }
  })
  window.addEventListener('mousemove', (e) => {
    if (!armed) return
    if (!(e.buttons & 1)) {
      armed = null
      return
    }
    if (Math.abs(e.screenX - armed.x) < SLOP && Math.abs(e.screenY - armed.y) < SLOP) return
    armed = null
    void getCurrentWindow().startDragging()
  })
  window.addEventListener('mouseup', () => (armed = null))
  window.addEventListener('blur', () => (armed = null))
}
