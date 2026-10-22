<script lang="ts">
  import { onMount } from "svelte";
  import { listen } from "@tauri-apps/api/event";
  import { toast } from "morphe/menu.svelte";
  import {
    message,
    overview,
    play,
    takePendingJoin,
    versionLabel,
    type Overview,
  } from "./lib/backend";
  import Home from "./pages/Home.svelte";
  import Profiles from "./pages/Profiles.svelte";
  import Versions from "./pages/Versions.svelte";
  import Mods from "./pages/Mods.svelte";
  import Settings from "./pages/Settings.svelte";

  type Page = "home" | "profiles" | "versions" | "mods" | "settings";
  const PAGES: [Page, string][] = [
    ["home", "Play"],
    ["profiles", "Profiles"],
    ["versions", "Versions"],
    ["mods", "Mods"],
    ["settings", "Settings"],
  ];
  const KEY = "eidos-launcher.profile";

  let ov = $state<Overview | null>(null);
  let failure = $state<string | null>(null);
  let page = $state<Page>("home");
  let selected = $state(read());
  let inviteCode = $state<string | null>(null);
  let inviteProfile = $state("");
  let inviteDialog = $state<HTMLDialogElement>();

  function read(): string {
    try {
      return localStorage.getItem(KEY) ?? "Default";
    } catch {
      return "Default";
    }
  }

  function select(name: string) {
    selected = name;
    try {
      localStorage.setItem(KEY, name);
    } catch {
      /* the choice is only remembered when storage works */
    }
  }

  async function refresh() {
    try {
      ov = await overview();
      if (!ov.profiles.some((p) => p.name === selected)) select("Default");
    } catch (e) {
      failure = message(e);
    }
  }

  function showInvite(code: string) {
    inviteCode = code;
    inviteProfile = selected;
    inviteDialog?.showModal();
  }

  async function join() {
    if (!inviteCode) return;
    try {
      await play(inviteProfile, inviteCode);
      inviteDialog?.close();
      await refresh();
    } catch (e) {
      toast(message(e), true);
    }
  }

  onMount(() => {
    refresh();
    const stops: Promise<() => void>[] = [
      listen("running-changed", refresh),
      listen<string>("invite", async (e) => {
        await takePendingJoin();
        await refresh();
        showInvite(e.payload);
      }),
    ];
    takePendingJoin().then((code) => code && showInvite(code));
    return () => stops.forEach((s) => s.then((stop) => stop()));
  });

  const current = $derived(ov?.profiles.find((p) => p.name === selected));
</script>

{#if failure}
  <div class="state bad" role="alert">{failure}</div>
{:else if !ov}
  <div class="state muted">Loading...</div>
{:else}
  <div class="shell">
    <nav class="window" aria-label="Pages">
      <h1>eidos</h1>
      {#each PAGES as [id, label]}
        <button
          class="row-item"
          aria-current={page === id}
          onclick={() => (page = id)}>{label}</button>
      {/each}
      <p class="muted small ver">Launcher {ov.launcherVersion}</p>
    </nav>
    <main>
      {#if page === "home"}
        <Home {ov} {selected} {current} onSelect={select} onRefresh={refresh} onGoto={(p: Page) => (page = p)} />
      {:else if page === "profiles"}
        <Profiles {ov} {selected} onSelect={select} onRefresh={refresh} />
      {:else if page === "versions"}
        <Versions {ov} onRefresh={refresh} />
      {:else if page === "mods"}
        <Mods {ov} {selected} onSelect={select} />
      {:else}
        <Settings {ov} onRefresh={refresh} />
      {/if}
    </main>
  </div>
{/if}

<dialog bind:this={inviteDialog} class="window invite">
  {#if ov && inviteCode}
    <h2>Join {inviteCode}</h2>
    <label>
      <span class="muted small">Profile</span>
      <select bind:value={inviteProfile}>
        {#each ov.profiles as p}<option value={p.name}>{p.name} ({versionLabel(p.version)})</option>{/each}
      </select>
    </label>
    <p class="muted small">
      Starts eidos with this profile and joins the game. The game and the profile must run the same version.
    </p>
    <div class="actions">
      <button class="primary" disabled={ov.running.includes(inviteProfile)} onclick={join}>Join</button>
      <button class="quiet" onclick={() => inviteDialog?.close()}>Cancel</button>
    </div>
  {/if}
</dialog>

<style>
  .state {
    display: grid;
    place-items: center;
    height: 100%;
  }
  .shell {
    display: grid;
    grid-template-columns: 11rem 1fr;
    gap: 1rem;
    height: 100%;
    padding: 1rem;
  }
  nav {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
    padding: 1rem 0.75rem;
  }
  nav h1 {
    font-size: 1.4rem;
    margin: 0 0 0.75rem 0.4rem;
  }
  .ver {
    margin: auto 0 0 0.4rem;
  }
  main {
    min-width: 0;
    overflow-y: auto;
    overflow-x: hidden;
    padding: 0.25rem 0.5rem;
  }
  .invite {
    color: var(--ink);
    padding: 1.25rem;
    max-width: 26rem;
  }
  .invite::backdrop {
    background: rgba(0, 0, 0, 0.5);
  }
  .invite h2 {
    margin-top: 0;
  }
  .invite label {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
  }
  .actions {
    display: flex;
    gap: 0.5rem;
  }
  .small {
    font-size: 0.85em;
  }
</style>
