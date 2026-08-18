import { defineConfig } from 'vite'

// Dashboard estático (HTML + JS vanilla). Vite procesa index.html,
// reemplaza %VITE_*% con las variables de .env y genera dist/.
export default defineConfig({
  // './' permite publicar en GitHub Pages con cualquier nombre de repo
  base: './',
  build: {
    outDir: 'dist',
    sourcemap: false
  }
})
