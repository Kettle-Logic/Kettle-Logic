# Architecture

## Layers

```
┌──────────────────────────────────────────────────────────────┐
│ Front ends (one per platform, no domain math)                │
│   macOS: SwiftUI          Windows: WinUI 3        Linux: GTK4│
└───────────────┬──────────────────┬────────────────────┬──────┘
                │ C ABI            │ direct C++        │ direct C++
┌───────────────▼──────────────────▼────────────────────▼──────┐
│ kettlelogic_c (thin C facade, macOS only needs it in practice) │
├──────────────────────────────────────────────────────────────┤
│ core (C++20)                                                 │
│   model/      Recipe, Fermentable, Hop, Yeast, Mash, Water,  │
│               Style, Equipment, Batch (stable IDs)           │
│   units/      exact Quantity, unit table, decimal parsing    │
│   calc/       OG/FG/ABV, IBU, SRM, water chemistry, mash     │
│   catalog/    built-in data: ingredients, systems, recipes,  │
│               glossary, tool forms, option lists             │
│   command/    CommandStack<Doc>, CompositeCommand           │
│   io/         BeerJSON reader/writer, BeerXML reader/writer  │
│   store/      SQLite persistence                             │
└──────────────────────────────────────────────────────────────┘
```

### Rules

1. **Core has no UI, no threads it owns, and no platform APIs.** It compiles the same everywhere.
2. **Front ends never compute domain values.** They send commands and render the snapshots they receive.
3. **Every mutation goes through `CommandStack`.** UI edits, imports, and scripts alike, so undo covers everything.
4. **Built-in data lives in the core.** Ingredient catalog, brewing systems, starter recipes, fermentation profiles, glossary, tool forms, and the option lists are tables in `core/src/catalog`, read by every front end (Swift gets them as JSON through the C facade). A front end keeps no copy, so the platforms cannot drift apart.
5. **Entities are referenced by stable ID, never by pointer or index.** IDs survive undo, redo, file round-trips, and future sync.

## Platform front ends

### macOS: SwiftUI (deployment target macOS 26)

Use the standard components and let them adopt the current design language (Liquid Glass) automatically. Don't restyle them.

| Need | Native element |
|---|---|
| Recipe / ingredient / style navigation | `NavigationSplitView` with a sidebar `List` |
| Ingredient bill, sortable columns | `Table` (not a hand-built grid) |
| Edit the selected item | `.inspector(isPresented:)` |
| Search | `.searchable` |
| Toolbar actions | `.toolbar` with `ToolbarItem` / `ToolbarSpacer` |
| Undo / redo | `CommandGroup(replacing: .undoRedo)` wired to the core `CommandStack`, labeled with `label()` |
| Preferences (units, water defaults) | `Settings` scene with `Form` and `LabeledContent` |
| Numeric entry with units | `TextField` with a `FormatStyle` for the unit, parsed to exact decimal |

AppKit is only for gaps SwiftUI doesn't cover well, such as very large data grids or custom text layout. Keep those isolated.

Bridging: Swift imports the core through the C facade. Keep the facade as the only Swift-visible surface so the core's C++ types never leak into SwiftUI code.

### Windows: WinUI 3 with C++/WinRT

| Need | Native element |
|---|---|
| Shell and navigation | `NavigationView` |
| Actions | `CommandBar` with `AppBarButton` |
| Ingredient lists | `ListView` / `ListViewItem` templates; `DataGrid` from the Community Toolkit if a grid is needed |
| Backdrop | `SystemBackdrop` (Mica for the main window) |
| Undo / redo | `CommandBar` buttons bound to the core stack; keyboard accelerators through `KeyboardAccelerator` |
| Dialogs | `ContentDialog` |

C++/WinRT lets the Windows front end link the core directly, so no C facade is needed there. C# with WinUI 3 is a viable alternative if XAML tooling matters more than one language across the stack.

### Linux: GTK4 + libadwaita

| Need | Native element |
|---|---|
| Shell | `AdwApplication` with `AdwApplicationWindow` |
| Navigation | `AdwNavigationSplitView`, collapsing in a narrow window |
| Actions | `AdwHeaderBar` buttons, and a main menu (`GMenu`) with `GAction`s and accelerators |
| Forms | `AdwPreferencesGroup` of `AdwActionRow`s, with `GtkEntry`, `GtkDropDown`, `GtkSwitch` |
| Ingredient lists | rows in an `AdwPreferencesGroup`; a recipe's tabs are rows of `GtkToggleButton`s |
| Dialogs | `AdwDialog` and `AdwAlertDialog` |
| Backdrop and theme | the system's, through libadwaita; the app adds one CSS rule |
| Undo / redo | `GActions` with accelerators bound to the core stack |

Use libadwaita's C API from C++ directly. gtkmm does not wrap libadwaita. The portable half of the Windows app
(`apps/windows/logic`) holds the app's state and settings and has no UI, so the Linux app reuses it and writes only the
screens. See [apps/linux/README.md](../apps/linux/README.md).

## Threading

- The core is **not thread-safe** for concurrent mutation. Each document is owned by one thread at a time.
- Calculations are pure functions over snapshots. The front end may run them on a background thread and publish results to the UI thread.
- Persistence runs off the UI thread. The UI writes through commands and receives a completion event.
- Recalculation after an edit should be synchronous for small recipes; it's cheap. Measure before adding threads.

## Why this is faster than BeerSmith

BeerSmith recalculates and redraws a lot on the UI thread. Here the calc engine is native code with no interpreter, recalculation is a pure function over a snapshot, and each front end renders only the fields that changed.

## Decisions

| # | Decision | Reason |
|---|---|---|
| 1 | C++20 core | Your strongest language; mature tooling on all three platforms |
| 2 | Boost.Multiprecision `cpp_rational` for exact quantities | Exact arithmetic with no overflow limit; header-only; works on MSVC (which lacks `__int128`) |
| 3 | SI canonical storage (kg, L, °C) | One comparable representation; conversions are exact (see [units.md](units.md)) |
| 4 | `CommandStack` for every edit | Uniform undo/redo, a future sync log, and testable domain logic |
| 5 | BeerJSON as the primary format, validated against the vendored upstream schema | Open standard with a schema; BeerXML is handled as an import/export adapter. Kettle Logic data lives in a sidecar file because upstream forbids extra properties |
| 6 | C facade only for Swift | Keeps the Swift bridge simple; Windows and Linux link C++ directly |
| 7 | SQLite for storage | Single-file, portable across all three platforms |
| 8 | macOS loads the engine at runtime, through a loader generated from the C header | The engine updates without the app. A copy that fails to load falls back to the one in the app. See [updates.md](updates.md) |

## Open questions

- ~~Choose between C++/WinRT and C# for the Windows front end.~~ Decided: C++/WinRT, with the screens built in code (see apps/windows/README.md).
- Decide the minimum macOS version (this spec assumes 26 for Liquid Glass).
