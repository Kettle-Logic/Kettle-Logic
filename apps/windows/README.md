# Windows front end (WinUI 3 with C++/WinRT)

Status: written to match the macOS app feature for feature. **The WinUI project has not been compiled or run yet**,
because it was written on a Mac. The portable half, which holds everything but the controls, is built and tested.
Read [What has been checked](#what-has-been-checked) before relying on any of it.

## What it is

The same app as `apps/macos`, built from native WinUI 3 controls:

| Need | Control |
|---|---|
| Menus (File, Edit, View, Tools, Unit Tools, Help) | `MenuBar` with `KeyboardAccelerator`s |
| Recipes, folders, and the ingredient directory | `NavigationView`, with drag and drop to file a recipe |
| Recipe actions: Undo, Redo, Save, Share, Duplicate, Delete | `CommandBar` with `AppBarButton`s |
| Recipe tabs | `SelectorBar` |
| Forms | `Border` cards of rows with `TextBox`, `ComboBox`, `ToggleSwitch`, `CalendarDatePicker` |
| Editors, settings, the new-recipe wizard, errors | `ContentDialog` |
| Calculators, brew day, help | a `Window` each |
| Help pages | `WebView2` |
| Backdrop | `MicaBackdrop` |

The engine is linked directly, as [docs/architecture.md](../../docs/architecture.md) specifies. The C facade is not used.

## Layout

```
apps/windows/
  CMakeLists.txt        builds the engine and the portable logic, and their tests
  build.ps1             builds everything: CMake, then MSBuild
  KettleLogic.sln
  logic/                portable C++20, no UI and no platform API. Builds and is tested on any platform.
    include/kettlelogic/app/
      engine.hpp          RecipeDocument: the core's Document and CommandStack, with every edit and calculation
      document_store.hpp  the app's state: open documents, selection, autosave, errors
      planning.hpp        what a recipe keeps for its yeast, water, mash, and brew sessions (JSON, shared with the Mac)
      catalog.hpp, equipment.hpp, templates.hpp, tool_specs.hpp, glossary.hpp   forward to the core's built-in data (core/src/catalog)
      ingredient_draft.hpp, recipe_builder.hpp, water_profiles.hpp, readings_csv.hpp, ...
    src/  tests/
  KettleLogic/          the WinUI 3 project
    Ui.h, Ui.cpp          a small kit that builds forms from standard controls
    MainWindow.*          menus, navigation pane, toolbar, and the other windows
    RecipeView.cpp        the recipe screen and its tabs
    PlanningTabs.cpp, JournalTab.cpp, ProcedureTab.cpp
    Dialogs.cpp           ingredient editor, equipment editor, wizard, settings, first launch
    Directory.cpp, ToolView.cpp, AuxWindows.cpp, Platform.cpp
    Help/                 the user guide, with Windows shortcuts
```

The built-in data is not in this app: the ingredient catalog, brewing systems, starter recipes, fermentation profiles,
glossary, tool forms, and option lists live in the engine (`core/src/catalog`), and both apps read them from there, so they
offer the same ingredients, systems, styles, and tools.

## Build

Requirements:

- Visual Studio 2026 with "Desktop development with C++" and "Windows application
  development". `build.ps1` uses the Visual Studio 2026 generator (MSVC toolset v145).
- CMake 3.25 or newer on the path (Developer PowerShell has it with Visual Studio), and `nuget.exe` on the path
  ([nuget.org/downloads](https://www.nuget.org/downloads)).
- Boost 1.80 or newer, headers only: unzip the `boost\` folder from the release archive at boost.io (for example to
  `C:\boost`), or install `boost-headers` with vcpkg. Pass `-BoostRoot` the folder that holds `boost\`, or set `BOOST_ROOT`.
  To build from the Visual Studio IDE, which does not see a `BOOST_ROOT` set in a shell, create
  `KettleLogic\KettleLogic.Local.props` (it is not in git) that sets `<BoostRoot>C:\boost</BoostRoot>`.

Then, from a Developer PowerShell:

```powershell
.\apps\windows\build.ps1 -Configuration Release -BoostRoot C:\vcpkg\installed\x64-windows\include
```

It builds the engine and the portable logic with CMake and runs their tests, restores the Windows App SDK and C++/WinRT
with NuGet, and builds `KettleLogic.exe` into `build\windows-app\x64\Release`. The app is unpackaged and self-contained,
so that folder runs as it is.

To work in Visual Studio, run `build.ps1` once, then open `KettleLogic.sln`.

To build and test only the portable logic, on any platform:

```sh
cmake -S apps/windows -B build/windows-logic
cmake --build build/windows-logic
ctest --test-dir build/windows-logic --output-on-failure
```

## Smoke test

`tests\smoke.ps1` is the Windows counterpart of the Linux smoke test. It starts the built app on an empty set of files
(it sets `KETTLELOGIC_HOME`, so your settings and recipes are left alone), then drives every screen through UI
Automation, as an assistive tool would: the first-launch choice, the new-recipe wizard, every tab of a recipe in full
mode, simple mode, and with flat tabs, the ingredient editors, the procedure and brew-day window, every calculator and
converter, the help window, the settings, the style editor, the ingredient directory, and undo, duplicate, and delete.
It reports a crash, a hang, a missing control, or a window that did not open, and ends with
`N screens visited, M problems`. It needs a desktop session and takes a couple of minutes.

```powershell
.\apps\windows\tests\smoke.ps1                          # the Debug build that build.ps1 makes
.\apps\windows\tests\smoke.ps1 -Shots $env:TEMP\shots    # the folder is optional: it saves a PNG per screen
```

Set `KETTLELOGIC_SMOKE_DEBUG=1` to list what was on screen at each problem. Do not use the mouse while it runs, because it
falls back to a click for a control that cannot be pressed any other way.

## Packaging

`make-installer.ps1` builds the app with `build.ps1` (unless `-SkipBuild` is given), then compiles
`installer\KettleLogic.iss` with Inno Setup 7 into `dist\`. The steps, the signing option, and the update entry are in
[Packaging: Windows (Inno Setup installer)](../../README.md#packaging-windows-inno-setup-installer) in the top-level README.

## What has been checked

- **Checked here, on macOS:** the engine adapter, the app's data and state, the planning JSON, the recipe builder, the
  tools, the CSV, the Markdown and PDF writers. `logic/tests/main.cpp` runs 1,176 checks, among them: every one of the
  twelve style templates builds to its target gravity and bitterness in both unit systems; every tool in the Tools and
  Unit Tools menus runs with its defaults and returns every result its form shows; planning JSON written by the Mac app
  reads back, and the app's own JSON reads back; autosave renames a file with its recipe; undo covers a recipe built from
  a style in one step. The library compiles clean with `-Wall -Wextra -Wpedantic -Werror`.
- **Built and run on Windows:** `KettleLogic/` builds with Visual Studio 2026 (MSVC 14.51, Windows SDK 10.0.26100, Windows
  App SDK 1.6) in Debug, and `tests\smoke.ps1` walks every screen with no crash and no missing control. The engine and the
  app logic pass their tests under MSVC with `/W4 /WX`.
- **Not checked:** the Release configuration, ARM64, the installer, and the screens' details against
  the macOS app. The smoke test shows that every screen opens and takes input, not that every number and label is right.
  Compare each screen with the macOS app.

## How it differs from the macOS app

- **Shortcuts use Ctrl** in place of Command, and Redo is Ctrl+Y. Settings is a dialog, from the gear at the bottom of the
  navigation pane, File ▸ Settings, or Ctrl+,. Help is F1. Ctrl+1 to Ctrl+9 go to a recipe tab, or to a group of tabs
  when the tabs are grouped. The user guide in `KettleLogic/Help` is the macOS guide with these shortcuts.
- **Fields commit on Enter or when focus leaves**, as the Mac's do on Return, and the screen is then drawn again from the
  engine's results. A short delay before the redraw keeps a click on a button from being lost to it; if a click right after
  typing does nothing, click again.
- **PDF export** is the engine's recipe PDF (`core/src/io/recipe_pdf.cpp`), the same file the Mac and Linux apps write. It is separate from the brew sheet PDF.
- **The Acid section of the Mash pH Calculator** runs on the inputs of the whole tool, so it works on its own. On the Mac it
  runs on its own fields only, and reports that it needs grain.
- **Where files live:** recipes in `%USERPROFILE%\KettleLogic\Recipes`, settings and the user's libraries in
  `%LOCALAPPDATA%\KettleLogic`. The recipe files are the same BeerJSON and sidecar files the Mac writes, so a folder can
  be shared between the two.
- **A recipe name that Windows cannot use as a file name** has `* ? " < > |` replaced as well as `/ : \`, and a trailing dot
  dropped.

## Open decisions

- C++/WinRT was chosen, as the plan's default. The screens are built in code and not in XAML, so only `App.xaml` goes
  through the XAML compiler. That keeps the project small and the screens in step with the macOS views, at
  the cost of Visual Studio's XAML designer.
- No application icon is included yet.
