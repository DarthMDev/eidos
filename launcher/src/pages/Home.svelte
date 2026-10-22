<script lang="ts">
  import { toast } from "morphe/menu.svelte";
  import { LATEST, LOCAL, message, play, versionLabel, type Overview, type Profile } from "../lib/backend";

  let {
    ov,
    selected,
    current,
    onSelect,
    onRefresh,
    onGoto,
  }: {
    ov: Overview;
    selected: string;
    current: Profile | undefined;
    onSelect: (name: string) => void;
    onRefresh: () => void;
    onGoto: (page: "versions") => void;
  } = $props();

  let error = $state<string | null>(null);

  // The version a Play press runs, as the backend resolves it.
  const version = $derived.by(() => {
    if (!current) return null;
    if (current.version === LATEST) return ov.installed[0] ?? (ov.localOk ? LOCAL : null);
    if (current.version === LOCAL) return ov.localOk ? LOCAL : null;
    return ov.installed.includes(current.version) ? current.version : null;
  });
  const running = $derived(ov.running.includes(selected));

  async function start() {
    error = null;
    try {
      await play(selected);
      await onRefresh();
    } catch (e) {
      error = message(e);
      toast(error, true);
    }
  }
</script>

<section class="home">
  <label class="pick">
    <span class="muted small">Profile</span>
    <select value={selected} onchange={(e) => onSelect(e.currentTarget.value)}>
      {#each ov.profiles as p}<option value={p.name}>{p.name}</option>{/each}
    </select>
  </label>

  <div class="window card">
    <p class="version">
      {#if version}
        eidos {versionLabel(version)}
      {:else}
        <span class="bad">No version to run</span>
      {/if}
    </p>
    <button class="primary play" disabled={!version || running} onclick={start}>
      {running ? "Running" : "Play"}
    </button>
    {#if running}
      <p class="muted">{selected} is running.</p>
    {:else if !version}
      <p class="muted">
        {#if current && current.version !== LATEST && current.version !== LOCAL}
          Version {current.version} is not installed.
        {:else}
          No eidos version is installed.
        {/if}
        <button class="quiet" onclick={() => onGoto("versions")}>Open Versions</button>
      </p>
    {/if}
    {#if error}<p class="bad" role="alert">{error}</p>{/if}
  </div>
</section>

<style>
  .home {
    display: flex;
    flex-direction: column;
    gap: 1rem;
    max-width: 34rem;
  }
  .pick {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
    max-width: 18rem;
  }
  .card {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 1rem;
    padding: 2.5rem 1.5rem;
  }
  .version {
    margin: 0;
    font-size: 1.1rem;
  }
  .play {
    font-size: 1.6rem;
    padding: 0.8rem 4rem;
  }
  .small {
    font-size: 0.85em;
  }
</style>
