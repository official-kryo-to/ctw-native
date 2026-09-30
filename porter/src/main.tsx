import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { App } from '@/App'
import { installWindowDrag } from '@/lib/window-drag'
import '@/styles.css'

installWindowDrag()
// A file dropped outside the drop handling would otherwise be opened by the web view itself.
window.addEventListener('dragover', (e) => e.preventDefault())
window.addEventListener('drop', (e) => e.preventDefault())

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App />
  </StrictMode>,
)
