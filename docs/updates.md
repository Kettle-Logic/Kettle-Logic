# Updates and releases

Kettle Logic gets its updates from GitHub Releases. This guide has two halves: what users see when they update, and what
you do to publish a release. Troubleshooting is at the end.

- [How updates work](#how-updates-work)
- [Updating the app (for users)](#updating-the-app-for-users)
- [Publishing a release](#publishing-a-release)
- [Testing an update list locally](#testing-an-update-list-locally)
- [Troubleshooting](#troubleshooting)
- [Reference](#reference)

## How updates work

Every release has two parts: the files people download, and a small list, `updates.json`, that tells the apps what is new.
The apps read the list, compare versions, and offer an update when the listed version is higher than their own.

| | macOS | Windows | Linux |
|---|---|---|---|
| Release file | `.zip` (updates) and `.dmg` (first installs) | `.exe` installer | Flatpak |
| Updates | App and engine, separately or together | Whole app, through the installer | Whole app, through Flatpak |
| In-app install | Yes | Yes | No. The app shows the command to run |
| List entry | `app` and `engine` | `windows` | `linux` |

An engine-only update works on macOS because the engine is a separate library there. On Windows and Linux the engine is
linked into the program, so only whole-app updates are possible for now.

## Updating the app (for users)

### Check for updates

Kettle Logic checks when it starts, unless you turn off "Check for updates when Kettle Logic starts" in Settings. An
automatic check opens a window only when something is newer, and stays quiet if it can't reach GitHub. To check
yourself, choose Check for Updates from the app menu (macOS) or Help menu (Windows, Linux). That always shows a result.

### macOS

1. Choose Check for Updates.
2. Choose Update App, Update Engine, or Update Both. Kettle Logic offers Update Both when the new app needs a newer engine.
3. Wait. The app downloads the update, checks it, installs it, and restarts.

The app needs permission to write to the folder it lives in, such as `/Applications`. If it doesn't have it, download the
newest `.dmg` from the [Releases page](https://github.com/Kettle-Logic/Kettle-Logic/releases) and drag the app over the old one.

### Windows

1. Choose Help > Check for Updates.
2. Choose Install Update. The app downloads the installer, checks it, closes, runs the installer silently, and reopens.

If the installer needs administrator rights, Windows asks you for them.

### Linux

1. Choose Help > Check for Updates.
2. If you installed from Flathub, run the command the window shows (the Copy button copies it):

   ```sh
   flatpak update org.kettlelogic.KettleLogic
   ```

   If you installed from a `.flatpak` file, `flatpak update` can't see new versions. Download the newest `.flatpak` from the
   Releases page and install it the same way you installed the first one.

### Your data is safe

Updating, reinstalling, and uninstalling never touch your recipes or settings.

| Platform | Program | Recipes | Settings and custom data |
|---|---|---|---|
| macOS | `/Applications/KettleLogic.app` | `~/KettleLogic/Recipes` | Preferences (bundle ID `com.kettlelogic.KettleLogic`). Custom ingredients, equipment, and water profiles in `~/Library/Application Support/KettleLogic/`, with installed engines under `Engines/` |
| Windows | `%ProgramFiles%\KettleLogic`, or a per-user folder if installed without administrator rights | `%USERPROFILE%\KettleLogic\Recipes` | `%LOCALAPPDATA%\KettleLogic` (`settings.json`, `custom-*.json`, `water-profiles.json`) |
| Linux | `/app` (read-only, part of the Flatpak) | `~/KettleLogic/Recipes` | `~/.var/app/org.kettlelogic.KettleLogic/data/KettleLogic` |

Removing the app leaves these folders in place. On Linux, `flatpak uninstall --delete-data` also deletes the settings folder.

## Publishing a release

This part is for maintainers. Each platform is built on its own machine, so you can publish them in any order. Each
publish adds that platform's entry to `updates.json` and keeps the others.

### Before you start (all platforms)

1. Install the `gh` tool from <https://cli.github.com>, and run `gh auth login` with an account that can write to
   `Kettle-Logic/Kettle-Logic`.
2. Bump the versions you changed:

   | What | Where |
   |---|---|
   | Engine | `kEngineVersion` in `core/include/kettlelogic/version.hpp`, which must equal `project(... VERSION ...)` in `core/CMakeLists.txt`. The engine's tests check this. |
   | macOS app | `MARKETING_VERSION` in the Xcode project, `app_version` and `app_build` in `apps/macos/make-app.sh`, and `KL_ENGINE_VERSION` in the Xcode project (it must equal the engine's) |
   | Linux app | `project(... VERSION ...)` in `apps/linux/CMakeLists.txt` |
   | Windows app | `KettleLogicUiVersion` in `apps/windows/KettleLogic/KettleLogic.Paths.props` |
   | Interface number | `KL_ENGINE_ABI_VERSION` in `core/include/kettlelogic/c/kettlelogic.h`. Bump it only when a facade function is added, removed, or changes its arguments or meaning. |

3. Build the engine and run its tests. On macOS and Linux:

   ```sh
   cmake -S core -B build/core
   cmake --build build/core
   ctest --test-dir build/core --output-on-failure
   ```

   On Windows, skip this step. `make-release.ps1` builds the engine and runs its tests as part of the build. To run just
   the engine tests, see [Testing an update list locally](#testing-an-update-list-locally).

4. Decide whether the release reports anonymous usage. A build reports to Google Analytics only when it is given a
   measurement ID and an API secret, and sends nothing otherwise. See [Privacy](privacy.md) for what is reported. The
   release scripts don't set these for you, so a release built without them has no telemetry.

   - **Measurement ID:** `G-5J1VSWCP8V`. It is not secret.
   - **API secret:** keep it out of the repository, and read it from your shell or your CI secrets. To make one, open
     [analytics.google.com](https://analytics.google.com) and go to Admin > Data collection and modification > Data
     streams > the web stream > Measurement Protocol API secrets > Create.

   | App | How to give the release its ids |
   |---|---|
   | macOS | Set `KL_GA_MEASUREMENT_ID` and `KL_GA_API_SECRET` in the environment of the build. The Xcode project reads them as build settings, and `make-app.sh` reads them too. |
   | Windows | Configure the logic build once with `cmake -S apps/windows -B build/windows-logic -DKETTLELOGIC_GA_MEASUREMENT_ID=G-5J1VSWCP8V -DKETTLELOGIC_GA_API_SECRET=<secret>`. CMake keeps the values in its cache, so `make-release.ps1` picks them up on later runs. |
   | Linux | Add the same two `-D` options to the `cmake -S apps/linux ...` line in `apps/linux/flatpak/org.kettlelogic.KettleLogic.yml`, and don't commit the secret. |

   To check the setup before you ship, add `-DKETTLELOGIC_GA_DEBUG=ON` (macOS: `KL_GA_DEBUG=YES`). Reports are then
   validated by Google and not recorded. Remove it for the real release.

Versions are `major.minor.patch` numbers. Pre-release tags are not supported.

### First time only: the `updates` release

Until it exists, every update check fails, because the address the apps read is a 404. The list is an asset of a
GitHub release tagged `updates`:

    https://github.com/Kettle-Logic/Kettle-Logic/releases/download/updates/updates.json

All three release scripts create this release for you the first time you publish, so there is nothing to do by hand. Don't
commit `updates.json` (it's in `.gitignore`), and don't use a `raw.githubusercontent.com` address. The apps expect the
release-asset address. **Never delete the `updates` release.** Its address is built into every installed app.

### Publish for macOS

Do this on a Mac with Xcode 26.2 or newer, a Developer ID Application certificate in your keychain, and Python 3. The
route is: archive in Xcode, notarize, build the DMG and update zip, then publish to GitHub Releases.

**Set up (once)**

1. In Xcode, open `apps/macos/KettleLogic.xcodeproj`. Under the KettleLogic target's Signing & Capabilities, set your
   Team. The project ships with `DEVELOPMENT_TEAM` empty, so your Team ID is never committed.
2. Create an app-specific password at [account.apple.com](https://account.apple.com) under Sign-In and Security >
   App-Specific Passwords. Apple shows it only once. Then store it for the release scripts:

   ```sh
   xcrun notarytool store-credentials kettlelogic --apple-id YOU@EXAMPLE.COM --team-id YOUR_TEAM_ID
   ```

   Paste the password when asked. `YOUR_TEAM_ID` is your 10-character Team ID, shown at
   [developer.apple.com/account](https://developer.apple.com/account) under Membership details, and in the parentheses of
   the identity that `security find-identity -v -p codesigning` prints.

**Release**

1. **Archive.** In Xcode, choose the KettleLogic scheme and a Mac destination, then Product > Archive. The Organizer opens
   when it finishes.
2. **Distribute and notarize.** In the Organizer, select the archive and choose Distribute App > Direct Distribution
   (Developer ID) > Upload. Xcode signs the app, sends it to Apple for notarization, and tells you when it is accepted.
   Then choose Export Notarized App, and save `KettleLogic.app` somewhere you can find it.

   The release script notarizes the app itself too, so you can skip Xcode's upload: choose Distribute App > Developer ID >
   Export instead.
3. **Build the DMG and update zip, and publish.** From the repository root:

   ```sh
   APP=/path/to/Exported/KettleLogic.app NOTARY_PROFILE=kettlelogic \
     SIGN_IDENTITY="Developer ID Application: Your Name (YOUR_TEAM_ID)" sh apps/macos/make-release.sh
   ```

   Run it without `PUBLISH=1` first. The script checks that the app's version, engine version, and update address agree,
   and stops if they don't. Then it notarizes and staples the app, makes the update zip, makes the DMG, signs and
   notarizes the DMG, and writes `updates.json`. Everything lands in `apps/macos/build/release`:

   - `KettleLogic-<version>-macos.zip`
   - `KettleLogic-<version>-macos.dmg`
   - `KettleLogic-engine-<engine version>-macos.dylib`
   - `updates.json`

   Open the DMG to check it, then run the same command again with `PUBLISH=1` in front:

   ```sh
   PUBLISH=1 APP=/path/to/Exported/KettleLogic.app NOTARY_PROFILE=kettlelogic \
     SIGN_IDENTITY="Developer ID Application: Your Name (YOUR_TEAM_ID)" sh apps/macos/make-release.sh
   ```

   This creates the `app-v<version>` release with the zip and the DMG, creates `engine-v<version>` if the engine is new,
   and replaces `updates.json` in the `updates` release. If `app-v<version>` already exists, nothing is uploaded, so bump
   the version first.

The DMG is for first installs only. It is not listed in `updates.json`, because the in-app update uses the zip.

**Build only the DMG, or publish an existing DMG**

To make a DMG from an app you already have, notarize it, and attach it to a release:

```sh
SIGN_IDENTITY="Developer ID Application: Your Name (YOUR_TEAM_ID)" \
  sh apps/macos/make-dmg.sh /path/to/KettleLogic.app apps/macos/build/release/KettleLogic-0.2.0-macos.dmg
NOTARY_PROFILE=kettlelogic sh apps/macos/notarize.sh apps/macos/build/release/KettleLogic-0.2.0-macos.dmg
gh release upload app-v0.2.0 apps/macos/build/release/KettleLogic-0.2.0-macos.dmg --repo Kettle-Logic/Kettle-Logic --clobber
```

Without `APP`, `make-release.sh` builds the app from the command line and signs it ad hoc. That build can't be notarized,
so use it for testing only.

### Publish for Windows

Do all of this from a Developer PowerShell for Visual Studio 2026, on Windows, in the repository root. You need Visual
Studio 2026 with the C++ and WinUI workloads, [Inno Setup 7](https://jrsoftware.org/isinfo.php), Python 3, the `gh` tool
signed in, and a code-signing certificate. `signtool` comes with the Windows SDK, which Visual Studio installs. You also
need the Boost headers (`vcpkg install boost-headers`), and you must tell the build where they are. Do this once per
PowerShell window, or use `-BoostRoot` with the script below:

```powershell
$env:BOOST_ROOT = "C:\vcpkg\installed\x64-windows\include"
```

Don't point it at MSYS2's Boost (`C:\msys64\...`). Those are MinGW headers, and they break the Visual Studio build.

One script builds the app, runs the tests, builds and signs the installer, writes the update list, and publishes:

1. **Build and check it first.** Without `-Publish`, nothing is uploaded:

   ```powershell
   .\apps\windows\make-release.ps1 -SignCommand 'signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /a $f'
   ```

   `$f` is where `signtool` gets the file to sign, so keep it as written. Use the same certificate every time, because the
   in-app update installs only an installer signed by the certificate that the running app was signed with.

   The installer is `dist\KettleLogic-Setup-<version>-x64.exe` and the list is `dist\updates.json`. Run the installer on a
   test machine if you can.
2. **Publish.** Run the same command with `-Publish` added:

   ```powershell
   .\apps\windows\make-release.ps1 -Publish -SignCommand 'signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /a $f'
   ```

   This creates the `app-v<version>` release with the installer (or adds the installer to that release if the macOS build
   already created it), fetches the published `updates.json`, adds the `windows` entry with the installer's SHA-256, and
   replaces `updates.json` in the `updates` release. It refuses to publish an unsigned installer.

Other options: `-Platform ARM64` for an ARM64 installer, `-SkipBuild` to package the last build again, and `-Repo` to publish
somewhere other than `Kettle-Logic/Kettle-Logic`.

### Publish for Linux

Do all of this in a Linux terminal, or in Windows Subsystem for Linux (WSL 2 with Ubuntu 24.04 or newer; run the commands in
the WSL shell, inside the Linux file system, such as `~/Kettle-Logic`, not under `/mnt/c`). You need Flatpak and
`flatpak-builder`, Python 3, and the `gh` tool signed in.

1. **Install the tools and the runtime (once).**

   ```sh
   sudo apt install flatpak flatpak-builder python3 gh      # Fedora: sudo dnf install flatpak flatpak-builder python3 gh
   flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
   flatpak install --user flathub org.gnome.Platform//48 org.gnome.Sdk//48
   gh auth login
   ```

   The manifest pins the GNOME 48 runtime. Move it to the current GNOME runtime before a release.
2. **Build the bundle and check it first.** From the repository root:

   ```sh
   sh apps/linux/make-release.sh
   ```

   This builds the Flatpak with `flatpak-builder`, bundles it into `apps/linux/build/release/KettleLogic-<version>-<arch>.flatpak`,
   and writes `updates.json` beside it. To try the bundle, run `flatpak install --user <that file>`, then
   `flatpak run org.kettlelogic.KettleLogic`. Flatpak apps in WSL need WSLg (Windows 11 or a current Windows 10) to show a window.
3. **Publish.**

   ```sh
   PUBLISH=1 sh apps/linux/make-release.sh
   ```

   This creates the `app-v<version>` release with the bundle (or adds the bundle to that release if another platform already
   created it), fetches the published `updates.json`, adds the `linux` entry, and replaces `updates.json` in the `updates`
   release. The `linux` entry points at the release page and has no checksum, because Flatpak does the installing.

### Check your work

1. Open the list's address in a browser. It should download `updates.json`.
2. Open an app older than the listed version and choose Check for Updates. It should offer the update.
3. Open an app at the listed version. It should say it is up to date.

## Testing an update list locally

Try a list before you publish it. Serve a folder that holds `updates.json`:

```sh
python3 -m http.server
```

Then point a build at it:

- **macOS:** run `UPDATE_MANIFEST_URL=http://localhost:8000/updates.json apps/macos/make-app.sh`.
- **Windows and Linux:** add `-DKETTLELOGIC_UPDATE_MANIFEST_URL=http://localhost:8000/updates.json` when you configure CMake.

The engine's own tests also cover the macOS loader. Build the engine first, then run the tests:

On macOS and Linux:

```sh
cmake -S core -B build/core
cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

On Windows, use a Developer PowerShell and name the Visual Studio generator. Without `-G`, CMake may pick another compiler,
which fails with syntax errors. Point `BOOST_ROOT` at your Boost headers first (for example, from
`vcpkg install boost-headers`):

```powershell
$env:BOOST_ROOT = "C:\vcpkg\installed\x64-windows\include"
cmake -S core -B build\core -G "Visual Studio 18 2026" -A x64
cmake --build build\core --config Release
ctest --test-dir build\core -C Release --output-on-failure
```

If you reuse a `build\core` folder from an earlier failed attempt, delete it first. CMake remembers the generator it
first used.

This runs `kettlelogic_engine_loader` (loads the built engine through the loader) and `engine_loader_current` (checks the
generated loader against the header). The Swift side has no automated tests, so check an update by hand as above.

## Troubleshooting

### Update checks

**"The update server answered with status 404" (Windows, Linux) or "The list of updates could not be read" (macOS).**
The `updates` release doesn't exist yet, or `updates.json` isn't attached to it. Follow
[First time only](#first-time-only-the-updates-release). Check that the release's tag is exactly `updates`.

**The app says it's up to date, but a newer version is out.**
- Your platform may have no entry in `updates.json`. A platform with no entry is told it's up to date. Publish its entry.
- The listed `version` must be higher than the running app's. Compare them.
- You may have replaced `updates.json` with a copy that lacks the other platforms. Always fetch the current list before you
  edit it.

**An automatic check showed nothing.** Automatic checks fail quietly. Choose Check for Updates yourself to see the error.

**The check times out.** The app waits 20 seconds. Check your connection and try again.

### Installing

**macOS: "The app can't be replaced" or the update won't install.** The app needs write access to the folder it lives
in. Move it to `/Applications`, or download the newest DMG from the Releases page and drag the app over the old one.

**macOS: an engine update isn't offered.** The engine's interface number must equal the app's. If the app and engine
disagree, update the app instead.

**macOS: the update window lists a skipped engine.** The loader skips an engine that is corrupt or missing, has the wrong
interface number, or lacks a function the app calls. The app falls back to the engine inside it, so it still works.
Install the newest app to get a matching engine.

**macOS: an older app runs a newer engine.** This is intended. Installed engines are kept after you replace the app, and
the loader uses the newest one that works. It's safe when the interface numbers match.

**Windows: hundreds of syntax errors, with paths under `C:\msys64\mingw64\include`.** CMake found MSYS2's MinGW Boost
instead of yours, and its headers don't work with Visual Studio. Set `BOOST_ROOT` (or pass `-BoostRoot`) to a folder that
holds `boost\version.hpp`, such as `C:\vcpkg\installed\x64-windows\include`. Then delete `build\windows-logic`, because
CMake remembers the Boost it found, and run the script again.

**Windows: the update never starts, or the check fails with a signature error.** The installer must be validly signed by
the same certificate as the running app. A development build is unsigned, so it never runs an update. Update it by hand.

**Windows or macOS: "checksum doesn't match".** The download is corrupt, or the `sha256` in `updates.json` doesn't match
the file. Download again. If it persists, regenerate the entry with `tools/update_manifest.py ... --file <installer>`.

**Linux: `flatpak update` finds nothing.** A Flatpak installed from a `.flatpak` file doesn't update with
`flatpak update`. Install the newest bundle from the Releases page.

### Publishing

**`make-release.sh` stops with a version mismatch.** The app's version, the engine's version, and the update address must
agree. Compare the values in the table under [Before you start](#before-you-start-all-platforms).

**`make-release.sh` stops about `NOTARY_PROFILE` or `SIGN_IDENTITY`.** Both are required whenever you pass `APP`. Set up the
profile with `xcrun notarytool store-credentials kettlelogic`.

**The DMG step asks to control Finder, or fails.** Laying out the DMG window asks Finder to run. Allow Terminal to control
Finder when prompted, and build from a logged-in desktop session. If `dmg-background.png` or `dmg-background@2x.png` is
missing from `apps/macos/`, the DMG is made without a background.

**The publish step uploads nothing.** The `app-v<version>` release already exists. Bump the version.

**`engine_loader_current` fails.** The C header changed. Run `python3 core/tools/gen_engine_loader.py`.

**You deleted the `updates` release.** Every installed app now fails to check. Recreate it with the same tag and attach
`updates.json`. Fetch the old list from your last build first if you still have it.

## Reference

### The update list

`updates.json` (schema 1) holds one entry per platform. Either part of the macOS entry can be absent, and a platform with
no entry is told it is up to date. A reader stops on a schema newer than it knows.

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
  },
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

| Entry | Read by | Needs | `url` is |
|---|---|---|---|
| `app`, `engine` | macOS | `sha256`. `app` also needs `engineAbi`, `engine` also `abi` | The zip, and the `.dylib` |
| `windows` | Windows | `sha256` | The installer's download address |
| `linux` | Linux | Nothing else | The release page |

- `app.engineAbi` is the interface number of the app's built-in engine.
- `engine.abi` is the interface number of the engine. The app offers the engine only when it matches its own.
- An entry is offered only when its `version` is higher than the running app's.

The release assets are `app-v<version>` (the app, and on macOS also the DMG), `engine-v<version>` (macOS engine), and
`updates` (a pre-release holding `updates.json`). The list's address is built into the app at build time. A build with
`UPDATE_MANIFEST_URL` empty has no update address, and says so when asked to check.

`tools/update_manifest.py` writes the entry for one platform and keeps the rest. `make-release.sh` uses it too.

### What the app checks before it installs

1. **Offer.** The app is offered when its entry's version is newer than the running app. The engine is offered when its
   version is newer than the loaded engine and its interface number matches.
2. **Download checks.** The SHA-256 must match the list. On macOS and Windows, the file must also be validly signed by the
   same team or certificate as the running app. A development build has no team, so it accepts only other team-less builds.
3. **Install.**
   - *macOS engine:* copied to `Engines/<version>/` under a temporary name, then renamed, so an interrupted install leaves
     nothing behind. Older installed engines are removed once a newer one loads.
   - *macOS app:* unpacked with `ditto`, which keeps the signature. A script, `relaunch.sh`, waits for the app to quit,
     moves the old app aside, moves the new one in, and opens it. If the move fails, the old app is put back.
   - *Windows:* the installer is saved to `%LOCALAPPDATA%\KettleLogic\Updates` and run with
     `/SILENT /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS`. The app compares certificate thumbprints.

A failed install leaves the current app and engine in place.

### How the macOS app loads its engine

The app loads the engine at launch (`EngineLoader.swift`) and tries these copies, newest version first:

1. Installed updates, in `~/Library/Application Support/KettleLogic/Engines/<version>/`.
2. The copy inside the app, in `Contents/Frameworks/`. This is the fallback, so the app always has an engine.

A copy is skipped, with the reason shown in the update window, when it can't be loaded, when its interface number differs
from the app's, or when it lacks a function the app calls. The app calls the engine through `kl_engine_load`, in
`Sources/CKettleLogic/engine_loader.c`, which `core/tools/gen_engine_loader.py` generates from the C header. An engine
update takes effect the next time the app starts, and the app restarts after every update.

### Rules the Windows installer follows

The in-app update depends on the installer ([KettleLogic.iss](../apps/windows/installer/KettleLogic.iss), built by
[make-installer.ps1](../apps/windows/make-installer.ps1)) doing these things:

- A fixed `AppId` GUID that never changes, so a new installer upgrades the program in place.
- `CloseApplications=yes` and `RestartApplications=yes`, which the app's switches rely on.
- The installer and the program are both signed with the same certificate.
- No `[UninstallDelete]` entries for `%LOCALAPPDATA%\KettleLogic` or `%USERPROFILE%\KettleLogic\Recipes`. Never delete
  user data, and ask first if that ever changes.

The installer installs into Program Files when run as an administrator, and offers a per-user install otherwise.

### Limits and trust

- Whoever can publish to the repository's GitHub releases can publish an update. The SHA-256 catches corrupt or
  mismatched downloads but not a changed list. The signature check does more, because GitHub doesn't hold the team's
  certificate.
- The signature is checked when an update is installed, not each time the engine loads. A copy changed on disk after
  installing would still load.
- Updates are whole files. There are no delta updates.
- Windows and Linux get whole-app updates only. Updating their engine alone needs it moved behind the C facade, as on
  macOS, and that port isn't done.

### Test status

- The update logic (version rules, the list reader, the newer-release decision, the checksum, and a check through a fake
  transport) is covered by the Windows logic tests, which run on any platform.
- The Linux app was run against a local list behind a redirect, the way GitHub serves download links, and read it. The
  offer dialog is covered by the smoke test.
- The Windows update code (WinHTTP, the PowerShell signature check, the WinUI dialog) and the installer script have not been
  compiled or run. They need a Windows machine with Visual Studio 2026, the Windows App SDK, and Inno Setup 7.
- The Flatpak manifest has not been built by `flatpak-builder` yet.
