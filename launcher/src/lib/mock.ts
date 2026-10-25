// A stand-in for the Tauri backend, for designing the pages in a plain
// browser (`npm run dev`, then open http://localhost:1430/?page=versions).
// Loaded only by the development server outside Tauri (main.ts); the built
// app never contains it.

const overview = {
  profiles: [
    { name: "Default", version: "latest" },
    { name: "Nuzlocke", version: "0.1.4" },
    { name: "Randomizer", version: "local" },
  ],
  installed: ["0.1.5", "0.1.4"],
  localBuild: "C:\\eidos-game",
  localOk: true,
  running: [] as string[],
  launcherVersion: "0.1.6",
  root: "C:\\Users\\player\\AppData\\Roaming\\eidos",
};

const remote = [
  { tag: "v0.1.5", id: "0.1.5", name: "eidos 0.1.5", prerelease: false, published: "2026-10-23T13:07:48Z", size: 7400000, available: true, installed: true },
  { tag: "v0.1.4", id: "0.1.4", name: "eidos 0.1.4", prerelease: false, published: "2026-10-23T12:28:22Z", size: 7390000, available: true, installed: true },
  { tag: "v0.1.3", id: "0.1.3", name: "eidos 0.1.3", prerelease: false, published: "2026-10-23T11:50:44Z", size: 7380000, available: true, installed: false },
  { tag: "v0.1.2", id: "0.1.2", name: "eidos 0.1.2", prerelease: false, published: "2026-10-23T10:25:16Z", size: 7350000, available: true, installed: false },
  { tag: "v0.1.1", id: "0.1.1", name: "eidos 0.1.1", prerelease: false, published: "2026-10-23T02:05:31Z", size: 7330000, available: true, installed: false },
];

const mods = [
  { folder: "hd-sprites", name: "HD Sprites", order: 0, enabled: true, hasManifest: true },
  { folder: "fast-bike", name: "Fast Bike", order: 1, enabled: false, hasManifest: true },
  { folder: "my-test", name: "my-test", order: 2, enabled: true, hasManifest: false },
];

const settings = { repo: "Project-Holon/eidos", tokenSet: false, indexUrl: "", localBuild: "C:\\eidos-game" };

const answers: Record<string, unknown> = {
  overview,
  list_remote: remote,
  mods_list: mods,
  get_settings: settings,
  save_settings: settings,
  take_pending_join: null,
};

let next = 1;

export function installMock() {
  const w = window as unknown as Record<string, unknown>;
  w.__TAURI_INTERNALS__ = {
    invoke: async (cmd: string) => {
      if (cmd.startsWith("plugin:event|")) return next++;
      if (cmd.startsWith("plugin:updater|")) return null;
      if (cmd in answers) return structuredClone(answers[cmd]);
      return null;
    },
    transformCallback: () => next++,
    unregisterCallback: () => {},
    metadata: { currentWindow: { label: "main" }, currentWebview: { label: "main" } },
  };
  w.__TAURI_EVENT_PLUGIN_INTERNALS__ = { unregisterListener: () => {} };
}
