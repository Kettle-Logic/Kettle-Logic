# Kettle Logic

A recipe design and brewing management app for homebrewers, with a shared C++ engine and native front ends for macOS, Windows, and Linux.

## Layout

```
LICENSE                    MIT license
docs/
  architecture.md          layers, platform stacks, threading, decisions
  units.md                 exact quantities, SI canonical storage, rounding rules
  commands.md              unified command model (undo/redo, merging, composites)
  model.md                 model layer interface: read/write, typed vs preserved, edits
  calc.md                  recipe calculations: gravities, ABV, IBU, color, strike water
  updates.md               how the app updates its engine and itself from GitHub Releases
  formats/
    beerjson-profile.md    Kettle Logic's BeerJSON 1.0 profile (primary format)
    beerxml-import.md      BeerXML 2.01 import/export mapping (BeerSmith compatibility)
core/                      C++20 engine (no UI code)
  include/kettlelogic/     public headers
    json.hpp               strict JSON with exact number lexemes
    rational.hpp           exact rational numbers
    quantity.hpp           exact quantities in SI canonical units
    measure.hpp            exact, unconverted values (gravity, color, pH, ...)
    command.hpp            generic undoable command stack
    model/                 recipe model, BeerJSON reader/writer, sidecar, model commands
    calc/                  recipe calculations (pure functions over the model)
    catalog/               built-in data every front end reads: ingredients (malts, hops, yeasts, waters, additions) and the
                           search, filter, and sort behind the ingredient picker,
                           brewing systems, starter recipes, fermentation profiles, glossary, tool forms, option lists
    io/                    atomic file save and load of a recipe and its sidecar
    styles/                user-defined beer styles and their checks; the BJCP table is in calc/style.cpp
  src/                     implementation
  schemas/beerjson/        vendored upstream BeerJSON schema (pinned commit, see its README)
  tests/                   engine tests, sample data, schema conformance check
  tools/                   kettlelogic_roundtrip: read and rewrite a BeerJSON file
tools/                     update_manifest.py: writes one platform's entry in the update list (updates.json)
build/                     output of the build steps below, including release files and logs; not committed
apps/                      platform front ends
  macos/                   SwiftUI app (started)
  windows/                 WinUI 3 with C++/WinRT (written, not yet compiled)
  linux/                   GTK4 + libadwaita (written; builds and passes its smoke test, not yet run on Linux)
```

## Versions

The engine and each app have their own semantic versions. Every app's About box shows both.

- Engine: `core/include/kettlelogic/version.hpp`, which must equal the `project(... VERSION ...)` in `core/CMakeLists.txt`. The engine's tests check this.
- macOS app: `app_version` and `app_build` in `apps/macos/make-app.sh`.
- Linux app: `project(... VERSION ...)` in `apps/linux/CMakeLists.txt`.
- Windows app: `KettleLogicUiVersion` in `apps/windows/KettleLogic/KettleLogic.Paths.props`.

The engine also has an interface number, `KL_ENGINE_ABI_VERSION` in `core/include/kettlelogic/c/kettlelogic.h`. An app loads only an engine with the same number. See [docs/updates.md](docs/updates.md).

## Building and testing the engine

Requirements: CMake 3.25+, a C++20 compiler, and Boost 1.80 or newer (header-only `multiprecision` is used for exact rationals). The schema conformance test also needs Python 3 with `jsonschema`.

On macOS with Homebrew:

```sh
brew install boost cmake
python3 -m pip install jsonschema
```

On Debian or Ubuntu 24.04 or newer:

```sh
sudo apt install build-essential cmake libboost-dev python3-jsonschema
```

On Fedora:

```sh
sudo dnf install gcc-c++ cmake boost-devel python3-jsonschema
```

On Windows, from a Developer PowerShell for Visual Studio 2026 (see [Windows](#windows-winui-3-with-c-winrt) for the install list), with Boost from vcpkg:

```powershell
vcpkg install boost-headers
python -m pip install jsonschema
$env:BOOST_ROOT = "C:\vcpkg\installed\x64-windows\include"
```

Then, on macOS and Linux:

```sh
cmake -S core -B build/core
cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

On Windows, the Visual Studio generator is multi-config, so the build and test steps name the configuration:

```powershell
cmake -S core -B build\core -G "Visual Studio 18 2026" -A x64
cmake --build build\core --config Release
ctest --test-dir build\core -C Release --output-on-failure
```

`apps\windows\build.ps1` does the same engine build and tests as part of the app build, so you only need the steps above
to work on the engine by itself.

The engine's outputs in `build/core` are:

- `libkettlelogic_core.a`: the engine's C++ API. The Linux and Windows apps link it.
- `libkettlelogic_c.a`: the C facade (`core/include/kettlelogic/c/kettlelogic.h`), linked statically by the tests.
- `libkettlelogic_engine.dylib` on macOS (`.so` on Linux, `.dll` on Windows): the same facade as a shared library. The macOS
  app loads it at launch, so the engine can be updated apart from the app. Build it with `--target kettlelogic_engine`.

When the C facade changes, run `python3 core/tools/gen_engine_loader.py` (Python 3, no extra packages) to regenerate the
macOS app's loader, `apps/macos/Sources/CKettleLogic/engine_loader.c`. The `engine_loader_current` test fails until you do.
Also bump `KL_ENGINE_ABI_VERSION` in the header when the facade's interface changes. See [docs/updates.md](docs/updates.md).

## Building the front ends

The macOS app loads its engine from `build/core`, so build the engine there first. The Linux and Windows apps compile the
engine as part of their own build, into `build/linux` and `build/windows-logic`, so they need no separate engine step.

### macOS (SwiftUI)

Requirements: macOS 26 with Xcode 26.2 or newer, which provides the Swift 6.2 toolchain and the macOS 26 SDK, and Homebrew's
`boost` and `cmake` (see [Building and testing the engine](#building-and-testing-the-engine)).

```sh
cmake -S core -B build/core
cmake --build build/core --target kettlelogic_engine
cd apps/macos
swift run KettleLogic
```

To start the app as a Mac app, with a menu bar and keyboard focus, build the bundle and open it:

```sh
apps/macos/make-app.sh
open apps/macos/build/KettleLogic.app
```

`make-app.sh` builds the engine, builds the app in release mode, and wraps it in `apps/macos/build/KettleLogic.app`, with the engine inside. The app checks GitHub for updates when it is built with `UPDATE_MANIFEST_URL` set. To publish a release, see [docs/updates.md](docs/updates.md) and `apps/macos/make-release.sh`. The CMake commands above only build the engine and run its tests; they do not start the app.

`swift run KettleLogic` also works, but it starts a bare executable from the shell, so use the bundle for everyday use.

To work in Xcode, open `apps/macos/KettleLogic.xcodeproj` and run the `KettleLogic` scheme. Its Build Engine phase runs the
CMake build of `kettlelogic_engine` on every build, and its Embed Engine phase copies the library into the app, so no
separate engine step is needed. To work on `apps/macos/Package.swift` instead, run the CMake steps above first, and again
after each engine change, because that route loads the library from `build/core`. The full Xcode workflow, including
signing setup notes and troubleshooting, is in [apps/macos/README.md](apps/macos/README.md).

The app reaches the engine only through the C facade, so no C++ types reach the Swift code. The built-in data (`catalog/`) comes through the same facade as JSON (`kl_ingredient_catalog_json` and `kl_builtin_data_json`), so the Swift code holds no copy of it. Each edit is a command, so undo and redo cover all of them.

#### Packaging: macOS (DMG)

Requirements: Xcode 26.2 or newer, a Developer ID Application certificate in the keychain, a `notarytool` keychain
profile, and Python 3. The `gh` tool is needed only to publish. The release scripts use Apple's `codesign`, `notarytool`,
`stapler`, `hdiutil`, and `osascript`, which come with macOS and Xcode.

Before a release:

- Bump the app's version, `MARKETING_VERSION` in the Xcode project. Bump the engine's version, `kEngineVersion` in
  `core/include/kettlelogic/version.hpp` and `project(... VERSION ...)` in `core/CMakeLists.txt`, and `KL_ENGINE_VERSION`
  in the Xcode project, so all three agree.
- Bump `KL_ENGINE_ABI_VERSION` only when the C facade's interface changed.
- Run the engine's tests, as in [Building and testing the engine](#building-and-testing-the-engine).

The app is signed in Xcode, which is the normal route to a release. Xcode's Developer ID signature, hardened runtime,
and notarization are then run by the release scripts:

1. In Xcode, choose Product > Archive, then Distribute App > Developer ID, and export `KettleLogic.app`.
2. Create the notarization profile once: `xcrun notarytool store-credentials kettlelogic --apple-id YOU@EXAMPLE.COM --team-id YOUR_TEAM_ID`.
   The command asks for an app-specific password, which is not your Apple ID password. Generate one at
   [account.apple.com](https://account.apple.com) under Sign-In and Security > App-Specific Passwords (two-factor
   authentication must be on for your Apple ID). Create a new one, label it something like `notarytool`, and paste it
   when the command prompts. Apple shows it only once. `notarytool` stores it in your login keychain, so the release
   scripts can use the `kettlelogic` profile without asking again.

   `YOUR_TEAM_ID` is your 10-character Apple Developer Team ID. Find it at [developer.apple.com/account](https://developer.apple.com/account) under Membership details, or in the parentheses of the identity that `security find-identity -v -p codesigning` prints. It is not a secret, but it is yours, so it is not committed. The Xcode project ships with `DEVELOPMENT_TEAM` empty. In Xcode, set it under the KettleLogic target's Signing & Capabilities.
3. Build the release files, from the repository root:

   ```sh
   APP=/path/to/Exported/KettleLogic.app NOTARY_PROFILE=kettlelogic \
     SIGN_IDENTITY="Developer ID Application: Your Name (YOUR_TEAM_ID)" sh apps/macos/make-release.sh
   ```

   This checks that the app's versions agree with the engine's headers, notarizes and staples the app, makes the update
   zip and the DMG (`apps/macos/make-dmg.sh`), notarizes the DMG, and writes `updates.json`. The files are in
   `apps/macos/build/release`: `KettleLogic-<version>-macos.zip`, `KettleLogic-<version>-macos.dmg`,
   `KettleLogic-engine-<engine version>-macos.dylib`, and `updates.json`.
4. To publish, add `PUBLISH=1` to the same command. It uploads the files with the `gh` tool to `Kettle-Logic/Kettle-Logic`
   and replaces `updates.json`. Without it, the files are only built.

The DMG is the installer. Users drag the app into Applications. Recipes and settings are not in the app, so they are kept
when the app is replaced. Details are in [docs/updates.md](docs/updates.md#publishing-a-release).

### Linux (GTK4 and libadwaita)

The Linux app has the same features as the macOS app, built from native libadwaita widgets. It links the engine directly,
without the C facade, and shares the portable app logic with the Windows app. It builds and passes its smoke test with GTK
4.24 on macOS, and has not been run on a Linux desktop yet. See [apps/linux/README.md](apps/linux/README.md) for the setup,
what has been checked, and how it differs from the other apps.

Requirements: GTK 4.12 or newer, libadwaita 1.5 or newer, libsoup 3, pkg-config, and a C++20 compiler. On Debian or Ubuntu 24.04 or newer:

```sh
sudo apt install build-essential cmake pkg-config libgtk-4-dev libadwaita-1-dev libsoup-3.0-dev libboost-dev
```

On Fedora:

```sh
sudo dnf install gcc-c++ cmake pkgconf-pkg-config gtk4-devel libadwaita-devel libsoup3-devel boost-devel
```

Then:

```sh
cmake -S apps/linux -B build/linux
cmake --build build/linux
build/linux/kettlelogic-gtk path/to/Recipe.json
```

The engine is compiled into `build/linux/core` with the app, so there is no separate engine step. The smoke test is not part of
the default build. To build and run it, see [apps/linux/README.md](apps/linux/README.md).

#### Packaging: Linux (Flatpak)

The Flatpak is built on Linux, from the manifest `apps/linux/flatpak/org.kettlelogic.KettleLogic.yml`. Its runtime is
GNOME 48. Boost's headers are fetched and pinned by SHA-256, and the app is built from this repository with the same CMake
project as above. The Flatpak gets network access for the update check, and `~/KettleLogic` for recipes. The first build
needs network access too, to download the runtime and the Boost source. Run the commands from the repository root.

Install the tools and the runtime once:

```sh
sudo apt install flatpak flatpak-builder      # Fedora: sudo dnf install flatpak flatpak-builder
flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install flathub org.gnome.Platform//48 org.gnome.Sdk//48
```

Then, from the repository root:

```sh
flatpak-builder --force-clean --user --install build-flatpak apps/linux/flatpak/org.kettlelogic.KettleLogic.yml
flatpak run org.kettlelogic.KettleLogic
```

To make a file for a GitHub release, build into a repository and bundle it:

```sh
flatpak-builder --force-clean --repo=build-repo build-flatpak apps/linux/flatpak/org.kettlelogic.KettleLogic.yml
flatpak build-bundle build-repo KettleLogic-0.1.0-x86_64.flatpak org.kettlelogic.KettleLogic
```

A bundle installed with `flatpak install KettleLogic-0.1.0-x86_64.flatpak` does not update with `flatpak update`. Its users
install the next bundle from the release page. A copy installed from Flathub does update with `flatpak update`. Publishing
the release and its update entry is in [docs/updates.md](docs/updates.md#publishing-a-windows-or-linux-release).

The manifest is written but has not yet been built by flatpak-builder. Its build steps are the same ones this README gives
for Linux, and were run locally, installing to a scratch prefix, to check the installed layout. The runtime version is
pinned; move it to the current GNOME runtime before a release.

### Windows (WinUI 3 with C++/WinRT)

The Windows app has the same features as the macOS app, built from native WinUI 3 controls. It links the engine directly,
without the C facade. Its portable half builds and is tested on any platform. The WinUI project itself has not been compiled
yet. See [apps/windows/README.md](apps/windows/README.md) for the setup, what has been checked, and how it differs from the
Mac app.

Requirements: Visual Studio 2026, with "Desktop development with C++" and "Windows application development"; CMake
3.25 or newer and NuGet on the path (a Developer PowerShell has both with Visual Studio); and Boost 1.80 or newer, headers
only (`vcpkg install boost-headers`). Then, from a Developer PowerShell in the repository root:

```powershell
.\apps\windows\build.ps1 -Configuration Release -BoostRoot C:\vcpkg\installed\x64-windows\include
```

The script compiles the engine into `build\windows-logic`, runs its tests, then builds the app into
`build\windows-app\x64\Release`. `-SkipTests` skips the tests. Use `-Platform ARM64` for an ARM64 build.

#### Packaging: Windows (Inno Setup installer)

The installer is built on Windows, from `apps\windows\installer\KettleLogic.iss`, by `apps\windows\make-installer.ps1`. You
need Visual Studio 2026 with the C++ and WinUI workloads (the same as `build.ps1`), and Inno Setup 7 from
https://jrsoftware.org/isinfo.php. The app is self-contained, so the Windows App SDK runtime is included in the installer.

From a Developer PowerShell, in the repository root:

```powershell
.\apps\windows\make-installer.ps1 -Configuration Release -Platform x64
```

This builds the app, then writes `dist\KettleLogic-Setup-<version>-x64.exe`. The version is read from
`KettleLogic.Paths.props`. To sign the installer and its uninstaller, add a sign command, with `$f` where the file goes:

```powershell
.\apps\windows\make-installer.ps1 -SignCommand 'signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /a $f'
```

Use the certificate the app is signed with. The in-app update installs only an installer signed by the same certificate.
Pass `-SkipBuild` to package the last build again. Use `-Platform ARM64` for an ARM64 installer.

The installer installs into Program Files when run as an administrator, and offers a per-user install otherwise. It
never deletes user data: recipes stay in `%USERPROFILE%\KettleLogic\Recipes`, and settings stay in
`%LOCALAPPDATA%\KettleLogic`. Publishing the installer and its update entry is in
[docs/updates.md](docs/updates.md#publishing-a-windows-or-linux-release).

The installer script has not been compiled yet. It needs a Windows machine with Inno Setup 7.

## License

Kettle Logic is released under the [MIT License](LICENSE). The vendored BeerJSON schema in `core/schemas/beerjson` keeps its own license, in that folder.
