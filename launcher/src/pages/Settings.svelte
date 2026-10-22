<script lang="ts">
  import { open } from "@tauri-apps/plugin-dialog";
  import ThemePicker from "morphe/ThemePicker.svelte";
  import { toast } from "morphe/menu.svelte";
  import { getSettings, message, openFolder, saveSettings, type Overview } from "../lib/backend";

  let { ov, onRefresh }: { ov: Overview; onRefresh: () => Promise<void> | void } = $props();

  let repo = $state("");
  let indexUrl = $state("");
  let localBuild = $state("");
  let tokenSet = $state(false);
  // Empty field keeps the saved token; "clear" removes it.
  let token = $state("");
  let clearToken = $state(false);

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
</script>

<section class="page">
  <h2>Settings</h2>

  <div class="window block">
    <ThemePicker />
  </div>

  <div class="window block">
    <label>
      <span class="muted small">Releases repository</span>
      <input bind:value={repo} placeholder="owner/name" />
    </label>
    <label>
      <span class="muted small">GitHub token {tokenSet ? "(saved)" : "(none)"}</span>
      <input type="password" bind:value={token} placeholder="Only for a private repository" autocomplete="off" />
    </label>
    {#if tokenSet}
      <label class="check"><input type="checkbox" bind:checked={clearToken} /> Remove the saved token</label>
    {/if}
    <label>
      <span class="muted small">Local build folder</span>
      <span class="row">
        <input class="grow" bind:value={localBuild} placeholder="A folder with eidos.exe or eidos_boot.exe" />
        <button onclick={pickBuild}>Choose</button>
      </span>
    </label>
    <label>
      <span class="muted small">Mods index address</span>
      <input bind:value={indexUrl} placeholder="Empty: no index" />
    </label>
    <div class="row">
      <button class="primary" onclick={save}>Save</button>
      <button class="quiet" onclick={() => openFolder("root")}>Open the eidos folder</button>
    </div>
  </div>
  <p class="muted small">The token is stored as plain text in the launcher's settings file.</p>
</section>

<style>
  .page {
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
    max-width: 36rem;
  }
  h2,
  p {
    margin: 0;
  }
  .block {
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
    padding: 1rem;
  }
  label {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
  }
  .check {
    flex-direction: row;
    align-items: center;
    gap: 0.5rem;
  }
  .row {
    display: flex;
    gap: 0.5rem;
    flex-direction: row;
  }
  .grow {
    flex: 1;
  }
  .small {
    font-size: 0.85em;
  }
</style>
