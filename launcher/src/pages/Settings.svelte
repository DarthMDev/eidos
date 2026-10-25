<script lang="ts">
  import { open } from "@tauri-apps/plugin-dialog";
  import ThemePicker from "morphe/ThemePicker.svelte";
  import { toast } from "morphe/menu.svelte";
  import { getSettings, message, openFolder, saveSettings, type Overview } from "../lib/backend";
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

  $effect(() => {
    getSettings().then((s) => {
      repo = s.repo;
      indexUrl = s.indexUrl;
      localBuild = s.localBuild ?? "";
      tokenSet = s.tokenSet;
    });
  });

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
    <p>The launcher's own settings. The game's settings are inside the game.</p>
  </header>

  <div>
    <h2 class="section-label">Appearance</h2>
    <div class="card block">
      <ThemePicker />
    </div>
  </div>

  <div>
    <h2 class="section-label">Launcher updates</h2>
    <div class="card block">
      <div class="inline">
        <div class="grow">
          <div class="title">Launcher {ov.launcherVersion}</div>
          <div class="muted small">
            {#if updates.state.kind === "checking"}Checking for updates
            {:else if updates.state.kind === "current"}Up to date
            {:else if updates.state.kind === "failed"}{updates.state.message}
            {:else}Updates are checked each time the launcher starts{/if}
          </div>
        </div>
        <button disabled={updates.state.kind === "checking"} onclick={checkNow}><Icon name="refresh" size={15} />Check now</button>
      </div>
      <label class="inline check">
        <input type="checkbox" bind:checked={auto} onchange={() => setAutoUpdate(auto)} />
        Update the launcher by itself when it starts
      </label>
    </div>
  </div>

  <div>
    <h2 class="section-label">Game files</h2>
    <div class="card block">
      <label class="field">
        <span>Releases repository</span>
        <input bind:value={repo} placeholder="owner/name" />
        <span class="hint">Where versions are downloaded from.</span>
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
