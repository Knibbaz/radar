import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import { VitePWA } from 'vite-plugin-pwa'

// base './' so dist/ can be served from any path (python -m http.server in Termux)
export default defineConfig({
  base: './',
  plugins: [
    react(),
    VitePWA({
      registerType: 'autoUpdate',
      manifest: {
        name: 'Capsule Radar',
        short_name: 'Radar',
        description: 'Live ADS-B aircraft radar (airplanes.live)',
        display: 'fullscreen',
        orientation: 'any',
        background_color: '#020403',
        theme_color: '#020403',
        icons: [
          { src: 'icon-192.png', sizes: '192x192', type: 'image/png' },
          { src: 'icon-512.png', sizes: '512x512', type: 'image/png', purpose: 'any maskable' },
        ],
      },
      // precache only the app shell; API requests always go to the network
    }),
  ],
})
