import { defineConfig, loadEnv } from 'vite';
import preact from '@preact/preset-vite';
import tailwindcss from '@tailwindcss/vite';

// Device endpoints proxied in `npm run dev` (set DEVICE_URL in .env.local).
// `npm run dev:mock` answers everything from src/lib/mock.ts instead.
const DEVICE_PATHS = '^/api/';

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, '.', '');
  const target = env.DEVICE_URL;
  return {
    // Relative asset paths: the bundle is served from the device web root.
    base: './',
    plugins: [preact(), tailwindcss()],
    build:
      mode === 'firmware'
        ? {
            // Embedded in the firmware: fixed names (no hashes), so the C asset
            // table stays stable; firmware/tools/embed_web.py gzips the output.
            target: 'es2019',
            outDir: 'dist-firmware',
            assetsInlineLimit: 4096,
            rollupOptions: {
              output: {
                entryFileNames: 'assets/app.js',
                chunkFileNames: 'assets/[name].js',
                assetFileNames: 'assets/[name][extname]',
              },
            },
          }
        : { target: 'es2019', assetsInlineLimit: 4096, reportCompressedSize: true },
    server: target && mode !== 'mock'
      ? { proxy: { [DEVICE_PATHS]: { target, changeOrigin: true } } }
      : undefined,
  };
});
