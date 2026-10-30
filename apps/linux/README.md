# Linux front end (GTK4 and libadwaita)

Status: written to match the macOS and Windows apps feature for feature. It builds and passes its smoke test with GTK 4.24
and libadwaita 1.10 on macOS, where this was written. **It has not been run on a Linux desktop yet.** Read
[What has been checked](#what-has-been-checked) before relying on any of it.

## What it is

The same app as `apps/macos` and `apps/windows`, built from native libadwaita widgets:

| Need | Widget |
|---|---|
| Shell | `AdwApplicationWindow` with `AdwNavigationSplitView`, collapsing to one pane in a narrow window |
| Recipes, folders, the ingredient directory | a `GtkListBox` with the `navigation-sidebar` style; drag a recipe onto a folder to file it |
| Recipe actions: Undo, Redo, Save, Export, Duplicate, Delete | buttons in the content `AdwHeaderBar` |
| Main menu: New, Open, Save As, Tools, Unit Tools, Simple Mode, Settings, Help | a `GMenu` behind the header bar's menu button, with `GAction`s and accelerators |
| Recipe tabs | scrolling rows of `GtkToggleButton`s |
| Forms | `AdwPreferencesGroup`s of `AdwActionRow`s with `GtkEntry`, `GtkDropDown`, `GtkSwitch`, `GtkCalendar` |
| Editors, settings, the new-recipe wizard, errors | `AdwDialog`, `AdwAlertDialog` |
| Calculators and converters, brew day, help | a window each (`AdwWindow`) |
| Help pages | a `GtkTextView` that renders the Markdown guide itself, so the app needs no web engine |

The app's state, settings, planning, and file handling are the portable C++ in `apps/windows/logic`, which has no UI and no
platform API. This front end builds only the screens on top of it, so the three apps cannot drift apart in what they
calculate or store. The engine is linked directly, as [docs/architecture.md](../../docs/architecture.md) specifies.

## Layout

```
apps/linux/
  CMakeLists.txt   builds the engine, the portable logic, the screens, the app, and the smoke test
  src/
    ui.hpp, ui.cpp          a small kit that builds forms from standard widgets (the Windows app's Ui.* kit, in GTK)
    host.hpp                what a screen needs from its window
    main_window.*           the window, its actions, the sidebar, and the other windows
    recipe_view.cpp         the recipe screen and its tabs
    planning_tabs.cpp, journal_tab.cpp, procedure_tab.cpp
    dialogs.*               ingredient editor, equipment editor, wizard, settings, first launch
    directory.cpp, tool_view.cpp, aux_windows.*   the directory, calculators, brew day, and help
    markdown_view.*         the user guide's Markdown, drawn in a text view
    platform.*              the user's folders, file dialogs, clipboard, and keeping the display awake
  Help/                     the user guide, with this app's menu in place of Windows' navigation pane
  data/                     the desktop entry and the AppStream metadata
  tests/smoke.cpp           opens every screen and drives it
```

## Build

Requirements: GTK 4.12 or newer, libadwaita 1.5 or newer, CMake 3.25, pkg-config, a C++20 compiler, and Boost (headers).
On Debian or Ubuntu 24.04 or newer:

```sh
sudo apt install build-essential cmake pkg-config libgtk-4-dev libadwaita-1-dev libboost-dev
```

On Fedora:

```sh
sudo dnf install gcc-c++ cmake pkgconf-pkg-config gtk4-devel libadwaita-devel boost-devel
```

Then:

```sh
cmake -S apps/linux -B build/linux
cmake --build build/linux
build/linux/kettlelogic-gtk [path/to/Recipe.json ...]
```

The build copies the user guide next to the executable. `cmake --install build/linux --prefix /usr/local` puts it in
`share/kettlelogic/Help`, with the desktop entry and the metadata. Set `KETTLELOGIC_HELP` to a folder to read the guide
from somewhere else.

To run the smoke test, which opens every screen and needs a display (use `xvfb-run` on a machine without one):

```sh
cmake --build build/linux --target kettlelogic-gtk-smoke
KETTLELOGIC_SMOKE_SHOTS=/tmp/shots build/linux/kettlelogic-gtk-smoke   # the folder is optional: it saves a PNG per screen
```

## Packaging

The Flatpak is built from `flatpak/org.kettlelogic.KettleLogic.yml`, which builds this same CMake project from the
repository. The steps, and the bundle for a release, are in [Packaging: Linux (Flatpak)](../../README.md#packaging-linux-flatpak)
in the top-level README.

## What has been checked

- **Checked here, on macOS, with GTK 4.24 and libadwaita 1.10:** the app builds with no warnings from a clean folder under
  `-Wall -Wextra -Wpedantic`. `tests/smoke.cpp` starts the app on an empty home folder, makes a recipe from a style, and
  visits 61 screens: every tab of a recipe in full mode, in simple mode, and with grouped tabs, with controls changed on
  each; the five ingredient editors; the procedure and the brew-day window with a running timer; every calculator and
  converter; every page of the help window; the settings and the wizard through its three steps; every page of the
  ingredient directory with a search; and undo, redo, duplicate, and delete. It fails on a crash or on any GTK or GLib
  warning or critical, and passes. The screens were also drawn to images and looked over.
- **Not checked:** running on Linux, with Wayland or X11, or with the Adwaita icon theme, which this Mac does not have: some
  icons showed as placeholders here, but they are standard icon-theme names. Also not exercised by the test: the file
  dialogs (Open, Save As, Export, the CSV buttons, the recipe folder chooser), drag and drop of a recipe onto a folder, the
  context menus, and keeping the display awake. The application id `org.kettlelogic.KettleLogic` was chosen here, and no
  application icon is included yet.

## How it differs from the other apps

- **The menu is one main menu**, as GNOME apps have, in place of a menu bar. File actions, Tools, Unit Tools, Simple
  Mode, Settings, and Help are in it. The shortcuts are the Windows ones: Ctrl+N, Ctrl+Shift+N, Ctrl+O, Ctrl+S,
  Ctrl+Shift+S, Ctrl+Z, Ctrl+Y (or Ctrl+Shift+Z), Ctrl+Alt+M, Ctrl+, , F1, Ctrl+1 to Ctrl+9, and Ctrl+Q.
- **Fields commit on Enter or when focus leaves**, as the Windows app's do, and the screen is then drawn again from the
  engine's results, with the focus, the caret, and the scroll position put back.
- **Export replaces Share.** Linux has no share sheet, so the header bar's Export menu writes BeerXML, text, PDF, or
  BeerJSON to a file. The PDF is the engine's recipe PDF, the same file the Mac and Windows apps write.
- **The help is drawn by the app.** There is no web view, so the guide's tables are grids of labels and its links open other
  pages in the same window, or the browser.
- **Where files live:** recipes in `~/KettleLogic/Recipes`, settings and the user's libraries in
  `$XDG_DATA_HOME/KettleLogic` (`~/.local/share/KettleLogic`). The recipe files are the same BeerJSON and sidecar files the
  other apps write, so a folder can be shared between them.
- **The brew-day window keeps the display on** by asking the session to inhibit idle, when its switch is on.
