<script lang="ts">
  import { onMount } from "svelte";
  import { listen } from "@tauri-apps/api/event";
  import { toast } from "morphe/menu.svelte";
  import { installVersion, listRemote, message, removeVersion, type Overview, type Remote } from "../lib/backend";

  let { ov, onRefresh }: { ov: Overview; onRefresh: () => Promise<void> | void } = $props();

  let remote = $state<Remote[] | null>(null);
  let error = $state<string | null>(null);
  let loading = $state(false);
  let busy = $state<string | null>(null);
  let stage = $state("");

  async function load() {
    loading = true;
    error = null;
    try {
      remote = await listRemote();
    } catch (e) {
      error = message(e);
    } finally {
      loading = false;
    }
  }

  async function install(tag: string) {
    busy = tag;
    try {
      await installVersion(tag);
      toast("Installed.");
      await onRefresh();
      await load();
    } catch (e) {
      toast(message(e), true);
    } finally {
      busy = null;
      stage = "";
    }
  }

  async function remove(id: string) {
    try {
      await removeVersion(id);
      await onRefresh();
      await load();
    } catch (e) {
      toast(message(e), true);
    }
  }

  const size = (n: number | null) => (n == null ? "" : `${(n / 1048576).toFixed(1)} MB`);

  onMount(() => {
    load();
    const stop = listen<[string, string]>("install-progress", (e) => (stage = e.payload[1]));
    return () => {
      stop.then((f) => f());
    };
  });
</script>

<section class="page">
  <h2>Versions</h2>

  <h3>Installed</h3>
  <ul class="window list">
    {#if ov.localBuild}
      <li>
        <span class="label">Local build</span>
        <span class="muted path">{ov.localBuild}</span>
        {#if !ov.localOk}<span class="bad">no eidos program in this folder</span>{/if}
      </li>
    {/if}
    {#each ov.installed as id (id)}
      <li>
        <span class="label">{id}</span>
        <button class="quiet" onclick={() => remove(id)}>Remove</button>
      </li>
    {/each}
    {#if !ov.localBuild && !ov.installed.length}<li class="muted">None.</li>{/if}
  </ul>

  <h3>
    Releases
    <button class="quiet" disabled={loading} onclick={load}>{loading ? "Loading..." : "Refresh"}</button>
  </h3>
  {#if error}
    <p class="bad" role="alert">{error}</p>
  {:else if remote && !remote.length}
    <p class="muted">The repository has no releases.</p>
  {:else if remote}
    <ul class="window list">
      {#each remote as r (r.tag)}
        <li>
          <span class="label">{r.name}</span>
          {#if r.prerelease}<span class="muted small">pre-release</span>{/if}
          <span class="muted small">{size(r.size)}</span>
          {#if r.installed}
            <span class="ok small">installed</span>
          {:else if !r.available}
            <span class="muted small">no file for this system</span>
          {:else}
            <button disabled={busy !== null} onclick={() => install(r.tag)}>
              {busy === r.tag ? stage || "Working..." : "Install"}
            </button>
          {/if}
        </li>
      {/each}
    </ul>
  {/if}
</section>

<style>
  .page {
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
  }
  h2,
  h3,
  p {
    margin: 0;
  }
  h3 {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    font-size: 1rem;
  }
  .list {
    list-style: none;
    margin: 0;
    padding: 0.5rem 0.75rem;
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }
  .list li {
    display: flex;
    align-items: center;
    gap: 0.6rem;
    flex-wrap: wrap;
  }
  .label {
    flex: 1;
    min-width: 6rem;
    font-weight: 600;
  }
  .path {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    max-width: 24rem;
  }
  .small {
    font-size: 0.85em;
  }
</style>
