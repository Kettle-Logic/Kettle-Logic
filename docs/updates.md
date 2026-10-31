# Updates

Kettle Logic can update its engine, its app, or both. Both come from GitHub Releases. The macOS app can update its
engine and its app separately. The Windows and Linux apps update the whole app. Their engine is linked into the program,
so an engine-only update for them is not possible yet (see [Windows and Linux](#windows-and-linux)).

## What can be updated

- **The app**: `KettleLogic.app`, as a zip. It is the interface, and it includes a copy of the engine that it was built with.
- **The engine**: `libkettlelogic_engine.dylib`, the calculations and file handling as a shared library.
- **Both**: the app's zip and the engine, when a new app needs a newer engine than the one it carries.

## How the app loads the engine

The engine is a separate library, so an engine update does not need a new app. The app loads it at launch, in
`EngineLoader.swift`. It tries these copies, newest version first:

1. The copies that updates installed, in `~/Library/Application Support/KettleLogic/Engines/<version>/`.
2. The copy inside the app, in `Contents/Frameworks/`. This is the fallback, so the app always has an engine.

A copy is skipped, and the reason is listed in the update window, when:

- it cannot be loaded (corrupt, or the file is missing);
- its interface number is not the one the app was built with (see below);
- it lacks a function that the app calls.

The app calls the engine through `kl_engine_load`, in `Sources/CKettleLogic/engine_loader.c`. That file is generated from
the engine's C header, `core/include/kettlelogic/c/kettlelogic.h`, by `core/tools/gen_engine_loader.py`. Run the script
after you change the header. The `engine_loader_current` test fails when the generated file is out of date.

An engine update takes effect the next time the app starts. The app restarts after every update, so this happens
without a step from the user.

### The interface number

`KL_ENGINE_ABI_VERSION`, in `kettlelogic.h`, is the interface between the app and the engine. An app loads only an
engine with the same number. Bump it when a facade function is added, removed, or changes its arguments or meaning. Do
not bump it for changes inside the engine that leave the facade alone.

## Where updates come from

Each release is a set of GitHub release assets:

- `app-v<version>`: the app zip, `KettleLogic-<version>-macos.zip`.
- `engine-v<version>`: the engine, `KettleLogic-engine-<version>-macos.dylib`.
- `updates`: a pre-release that holds one file, `updates.json`. Its address is fixed, so the apps can always find it:

      https://github.com/Kettle-Logic/Kettle-Logic/releases/download/updates/updates.json

The address is baked into the app at build time. Set `UPDATE_MANIFEST_URL` when you run `make-app.sh`. Left empty, the
build has no update address, and the app says so when asked to check.

`updates.json` (schema 1):

```json
{
  "schema": 1,
  "app": {
    "version": "0.2.0",
    "engineAbi": 1,
    "url": "https://github.com/Kettle-Logic/Kettle-Logic/releases/download/app-v0.2.0/KettleLogic-0.2.0-macos.zip",
    "sha256": "..."
  },
  "engine": {
    "version": "0.2.1",
    "abi": 1,
    "url": "https://github.com/Kettle-Logic/Kettle-Logic/releases/download/engine-v0.2.1/KettleLogic-engine-0.2.1-macos.dylib",
    "sha256": "..."
  }
}
```

- `app.engineAbi` is the interface number of the app's built-in engine.
- `engine.abi` is the interface number of the engine. The app offers it only when the number matches its own.
- Either part can be absent. A reader stops on a manifest with a schema newer than it knows.
- Versions are `major.minor.patch` numbers. Pre-release tags are not supported.

## First-time setup: creating updates.json on GitHub

Do this once, for all platforms. Until it is done, every update check fails, because the address the apps read does not
exist yet. On Windows and Linux that is "the update server answered with status 404". On macOS it is "The list of updates
could not be read."

`updates.json` is not a file in the repository. It is an asset of a GitHub release whose tag is `updates`. The tag never
changes, so the address never changes:

    https://github.com/Kettle-Logic/Kettle-Logic/releases/download/updates/updates.json

Replacing the asset on that release is how a new version is announced. Do not commit it to the repository
(`/updates.json` is in `.gitignore`), and do not use the `raw.githubusercontent.com` address: the apps expect the
release-asset address. Never delete the `updates` release. The address is built into every app already installed, so
deleting it brings the failure back for all of them.

All three apps read the same list. Each platform has its own entry, and a platform with no entry is simply told it is
up to date:

| Entry | Read by | Needs | `url` is |
|---|---|---|---|
| `app`, `engine` | macOS | `sha256`; `app` also `engineAbi`, `engine` also `abi` | the zip, and the `.dylib` |
| `windows` | Windows | `sha256` | the installer's own download address |
| `linux` | Linux | no `sha256` | the release page |

An app is offered an entry only when its `version` is higher than the running app's (`kAppVersion`, 0.1.0 for now).

**One-time preparation (any platform).** Install the `gh` tool from <https://cli.github.com> and run `gh auth login`
with an account that can write to the repository.

### macOS

`apps/macos/make-release.sh` does all of it. With `PUBLISH=1` it uploads the app and engine releases, **creates the
`updates` pre-release if it does not exist**, and uploads `updates.json` into it. If no list is published yet, it says
"No update list is published yet, so a new one is started" and writes `app` and `engine` entries. Follow the steps under
[Publishing a release](#publishing-a-release).

### Windows

1. Build the installer with `apps\windows\make-installer.ps1`, which prints the commands below with the right file names.
2. Publish it. The release tag is `app-v<version>`:

   ```sh
   gh release create app-v0.2.0 dist\KettleLogic-Setup-0.2.0-x64.exe --repo Kettle-Logic/Kettle-Logic ^
     --title "Kettle Logic 0.2.0" --notes "What changed."
   ```

3. Write the list. The tool records the installer's SHA-256, which the Windows entry needs:

   ```sh
   python tools\update_manifest.py updates.json windows --version 0.2.0 ^
     --url https://github.com/Kettle-Logic/Kettle-Logic/releases/download/app-v0.2.0/KettleLogic-Setup-0.2.0-x64.exe ^
     --file dist\KettleLogic-Setup-0.2.0-x64.exe
   ```

### Linux

There is no installer to upload: the Flatpak runtime installs the app, and the offer only tells the user to run
`flatpak update org.kettlelogic.KettleLogic`. Publish the release, then write the entry with no `--file`:

```sh
gh release create app-v0.2.0 --repo Kettle-Logic/Kettle-Logic --title "Kettle Logic 0.2.0" --notes "What changed."
python3 tools/update_manifest.py updates.json linux --version 0.2.0 \
  --url https://github.com/Kettle-Logic/Kettle-Logic/releases/tag/app-v0.2.0
```

### Creating the `updates` release (Windows and Linux)

Once `updates.json` exists, create the release. It is a pre-release, so it never shows as the repository's latest:

```sh
gh release create updates updates.json --repo Kettle-Logic/Kettle-Logic --prerelease ^
  --title "Update list" --notes "Where Kettle Logic looks for updates. Do not delete this release."
```

The result looks like this (`windows` and `linux` can be in the same file):

```json
{
  "schema": 1,
  "windows": {
    "version": "0.2.0",
    "url": "https://github.com/Kettle-Logic/Kettle-Logic/releases/download/app-v0.2.0/KettleLogic-Setup-0.2.0-x64.exe",
    "sha256": "<64 lowercase hex digits>"
  },
  "linux": {
    "version": "0.2.0",
    "url": "https://github.com/Kettle-Logic/Kettle-Logic/releases/tag/app-v0.2.0"
  }
}
```

### Check it

Open the address above in a browser: it should download `updates.json`. Then use Check for Updates in an app older than
the listed version. It should offer the update, and an app at the listed version says that it is up to date.

### Later releases

Fetch the published list, add the new entry, and replace the asset. The other platforms' entries stay. For Windows and
Linux, see [Publishing a Windows or Linux release](#publishing-a-windows-or-linux-release). macOS does it in
`make-release.sh`.

### Testing a list before publishing it

Serve a folder that holds `updates.json` (`python3 -m http.server`) and point a build at it:

- Windows and Linux: `-DKETTLELOGIC_UPDATE_MANIFEST_URL=http://localhost:8000/updates.json` when configuring CMake.
- macOS: `UPDATE_MANIFEST_URL=http://localhost:8000/updates.json` when running `make-app.sh`.

## What the app does

1. **Check.** The app fetches `updates.json`, with a 20-second timeout. At launch it checks when "Check for updates when
   Kettle Logic starts" is on (the default). An automatic check shows the window only when something is newer, and fails
   quietly. Check for Updates in the app menu always shows the window.
2. **Offer.** The app is offered when the manifest's version is newer than the running app. The engine is offered when
   its version is newer than the loaded engine and its interface number matches. When a new app needs a newer engine,
   "Update Both" installs them together.
3. **Install.** Each download is checked before anything changes:
   - Its SHA-256 must match the one in the manifest.
   - It must be validly signed by the same team as the running app. A development build has no team, so an update to
     it must be unsigned by a team too.
4. **Engine.** The file is copied to `Engines/<version>/` under a temporary name and then renamed, so an interrupted
   install leaves nothing behind. Older installed engines are removed once a newer one loads.
5. **App.** The zip is unpacked with `ditto`, which keeps the signature. A small script, `relaunch.sh`, is started, and
   then the app quits. The script waits for the app to exit, moves the old app aside, moves the new one into its place,
   and opens it. If the move fails partway, the old app is put back. The app needs write access to the folder that
   holds it, such as `/Applications`.

## Windows and Linux

These apps check the same update list, under their own entries (`windows` and `linux`). They update the whole app.

- **Checking.** At launch, when "Check for updates when Kettle Logic starts" is on (the default). Help > Check for Updates,
  or Settings, always checks, and says the result. An automatic check shows an offer only when a newer version exists,
  and fails quietly when the list cannot be reached.
- **Windows.** Install Update downloads the installer into `%LOCALAPPDATA%\KettleLogic\Updates`, and checks it in three
  steps: its SHA-256 must match the list; its Authenticode signature must be valid; and it must be signed by the same
  certificate as the running app. Then the app runs the installer with `/SILENT /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS`
  and closes, so the installer can replace it. The installer decides whether it needs administrator rights.
- **Linux (Flatpak).** A Flatpak's program is read-only and belongs to its runtime, so the app cannot replace itself. The
  offer shows the command `flatpak update org.kettlelogic.KettleLogic`, with a button that copies it, and a link to the
  release notes.

### The Windows installer

The installer is [apps/windows/installer/KettleLogic.iss](../apps/windows/installer/KettleLogic.iss), built by
[apps/windows/make-installer.ps1](../apps/windows/make-installer.ps1). The in-app update depends on these rules, which the script
follows:

- The `AppId` is a fixed GUID, which never changes, so a new installer upgrades the program in place.
- `CloseApplications=yes` and `RestartApplications=yes`, which the app's `/CLOSEAPPLICATIONS` and
  `/RESTARTAPPLICATIONS` switches rely on.
- The installer and the program are both signed with the team's certificate (`make-installer.ps1 -SignCommand`).
  The app compares certificate thumbprints.
- Uninstalling never deletes `%LOCALAPPDATA%\KettleLogic` or `%USERPROFILE%\KettleLogic\Recipes`. Leave out
  `[UninstallDelete]` entries for them, or ask first.

### Publishing a Windows or Linux release

Both start from the list already published, so the other platforms' entries stay. Fetch it with
`gh release download updates --repo Kettle-Logic/Kettle-Logic --pattern updates.json` first.

```sh
# Windows, on the Windows machine that built the signed installer
python tools\update_manifest.py updates.json windows --version 0.2.0 ^
  --url https://github.com/Kettle-Logic/Kettle-Logic/releases/download/app-v0.2.0/KettleLogic-Setup-0.2.0-x64.exe ^
  --file dist\KettleLogic-Setup-0.2.0-x64.exe
gh release upload app-v0.2.0 dist\KettleLogic-Setup-0.2.0-x64.exe --repo Kettle-Logic/Kettle-Logic
gh release upload updates updates.json --clobber --repo Kettle-Logic/Kettle-Logic

# Linux, once the Flatpak is published (no file: the runtime installs it)
python3 tools/update_manifest.py updates.json linux --version 0.2.0 \
  --url https://github.com/Kettle-Logic/Kettle-Logic/releases/tag/app-v0.2.0
```

`tools/update_manifest.py` writes the entry for one platform and keeps the rest. The macOS `make-release.sh` uses it too.

### How it was tested

- The update module (version rules, the list reader, the newer-release decision, the checksum, and the check through a
  fake transport) is in the Windows logic's tests, which run on any platform: 1228 checks pass.
- The Linux app was run against a local list behind a redirect, the way GitHub serves its download links, and libsoup
  followed the redirect and read the list. The offer dialog was rendered in the smoke test.
- The Windows code (WinHTTP, the PowerShell signature check, and the WinUI dialog) has not been compiled or run. It needs
  a Windows machine with Visual Studio 2022 and the Windows App SDK, using `apps\windows\build.ps1`.

## Publishing a release

Signing and notarization are done with Xcode and Apple's tools. Notarization is needed for the DMG, so that people who
download it are not blocked by Gatekeeper.

1. **Bump the versions.** The app's version is `MARKETING_VERSION` in the Xcode project. The engine's is
   `kEngineVersion` in `core/include/kettlelogic/version.hpp`, which must match `project(... VERSION ...)` in
   `core/CMakeLists.txt`. The Xcode project's `KL_ENGINE_VERSION` must match the engine's too. Bump the interface number
   only if the facade changed.
2. **Archive and export the app in Xcode.** Choose Product > Archive, then Distribute App > Developer ID, and export
   `KettleLogic.app`. Xcode signs it with the team's Developer ID certificate and the hardened runtime. The engine inside
   is signed by the Embed Engine phase with the same identity.
3. **Build the release files.** Give the exported app to `make-release.sh`:

   ```sh
   APP=/path/to/Exported/KettleLogic.app \
   NOTARY_PROFILE=kettlelogic \
   SIGN_IDENTITY="Developer ID Application: Your Name (YOUR_TEAM_ID)" \
   sh apps/macos/make-release.sh
   ```

   `YOUR_TEAM_ID` is your 10-character Apple Developer Team ID. Find it at [developer.apple.com/account](https://developer.apple.com/account) under Membership details, or in the parentheses of the identity that `security find-identity -v -p codesigning` prints. See the [README](../README.md#packaging-macos-dmg) for details.

   The script checks that the app's version, engine version, and update address agree with each other, and stops if
   they do not. It then notarizes and staples the app (`notarize.sh`), makes the update zip, makes the DMG
   (`make-dmg.sh`), signs and notarizes the DMG, and writes `updates.json`. Everything is in
   `apps/macos/build/release`.
4. **Publish.** Add `PUBLISH=1`, with the `gh` tool signed in to `Kettle-Logic/Kettle-Logic`:

   ```sh
   PUBLISH=1 APP=... NOTARY_PROFILE=... SIGN_IDENTITY=... sh apps/macos/make-release.sh
   ```

   This creates `app-v<version>` with the zip and the DMG, creates `engine-v<version>` if it is new, and replaces
   `updates.json` in the `updates` pre-release.

The notarytool keychain profile is set up once with `xcrun notarytool store-credentials kettlelogic`. See the comments in
[notarize.sh](../apps/macos/notarize.sh).

Without `APP`, the release script builds the app from the command line, signed ad hoc. That path cannot be notarized,
so use it for testing only.

## Your data and reinstalling

The installers do not write into user data, because user data is never stored where an installer puts the program.

| Platform | Program | Recipes | Settings and custom data |
|---|---|---|---|
| macOS (DMG) | `/Applications/KettleLogic.app` | `~/KettleLogic/Recipes` | Preferences, under the bundle ID `com.kettlelogic.KettleLogic`. Custom ingredients, equipment, and water profiles in `~/Library/Application Support/KettleLogic/`. Installed engines in the same folder, under `Engines/`. |
| Windows (InnoSetup, planned) | `%ProgramFiles%\KettleLogic` | `%USERPROFILE%\KettleLogic\Recipes` | `%LOCALAPPDATA%\KettleLogic` (`settings.json`, `custom-*.json`, `water-profiles.json`) |
| Linux (Flatpak, planned) | `/app`, read-only, from the runtime | `~/KettleLogic/Recipes`, which the Flatpak needs permission to reach | `~/.var/app/<app-id>/data/KettleLogic`, the Flatpak's own data folder |

What a reinstall or upgrade does with this:

- **macOS.** Dragging a new app over the old one replaces only the bundle. Recipes, preferences, and the Application
  Support folder are untouched. One thing to know: an installed engine is kept after an app is replaced, and the loader
  uses the newest engine it can load. An older app therefore may run a newer installed engine. That is safe when the
  interface numbers match. When they do not, the engine is skipped and the app's own copy is used.
- **Windows.** The InnoSetup script must use one fixed `AppId`, so a new installer upgrades the program in place. It must
  not delete the data folder. Uninstalling should leave `%LOCALAPPDATA%\KettleLogic` and the recipes alone, or ask first.
- **Linux.** Flatpak updates replace `/app` and keep `~/.var/app/<app-id>/data`. Removing the app keeps the data unless
  the user runs `flatpak uninstall --delete-data`.

Two things would break this, and they should be tested before any installer ships: an installer that writes into its own
data folder, and an uninstaller that deletes the user's folders. Dragging the app to the Trash, on macOS, leaves
`~/KettleLogic/Recipes` and the Application Support folder behind, which is intended.

The in-app update works only for the macOS app. Windows programs in Program Files and Flatpak programs in `/app` cannot
replace themselves, so their app updates should go through the installer or Flatpak.

## Limits and trust

- The files come from the repository's GitHub releases, so whoever can publish there can publish an update. The
  SHA-256 in the manifest catches corrupted or mismatched downloads. It does not protect against a changed manifest.
  The signature check does more: a replaced file must also be signed by the team's certificate, which GitHub does not
  hold. Development builds, which have no team, accept only other team-less builds.
- The signature is checked when an update is installed. It is not checked again each time the engine loads, so a
  copy changed on disk after installing would still load.
- Updates are whole files. There are no partial (delta) updates.
- The in-app update needs write access to the folder that holds the app, such as `/Applications`. A user without that
  access is told to download the update from GitHub instead.
- Windows and Linux get whole-app updates only. Their engine is linked into the program, so updating it alone needs the
  engine behind the C facade, as on macOS. That port has not been done.
- The Windows installer is run only when it is signed by the same certificate as the running app. A development build
  is not signed, so it never runs an update, and it must be updated by hand.
- An update that fails to install leaves the current app and engine in place.

## Testing

- `ctest --test-dir build/core` runs `kettlelogic_engine_loader`, which loads the built engine through the loader, and
  `engine_loader_current`, which checks the generated loader against the header.
- The Swift side has no automated test target. Check an update by hand with `UPDATE_MANIFEST_URL` pointed at a local
  server, for example `python3 -m http.server` in a folder that holds an `updates.json`.
