<script lang="ts">
  import { open } from "@tauri-apps/plugin-dialog";
  import { toast } from "morphe/menu.svelte";
  import {
    message,
    modsAdd,
    modsInspect,
    modsList,
    modsRemove,
    modsSetEnabled,
    openFolder,
    getSettings,
    type ModInfo,
    type Overview,
  } from "../lib/backend";

  let { ov, selected, onSelect }: { ov: Overview; selected: string; onSelect: (name: string) => void } = $props();

  let mods = $state<ModInfo[]>([]);
  let indexUrl = $state("");
  let removing = $state<ModInfo | null>(null);
  let removeDialog = $state<HTMLDialogElement>();
  let adding = $state<{ path: string; label: string } | null>(null);
  let addDialog = $state<HTMLDialogElement>();

  const running = $derived(ov.running.includes(selected));

  async function load() {
    try {
      mods = await modsList(selected);
    } catch (e) {
      toast(message(e), true);
    }
  }

  $effect(() => {
    selected;
    load();
  });

  $effect(() => {
    getSettings().then((s) => (indexUrl = s.indexUrl));
  });

  async function run(task: () => Promise<unknown>, done?: string) {
    try {
      await task();
      await load();
      if (done) toast(done);
    } catch (e) {
      toast(message(e), true);
    }
  }

  async function pickPackage() {
    const path = await open({ multiple: false, title: "Add a mod", filters: [{ name: "Mod package", extensions: ["bw"] }] });
    if (typeof path !== "string") return;
    try {
      const info = await modsInspect(path);
      adding = { path, label: `${info.name}: ${info.files} files, ${(info.bytes / 1048576).toFixed(1)} MB` };
      addDialog?.showModal();
    } catch (e) {
      toast(message(e), true);
    }
  }

  async function pickFolder() {
    const path = await open({ directory: true, multiple: false, title: "Add a mod folder" });
    if (typeof path === "string") run(() => modsAdd(selected, path), "Mod added.");
  }

  const confirmAdd = () =>
    run(async () => {
      if (adding) await modsAdd(selected, adding.path);
      addDialog?.close();
    }, "Mod added.");

  function askRemove(m: ModInfo) {
    removing = m;
    removeDialog?.showModal();
  }

  const confirmRemove = () =>
    run(async () => {
      if (removing) await modsRemove(selected, removing.folder);
      removeDialog?.close();
    });
</script>

<section class="page">
  <h2>Mods</h2>
  <div class="row">
    <label class="inline">
      <span class="muted small">Profile</span>
      <select value={selected} onchange={(e) => onSelect(e.currentTarget.value)}>
        {#each ov.profiles as p}<option value={p.name}>{p.name}</option>{/each}
      </select>
    </label>
    <button disabled={running} onclick={pickPackage}>Add from .bw file</button>
    <button disabled={running} onclick={pickFolder}>Add from folder</button>
    <button class="quiet outlined" onclick={() => openFolder("mods", selected)}>Open folder</button>
  </div>
  {#if running}<p class="muted">The game is running. Close it to change mods.</p>{/if}

  <ul class="window list">
    {#each mods as m (m.folder)}
      <li>
        <label class="check">
          <input
            type="checkbox"
            checked={m.enabled}
            disabled={running}
            onchange={(e) => run(() => modsSetEnabled(selected, m.folder, e.currentTarget.checked))} />
          <span class="label">{m.name}</span>
        </label>
        {#if m.name !== m.folder}<span class="muted small">{m.folder}</span>{/if}
        {#if !m.hasManifest}<span class="muted small">no manifest</span>{/if}
        <button class="quiet" disabled={running} onclick={() => askRemove(m)}>Remove</button>
      </li>
    {:else}
      <li class="muted">No mods in this profile.</li>
    {/each}
  </ul>

  <h3>Index</h3>
  <p class="muted">{indexUrl ? indexUrl : "No index yet."}</p>
</section>

<dialog bind:this={addDialog} class="window confirm">
  <h2>Add this mod?</h2>
  <p class="muted">{adding?.label}</p>
  <div class="row">
    <button class="primary" onclick={confirmAdd}>Add</button>
    <button class="quiet" onclick={() => addDialog?.close()}>Cancel</button>
  </div>
</dialog>

<dialog bind:this={removeDialog} class="window confirm">
  <h2>Remove {removing?.name}?</h2>
  <p class="muted">The mod's folder is deleted from this profile.</p>
  <div class="row">
    <button class="primary" onclick={confirmRemove}>Remove</button>
    <button class="quiet" onclick={() => removeDialog?.close()}>Cancel</button>
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
  h3 {
    font-size: 1rem;
  }
  .row {
    display: flex;
    gap: 0.5rem;
    flex-wrap: wrap;
    align-items: flex-end;
  }
  .inline {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
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
  .check {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    flex: 1;
    min-width: 8rem;
  }
  .label {
    font-weight: 600;
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
  .confirm .row {
    margin-top: 0.75rem;
  }
</style>
