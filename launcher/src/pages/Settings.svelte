<script lang="ts">
  import { open } from "@tauri-apps/plugin-dialog";
  import ThemePicker from "morphe/ThemePicker.svelte";
  import { toast } from "morphe/menu.svelte";
  import { getSettings, message, openFolder, saveSettings, setRom, type Game, type Overview } from "../lib/backend";
  import Icon from "../lib/Icon.svelte";
  import { autoUpdate, checkForUpdate, setAutoUpdate, updates } from "../lib/updater.svelte";

  let { ov, onRefresh }: { ov: Overview; onRefresh: () => Promise<void> | void } = $props();

  let repo = $state("");
  let indexUrl = $state("");
  let localBuild = $state("");
  let tokenSet = $state(false);
  // Empty field keeps the saved token; "clear" removes it.
  let token = $state("");
  let clearToken = $state(false);
  let auto = $state(autoUpdate());
  let roms = $state<Record<Game, string | null>>({ black: null, white: null });

  $effect(() => {
    getSettings().then((s) => {
      repo = s.repo;
      indexUrl = s.indexUrl;
      localBuild = s.localBuild ?? "";
      tokenSet = s.tokenSet;
      roms = { black: s.romBlack, white: s.romWhite };
    });
  });

  const games: [Game, string][] = [
    ["black", "Pokémon Black"],
    ["white", "Pokémon White"],
  ];

  async function chooseRom(game: Game, label: string) {
    const path = await open({ multiple: false, title: label, filters: [{ name: "DS ROM", extensions: ["nds"] }] });
    if (typeof path === "string") await applyRom(game, path);
  }

  async function applyRom(game: Game, path: string | null) {
    try {
      const s = await setRom(game, path);
      roms = { black: s.romBlack, white: s.romWhite };
      toast(path ? "ROM saved." : "ROM removed.");
    } catch (e) {
      toast(message(e), true);
    }
  }

  const fileName = (p: string) => p.split(/[\\/]/).pop() ?? p;

  async function pickBuild() {
    const path = await open({ directory: true, multiple: false, title: "Folder with an eidos build" });
    if (typeof path === "string") localBuild = path;
  }

  async function save() {
    try {
      const s = await saveSettings({
        repo,
        token: clearToken ? "" : token ? token : null,
        indexUrl,
        localBuild: localBuild || null,
      });
      tokenSet = s.tokenSet;
      token = "";
      clearToken = false;
      await onRefresh();
      toast("Saved.");
    } catch (e) {
      toast(message(e), true);
    }
  }

  async function checkNow() {
    await checkForUpdate(true);
    if (updates.state.kind === "current") toast("The launcher is up to date.");
    else if (updates.state.kind === "failed") toast(updates.state.message, true);
  }
</script>

<section class="page">
  <header class="page-head">
    <h1>Settings</h1>
  </header>

  <div>
    <h2 class="section-label">Appearance</h2>
    <div class="window block">
      <ThemePicker />
    </div>
  </div>

  <div>
    <h2 class="section-label">ROMs</h2>
    <div class="window block">
      {#each games as [game, label] (game)}
        <div class="inline rom">
          <div class="grow">
            <div class="title">{label}</div>
            {#if roms[game]}
              <div class="muted small path" title={roms[game]}>{fileName(roms[game] ?? "")}</div>
            {:else}
              <div class="muted small">Not set</div>
            {/if}
          </div>
          {#if roms[game]}
            <button class="quiet" onclick={() => applyRom(game, null)} title="Remove"><Icon name="trash" size={15} /></button>
          {/if}
          <button onclick={() => chooseRom(game, label)}><Icon name="folder" size={15} />Choose</button>
        </div>
      {/each}
      <span class="muted small">Your own dumps. eidos makes its game data from the Black ROM when it needs to.</span>
    </div>
  </div>

  <div>
    <h2 class="section-label">Updates</h2>
    <div class="window block">
      <div class="inline">
        <div class="grow">
          <div class="title">Launcher {ov.launcherVersion}</div>
          <div class="muted small">
            {#if updates.state.kind === "checking"}Checking for updates
            {:else if updates.state.kind === "current"}Up to date
            {:else if updates.state.kind === "failed"}{updates.state.message}
{/if}
          </div>
        </div>
        <button disabled={updates.state.kind === "checking"} onclick={checkNow}><Icon name="refresh" size={15} />Check now</button>
      </div>
      <label class="inline check">
        <input type="checkbox" bind:checked={auto} onchange={() => setAutoUpdate(auto)} />
        Update on start
      </label>
    </div>
  </div>

  <div>
    <h2 class="section-label">Downloads</h2>
    <div class="window block">
      <label class="field">
        <span>Releases repository</span>
        <input bind:value={repo} placeholder="owner/name" />
      </label>
      <label class="field">
        <span>GitHub token {tokenSet ? "(saved)" : ""}</span>
        <input type="password" bind:value={token} placeholder="Only for a private repository" autocomplete="off" />
        {#if tokenSet}
          <span class="inline check"><input type="checkbox" bind:checked={clearToken} /> Remove the saved token</span>
        {/if}
        <span class="hint">Stored as plain text in the launcher's settings file.</span>
      </label>
      <label class="field">
        <span>Local build folder</span>
        <span class="inline">
          <input class="grow" bind:value={localBuild} placeholder="A folder with eidos.exe or eidos_boot.exe" />
          <button onclick={pickBuild}><Icon name="folder" size={15} />Choose</button>
        </span>
      </label>
      <label class="field">
        <span>Mods index address</span>
        <input bind:value={indexUrl} placeholder="Empty: no index" />
      </label>
      <div class="inline buttons">
        <button class="quiet outlined" onclick={() => openFolder("root")}><Icon name="folder" size={15} />Open the eidos folder</button>
        <div class="grow"></div>
        <button class="primary" onclick={save}><Icon name="check" size={15} />Save</button>
      </div>
    </div>
  </div>
</section>

<style>
  .page {
    max-width: 40rem;
  }
  .block {
    display: flex;
    flex-direction: column;
    gap: 1rem;
    padding: 1.1rem 1.2rem;
  }
  .title {
    font-weight: 600;
  }
  .rom .grow {
    min-width: 0;
  }
  .path {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .check {
    gap: 0.6rem;
    color: var(--ink);
    font-size: 0.9rem;
  }
  .buttons {
    border-top: 1px solid var(--line);
    padding-top: 1rem;
  }
</style>
