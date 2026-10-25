import { mount } from "svelte";
import App from "./App.svelte";
import "morphe/theme.css";
import "@fontsource/marcellus-sc/400.css";
import "./lib/launcher.css";
import { initTheme, themeStorageKey } from "morphe/theme";
import { installMenuFallback } from "morphe/menu.svelte";

themeStorageKey("eidos-launcher.theme");
initTheme();
installMenuFallback();

const target = document.getElementById("app");
if (!target) throw new Error("no #app element");

// The development server outside Tauri draws the pages over sample data
// (lib/mock.ts); the built app never loads it.
const ready =
  import.meta.env.DEV && !("__TAURI_INTERNALS__" in window)
    ? import("./lib/mock").then((m) => m.installMock())
    : Promise.resolve();

ready.then(() => mount(App, { target }));
