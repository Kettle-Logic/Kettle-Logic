# Kettle Logic

Kettle Logic is free, open-source recipe software for beer, cider, mead, and wine. Design a recipe, check it against its
style, and follow it step by step on brew day. It runs natively on macOS, Windows, and Linux, with no account and no
sign-up. Your recipes stay on your computer. The app can send anonymous error reports and basic usage data to help
find and fix problems. It asks you the first time you open it, and you can turn it off. See
[docs/privacy.md](docs/privacy.md) for exactly what is sent.

## What you can do

**Plan your recipes**

- Work on beer, cider, mead, and wine in one app, and switch between them whenever you like.
- Watch original and final gravity, ABV, IBU, and color (SRM, with EBC and Lovibond conversions) update as you edit.
- Scale a recipe to a new batch size, and every ingredient follows.
- Save your equipment profile once, so every recipe matches your setup.
- Check a recipe against its style. Vital statistics for 86 BJCP 2021 styles are built in.
- Plan the mash and boil: strike-water temperature, decoction and step volumes, and boil-off.
- Use the water tools: ion blending, residual alkalinity, mash pH, and salt additions for a target profile.
- Plan sugar and honey additions, estimate alcohol, and dose sulfites for cider, mead, and wine.

**Brew with confidence**

- Follow a recipe step by step on brew day, with timers for the steps that need them.
- Print or export a brew sheet.

**Keep your data**

- Recipes are saved as BeerJSON 1.0 files on your computer.
- Import BeerXML 1.0 and 2.01 (from BeerSmith, for example), and export BeerXML 2.01. Before an export, Kettle Logic tells
  you what BeerXML cannot hold.
- Undo and redo cover every edit, and an import is a single undo step.
- Saves are atomic, so a crash during a save does not damage your recipe.

## Install

Download the file for your computer from the [Releases page](https://github.com/Kettle-Logic/Kettle-Logic/releases).
Pick the newest release whose name starts with `app-v`.

> **Status:** The macOS app is the most complete. The Windows and Linux apps are written but still being tested, so their
> download may not be on the Releases page yet. If yours is missing, [build from source](#build-from-source).

### macOS

1. Download `KettleLogic-<version>-macos.dmg`.
2. Open it and drag Kettle Logic into Applications.
3. Open Kettle Logic from Applications.

You need macOS 26.

### Windows

1. Download `KettleLogic-Setup-<version>-x64.exe`.
2. Run it and follow the prompts. Run it as an administrator to install for everyone, or as yourself to install just for you.
3. Open Kettle Logic from the Start menu.

Free code signing for the Windows build is provided by [SignPath.io](https://signpath.io), with a certificate from the
[SignPath Foundation](https://signpath.org).

### Linux

1. Install [Flatpak](https://flatpak.org/setup/) if you don't have it.
2. Download `KettleLogic-<version>-x86_64.flatpak`.
3. Install it, then run it:

   ```sh
   flatpak install KettleLogic-<version>-x86_64.flatpak
   flatpak run org.kettlelogic.KettleLogic
   ```

## Update

Kettle Logic checks for updates when it starts. You can also choose Check for Updates from the app menu (macOS) or
Help menu (Windows, Linux). When an update is available, follow the prompts. Your recipes and settings are never touched.
For platform details, see [docs/updates.md](docs/updates.md).

## Where your files live

| | Recipes | Settings and custom data |
|---|---|---|
| macOS | `~/KettleLogic/Recipes` | `~/Library/Application Support/KettleLogic` |
| Windows | `%USERPROFILE%\KettleLogic\Recipes` | `%LOCALAPPDATA%\KettleLogic` |
| Linux | `~/KettleLogic/Recipes` | `~/.var/app/org.kettlelogic.KettleLogic/data/KettleLogic` |

Uninstalling or reinstalling leaves these folders alone. Back up the Recipes folder and you've backed up your work.

## Build from source

You only need this if you want to change Kettle Logic, or if there's no download for your system yet. Run every command
from the repository root.

### 1. Get the tools

You need CMake 3.25 or newer, a C++20 compiler, and Boost 1.80 or newer (headers only). Tests also need Python 3 with
`jsonschema`.

**macOS** (Xcode 26.2 or newer, and [Homebrew](https://brew.sh)):

```sh
brew install boost cmake
python3 -m pip install jsonschema
```

**Debian or Ubuntu 24.04 or newer:**

```sh
sudo apt install build-essential cmake pkg-config python3-jsonschema libboost-dev \
  libgtk-4-dev libadwaita-1-dev libsoup-3.0-dev
```

**Fedora:**

```sh
sudo dnf install gcc-c++ cmake pkgconf-pkg-config python3-jsonschema boost-devel \
  gtk4-devel libadwaita-devel libsoup3-devel
```

**Windows:** install Visual Studio 2026 with "Desktop development with C++" and "Windows application development". Then,
in a Developer PowerShell:

```powershell
vcpkg install boost-headers
python -m pip install jsonschema
```

### 2. Build and run

**macOS**

```sh
apps/macos/make-app.sh
open apps/macos/build/KettleLogic.app
```

To work in Xcode instead, open `apps/macos/KettleLogic.xcodeproj` and run the `KettleLogic` scheme. See
[apps/macos/README.md](apps/macos/README.md) for the full Xcode workflow and troubleshooting.

**Windows**

```powershell
.\apps\windows\build.ps1 -Configuration Release -BoostRoot C:\vcpkg\installed\x64-windows\include
```

The app lands in `build\windows-app\x64\Release`. Add `-Platform ARM64` for an ARM64 build, or `-SkipTests` to skip the
engine tests. See [apps/windows/README.md](apps/windows/README.md) for more.

**Linux**

```sh
cmake -S apps/linux -B build/linux
cmake --build build/linux
build/linux/kettlelogic-gtk path/to/Recipe.json
```

See [apps/linux/README.md](apps/linux/README.md) for the smoke test and notes on how this app differs from the others.

### 3. Build a package (optional)

These make the same kind of file the Releases page offers, for your own use. They don't sign anything.

- **Windows installer.** Install [Inno Setup 7](https://jrsoftware.org/isinfo.php), then run
  `.\apps\windows\make-installer.ps1 -Configuration Release -Platform x64`. The installer is written to `dist\`.
- **Linux Flatpak.** Install the tools and runtime once:

  ```sh
  sudo apt install flatpak flatpak-builder      # Fedora: sudo dnf install flatpak flatpak-builder
  flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
  flatpak install flathub org.gnome.Platform//48 org.gnome.Sdk//48
  ```

  Then build, install, and run it:

  ```sh
  flatpak-builder --force-clean --user --install build-flatpak apps/linux/flatpak/org.kettlelogic.KettleLogic.yml
  flatpak run org.kettlelogic.KettleLogic
  ```

### Work on the engine

The engine is the C++ core that all three apps share. To build and test it on its own:

```sh
cmake -S core -B build/core
cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

On Windows, add `-G "Visual Studio 18 2026" -A x64` to the first command, `--config Release` to the second, and
`-C Release` to the third.

If you change the C facade (`core/include/kettlelogic/c/kettlelogic.h`), run
`python3 core/tools/gen_engine_loader.py` to regenerate the macOS loader. The `engine_loader_current` test fails until you do.
If the facade's interface changed, also bump `KL_ENGINE_ABI_VERSION` in that header (see [docs/updates.md](docs/updates.md)).

## How it's organized

```
core/          C++20 engine: recipe model, calculations, built-in data, file handling (no UI code)
apps/macos/    SwiftUI app
apps/windows/  WinUI 3 app (C++/WinRT)
apps/linux/    GTK4 and libadwaita app
tools/         update_manifest.py, which writes the update list
docs/          design notes and the release guide
build/         build output (not committed)
```

The design notes in [docs/](docs/) explain how things work:

- [architecture.md](docs/architecture.md): layers, platform stacks, and threading
- [units.md](docs/units.md): exact quantities and rounding rules
- [commands.md](docs/commands.md): the undo and redo model
- [model.md](docs/model.md): the recipe model
- [calc.md](docs/calc.md): how gravity, ABV, IBU, color, and strike water are calculated
- [formats/beerjson-profile.md](docs/formats/beerjson-profile.md) and [formats/beerxml-import.md](docs/formats/beerxml-import.md): file formats
- [updates.md](docs/updates.md): how updates work, and how to publish a release

## Help us out

- **Found a bug, or want a feature?** [Open an issue](https://github.com/Kettle-Logic/Kettle-Logic/issues).
- **Spot a calculation that looks off?** Compare it with your brew notes or a reference, and tell us what differs.
- **Want to write code?** Fork the repository, make your change, and open a pull request.

## License

Kettle Logic is released under the [MIT License](LICENSE). The vendored BeerJSON schema in `core/schemas/beerjson` keeps
its own license, in that folder.
