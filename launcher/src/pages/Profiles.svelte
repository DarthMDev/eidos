<script lang="ts">
  import { toast } from "morphe/menu.svelte";
  import {
    LATEST,
    LOCAL,
    createProfile,
    deleteProfile,
    duplicateProfile,
    message,
    openFolder,
    renameProfile,
    setProfileVersion,
    versionLabel,
    type Overview,
  } from "../lib/backend";

  let {
    ov,
    selected,
    onSelect,
    onRefresh,
  }: {
    ov: Overview;
    selected: string;
    onSelect: (name: string) => void;
    onRefresh: () => Promise<void> | void;
  } = $props();

  let newName = $state("");
  let newVersion = $state(LATEST);
  let renaming = $state<string | null>(null);
  let renameTo = $state("");
  let deleting = $state<string | null>(null);
  let deleteDialog = $state<HTMLDialogElement>();

  // Latest, the local build when set, then the installed versions.
  const choices = $derived([LATEST, ...(ov.localBuild ? [LOCAL] : []), ...ov.installed]);

  async function run(task: () => Promise<unknown>, done?: string) {
    try {
      await task();
      await onRefresh();
      if (done) toast(done);
    } catch (e) {
      toast(message(e), true);
    }
  }

  const create = () =>
    run(async () => {
      const p = await createProfile(newName, newVersion);
      newName = "";
      onSelect(p.name);
    }, "Profile created.");

  const rename = (from: string) =>
    run(async () => {
      await renameProfile(from, renameTo);
      if (selected === from) onSelect(renameTo.trim());
      renaming = null;
    });

  const duplicate = (name: string) => run(() => duplicateProfile(name), "Profile copied.");

  function askDelete(name: string) {
    deleting = name;
    deleteDialog?.showModal();
  }

  const confirmDelete = () =>
    run(async () => {
      if (deleting) await deleteProfile(deleting);
      deleting = null;
      deleteDialog?.close();
    });
</script>

<section class="page">
  <h2>Profiles</h2>
  <p class="muted">
    A profile has its own eidos version, mods, saves and settings.
  </p>

  <ul class="window list">
    {#each ov.profiles as p (p.name)}
      <li>
        {#if renaming === p.name}
          <input bind:value={renameTo} onkeydown={(e) => e.key === "Enter" && rename(p.name)} />
          <button onclick={() => rename(p.name)}>Save</button>
          <button class="quiet" onclick={() => (renaming = null)}>Cancel</button>
        {:else}
          <button class="row-item name" aria-current={selected === p.name} onclick={() => onSelect(p.name)}>
            <span class="label">{p.name}</span>
            {#if ov.running.includes(p.name)}<span class="ok small">running</span>{/if}
          </button>
          <select
            value={p.version}
            aria-label="Version of {p.name}"
            onchange={(e) => run(() => setProfileVersion(p.name, e.currentTarget.value))}>
            {#each choices as c}<option value={c}>{versionLabel(c)}</option>{/each}
            {#if !choices.includes(p.version)}<option value={p.version}>{p.version} (not installed)</option>{/if}
          </select>
          <button class="quiet" onclick={() => openFolder("profile", p.name)}>Folder</button>
          <button class="quiet" onclick={() => duplicate(p.name)}>Copy</button>
          {#if p.name !== "Default"}
            <button class="quiet" onclick={() => ((renaming = p.name), (renameTo = p.name))}>Rename</button>
            <button class="quiet" onclick={() => askDelete(p.name)}>Delete</button>
          {/if}
        {/if}
      </li>
    {/each}
  </ul>

  <h3>New profile</h3>
  <div class="row">
    <input placeholder="Name" bind:value={newName} onkeydown={(e) => e.key === "Enter" && create()} />
    <select bind:value={newVersion} aria-label="Version">
      {#each choices as c}<option value={c}>{versionLabel(c)}</option>{/each}
    </select>
    <button class="primary" disabled={!newName.trim()} onclick={create}>Create</button>
  </div>
</section>

<dialog bind:this={deleteDialog} class="window confirm" onclose={() => (deleting = null)}>
  <h2>Delete {deleting}?</h2>
  <p class="muted">Its mods, saves and settings are deleted with it. This cannot be undone.</p>
  <div class="row">
    <button class="primary" onclick={confirmDelete}>Delete</button>
    <button class="quiet" onclick={() => deleteDialog?.close()}>Cancel</button>
  </div>
</dialog>

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
  .list {
    list-style: none;
    margin: 0;
    padding: 0.5rem;
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
  }
  .list li {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    flex-wrap: wrap;
  }
  .name {
    flex: 1;
    min-width: 8rem;
  }
  .row {
    display: flex;
    gap: 0.5rem;
    flex-wrap: wrap;
  }
  .small {
    font-size: 0.85em;
  }
  .confirm {
    color: var(--ink);
    padding: 1.25rem;
    max-width: 26rem;
  }
  .confirm::backdrop {
    background: rgba(0, 0, 0, 0.5);
  }
  .confirm h2 {
    margin-bottom: 0.5rem;
  }
</style>
