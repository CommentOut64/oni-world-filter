import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const root = new URL("../../", import.meta.url);
const readText = (path) => readFileSync(new URL(path, root), "utf8");

test("desktop release version has a single source synchronized to package metadata", () => {
  const releaseVersion = readText("VERSION").trim();
  const tauriCargo = readText("src-tauri/Cargo.toml");
  const tauriConfig = JSON.parse(readText("src-tauri/tauri.conf.json"));
  const desktopPackage = JSON.parse(readText("desktop/package.json"));
  const cargoVersion = tauriCargo.match(/^version\s*=\s*"([^"]+)"/m)?.[1];

  assert.equal(releaseVersion, "0.0.1");
  assert.equal(cargoVersion, releaseVersion);
  assert.equal(tauriConfig.version, releaseVersion);
  assert.equal(desktopPackage.version, releaseVersion);
});

test("desktop package identity is oni-world-filter everywhere", () => {
  const tauriCargo = readText("src-tauri/Cargo.toml");
  const tauriLock = readText("src-tauri/Cargo.lock");
  const tauriConfig = JSON.parse(readText("src-tauri/tauri.conf.json"));
  const desktopPackage = JSON.parse(readText("desktop/package.json"));
  const indexHtml = readText("desktop/index.html");
  const buildScript = readText("scripts/build-desktop.ps1");

  assert.match(tauriCargo, /^name\s*=\s*"oni-world-filter"/m);
  assert.match(tauriLock, /^name = "oni-world-filter"/m);
  assert.equal(tauriConfig.productName, "oni-world-filter");
  assert.equal(tauriConfig.identifier, "com.wgh.oni-world-filter");
  assert.equal(tauriConfig.app.windows[0].title, "oni-world-filter");
  assert.equal(desktopPackage.name, "oni-world-filter");
  assert.match(indexHtml, /<title>oni-world-filter<\/title>/);
  assert.match(buildScript, /oni-world-filter-\$Version-\$Variant-nsis/);
});

test("desktop build script synchronizes package versions before validation", () => {
  const bootstrap = readText("scripts/lib/desktop-bootstrap.ps1");
  const buildScript = readText("scripts/build-desktop.ps1");

  assert.match(bootstrap, /function Get-DesktopVersion\b/);
  assert.match(bootstrap, /function Sync-DesktopVersion\b/);
  assert.match(buildScript, /Sync-DesktopVersion -RepoRoot \$repoRoot/);
  assert.ok(buildScript.indexOf("Sync-DesktopVersion -RepoRoot $repoRoot") < buildScript.indexOf("Assert-VersionConsistency -RepoRoot $repoRoot"));
});

test("nsis installer uses Chinese language and sidecar cleanup hook", () => {
  const tauriConfig = JSON.parse(readText("src-tauri/tauri.conf.json"));
  const nsis = tauriConfig.bundle?.windows?.nsis;
  const hooks = readText("src-tauri/installer/nsis-hooks.nsh");

  assert.deepEqual(nsis?.languages, ["SimpChinese"]);
  assert.equal(nsis?.installerHooks, "installer/nsis-hooks.nsh");
  assert.equal(nsis?.installMode, "both");
  assert.match(hooks, /!macro NSIS_HOOK_POSTUNINSTALL/);
  assert.match(hooks, /\$LOCALAPPDATA\\\$\{BUNDLEID\}\\sidecars/);
  assert.match(hooks, /RMDir \/r/);
});

test("host debug no longer logs forwarded stdout events", () => {
  const sidecar = readText("src-tauri/src/sidecar.rs");

  assert.doesNotMatch(sidecar, /forwarded stdout event/);
});

test("sidecar settings asset must be resolved from runtime directory", () => {
  const configTemplate = readText("src/config.h.in");
  const sidecarEntry = readText("src/entry_sidecar.cpp");
  const tauriConfig = JSON.parse(readText("src-tauri/tauri.conf.json"));

  assert.match(configTemplate, /SETTING_ASSET_FILENAME "data\.zip"/);
  assert.match(sidecarEntry, /ResolveSettingsAssetPath/);
  assert.doesNotMatch(sidecarEntry, /SETTING_ASSET_FILEPATH/);
  assert.ok(tauriConfig.bundle?.resources?.includes("binaries/data.zip"));
});
