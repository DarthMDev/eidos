import { defineConfig } from "vite";
import { svelte } from "@sveltejs/vite-plugin-svelte";

// The Tauri shell (src-tauri/tauri.conf.json) loads dist/.
export default defineConfig({
  plugins: [svelte()],
  clearScreen: false,
  // morphe ships .svelte.ts sources; the dev server's pre-bundling cannot parse them.
  optimizeDeps: { exclude: ["morphe"] },
  server: {
    port: 1430,
    strictPort: true,
    watch: { ignored: ["**/src-tauri/**"] },
  },
  build: {
    outDir: "dist",
    emptyOutDir: true,
    target: "es2022",
  },
});
