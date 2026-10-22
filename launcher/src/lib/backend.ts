import { invoke } from "@tauri-apps/api/core";

export type Profile = { name: string; version: string };

export type Overview = {
  profiles: Profile[];
  installed: string[];
  localBuild: string | null;
  localOk: boolean;
  running: string[];
  launcherVersion: string;
  root: string;
};

export type Remote = {
  tag: string;
  id: string | null;
  name: string;
  prerelease: boolean;
  published: string | null;
  size: number | null;
  available: boolean;
  installed: boolean;
};

export type ModInfo = {
  folder: string;
  name: string;
  order: number;
  enabled: boolean;
  hasManifest: boolean;
};

export type PackageInfo = { name: string; files: number; bytes: number };

export type SettingsView = {
  repo: string;
  tokenSet: boolean;
  indexUrl: string;
  localBuild: string | null;
};

export const LATEST = "latest";
export const LOCAL = "local";

export const overview = () => invoke<Overview>("overview");
export const createProfile = (name: string, version: string) => invoke<Profile>("create_profile", { name, version });
export const renameProfile = (from: string, to: string) => invoke<void>("rename_profile", { from, to });
export const duplicateProfile = (name: string) => invoke<Profile>("duplicate_profile", { name });
export const deleteProfile = (name: string) => invoke<void>("delete_profile", { name });
export const setProfileVersion = (name: string, version: string) =>
  invoke<void>("set_profile_version", { name, version });
export const listRemote = () => invoke<Remote[]>("list_remote");
export const installVersion = (tag: string) => invoke<void>("install_version", { tag });
export const removeVersion = (id: string) => invoke<void>("remove_version", { id });
export const play = (name: string, join?: string) => invoke<{ version: string }>("play", { name, join: join ?? null });
export const modsList = (profile: string) => invoke<ModInfo[]>("mods_list", { profile });
export const modsSetEnabled = (profile: string, folder: string, enabled: boolean) =>
  invoke<void>("mods_set_enabled", { profile, folder, enabled });
export const modsRemove = (profile: string, folder: string) => invoke<void>("mods_remove", { profile, folder });
export const modsInspect = (path: string) => invoke<PackageInfo>("mods_inspect", { path });
export const modsAdd = (profile: string, path: string) => invoke<string>("mods_add", { profile, path });
export const openFolder = (what: "root" | "profile" | "mods", profile?: string) =>
  invoke<void>("open_folder", { what, profile: profile ?? null });
export const getSettings = () => invoke<SettingsView>("get_settings");
export const saveSettings = (s: { repo: string; token: string | null; indexUrl: string; localBuild: string | null }) =>
  invoke<SettingsView>("save_settings", s);
export const takePendingJoin = () => invoke<string | null>("take_pending_join");

/** The text of a failed command. */
export const message = (e: unknown) => (typeof e === "string" ? e : e instanceof Error ? e.message : String(e));

/** "Latest" and "Local build" for the version pickers. */
export function versionLabel(v: string): string {
  return v === LATEST ? "Latest" : v === LOCAL ? "Local build" : v;
}
