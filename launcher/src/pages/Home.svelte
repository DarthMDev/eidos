<script lang="ts">
  import { toast } from "morphe/menu.svelte";
  import { LATEST, LOCAL, message, play, versionLabel, type Overview, type Profile } from "../lib/backend";
  import Icon from "../lib/Icon.svelte";
  import dragon from "../assets/dragon.png";

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
    onGoto: (page: "versions" | "profiles") => void;
  } = $props();

  let error = $state<string | null>(null);
  let starting = $state(false);

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
    starting = true;
    try {
      await play(selected);
      await onRefresh();
    } catch (e) {
      error = message(e);
      toast(error, true);
    } finally {
      starting = false;
    }
  }
</script>

<section class="home">
  <div class="hero card">
    <div class="art" style:background-image="url({dragon})"></div>
    <div class="intro">
      <span class="display title">EIDOS</span>
      <span class="tagline">A native PC port of Pokémon Black and White</span>
    </div>

    <div class="launch">
      <label class="field profile">
        <span>Profile</span>
        <select value={selected} onchange={(e) => onSelect(e.currentTarget.value)}>
          {#each ov.profiles as p}<option value={p.name}>{p.name}</option>{/each}
        </select>
      </label>
      <div class="which">
        {#if version}
          <span class="badge accent">eidos {versionLabel(version)}</span>
          {#if current?.version === LATEST}<span class="faint small">follows the newest version</span>{/if}
        {:else}
          <span class="badge bad">No version</span>
        {/if}
      </div>
      <button class="primary play" disabled={!version || running || starting} onclick={start}>
        <Icon name="play" size={20} />
        {running ? "Running" : starting ? "Starting" : "Play"}
      </button>
    </div>
  </div>

  {#if running}
    <p class="note">{selected} is running. Close the game to play another profile or change its mods.</p>
  {:else if !version}
    <p class="note">
      {#if current && current.version !== LATEST && current.version !== LOCAL}
        Version {current.version} is not installed.
      {:else}
        No eidos version is installed yet.
      {/if}
      <button class="quiet outlined" onclick={() => onGoto("versions")}><Icon name="download" size={16} />Install one</button>
    </p>
  {/if}
  {#if error}<p class="note bad" role="alert">{error}</p>{/if}

  <div class="tiles">
    <button class="tile card" onclick={() => onGoto("profiles")}>
      <span class="section-label">Profiles</span>
      <span class="big">{ov.profiles.length}</span>
      <span class="faint small">Each has its own saves, mods and version</span>
    </button>
    <button class="tile card" onclick={() => onGoto("versions")}>
      <span class="section-label">Installed versions</span>
      <span class="big">{ov.installed.length}</span>
      <span class="faint small">{ov.installed[0] ? `Newest: ${ov.installed[0]}` : "None yet"}</span>
    </button>
  </div>
</section>

<style>
  .home {
    display: flex;
    flex-direction: column;
    gap: 1rem;
    max-width: 58rem;
  }
  .hero {
    position: relative;
    overflow: hidden;
    min-height: 22.5rem;
    display: grid;
    grid-template-rows: 1fr auto;
    background: linear-gradient(160deg, var(--hero-a), var(--hero-b));
  }
  .art {
    position: absolute;
    right: 2rem;
    top: 1.25rem;
    width: 290px;
    height: 238px;
    background-repeat: no-repeat;
    background-size: 290px 238px;
    image-rendering: pixelated;
  }
  .intro {
    position: relative;
    padding: 2.25rem 2rem 0;
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
  }
  .title {
    font-size: 3.2rem;
    letter-spacing: 0.35em;
    line-height: 1;
  }
  .tagline {
    color: var(--muted);
    letter-spacing: 0.08em;
    font-size: 0.85rem;
    text-transform: uppercase;
  }
  .launch {
    position: relative;
    display: flex;
    align-items: flex-end;
    gap: 1rem;
    padding: 1.25rem 1.5rem;
    background: rgba(0, 0, 0, 0.22);
    border-top: 1px solid var(--line);
  }
  .profile {
    width: 13rem;
  }
  .which {
    flex: 1;
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
    align-items: flex-start;
    padding-bottom: 0.3rem;
  }
  .play {
    font-size: 1.15rem;
    padding: 0.85rem 2.6rem;
    letter-spacing: 0.06em;
    border-radius: 11px;
  }
  .note {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    color: var(--muted);
    margin: 0;
  }
  .tiles {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(14rem, 1fr));
    gap: 1rem;
  }
  .tile {
    flex-direction: column;
    align-items: flex-start;
    gap: 0.15rem;
    padding: 1rem 1.2rem;
    text-align: left;
    font-weight: 400;
    background: var(--panel);
    border: 1px solid var(--line);
  }
  .tile .section-label {
    margin: 0;
  }
  .big {
    font-size: 1.6rem;
    font-weight: 700;
  }
</style>
