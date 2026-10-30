# Contributing to Kettle Logic

Thanks for your interest in Kettle Logic. This page covers how to propose a change, how the code is organized, and what to
check before you send one. The [README](README.md) explains how to build each part of the project, and
[docs/architecture.md](docs/architecture.md) explains why it is built that way. Read the architecture rules before you
change code.

## Before you start

- For a small fix, such as a typo, a broken link, or a clear bug, open a pull request directly.
- For a new feature, a change to the recipe file format, or a change to a calculation, open an issue first. These change
  behavior that users and other front ends depend on, so it is worth agreeing on the approach before you write the code.
- Check the existing docs. Most design decisions are recorded in `docs/`, and the calculations in
  [docs/calc.md](docs/calc.md) and the units in [docs/units.md](docs/units.md) have rules that the tests enforce.

## Project layout

The engine (`core/`) is C++20 and has no UI code. The front ends in `apps/` are native to each platform: SwiftUI for macOS,
WinUI 3 for Windows, and GTK4 with libadwaita for Linux. The layout is listed in the README's
[Layout](README.md#layout) section.

Work in the layer where the change belongs:

- A calculation, a unit, a file format, or a piece of built-in data goes in `core/`.
- A screen, menu, or platform behavior goes in the matching `apps/` folder.
- Do not put a domain calculation in a front end. Front ends send commands to the engine and render what it returns.

## Architecture rules

These rules come from [docs/architecture.md](docs/architecture.md). A pull request that breaks one needs a reason in its
description.

1. **The core has no UI, owns no threads, and calls no platform APIs.** It must compile the same on every platform.
2. **Front ends never compute domain values.** They send commands and render the snapshots they receive.
3. **Every change to a recipe goes through `CommandStack`.** Edits from the UI, imports, and scripts all use it, so undo
   covers them.
4. **Built-in data lives in `core/src/catalog`.** Each front end reads it from the engine. Do not copy it into an app.
5. **Refer to entities by their stable ID,** never by pointer or index. IDs survive undo, redo, file round-trips, and sync.

Keep quantities exact. Store values in SI units and round only when you display them, as described in
[docs/units.md](docs/units.md). Do not use `double` for a value that a user or a file will see.

## Building and testing

Build and test the engine before you open a pull request:

```sh
cmake -S core -B build/core
cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

Then build the app you touched, using the steps in the README:

- macOS: `apps/macos/make-app.sh`, or open `apps/macos/KettleLogic.xcodeproj` in Xcode.
- Linux: `cmake -S apps/linux -B build/linux`, then `cmake --build build/linux`. Run the smoke test as described in
  [apps/linux/README.md](apps/linux/README.md) when you change a screen.
- Windows: `apps\windows\build.ps1` from a Developer PowerShell. Use `-SkipTests` only if you have already run the tests.

Some steps need extra care:

- **C facade.** If you change `core/include/kettlelogic/c/kettlelogic.h`, run `python3 core/tools/gen_engine_loader.py` to
  regenerate `apps/macos/Sources/CKettleLogic/engine_loader.c`. The `engine_loader_current` test fails until you do. If the
  facade's interface changed, also bump `KL_ENGINE_ABI_VERSION` in the same header. See [docs/updates.md](docs/updates.md).
- **Versions.** Do not bump a version number in a feature pull request. Versions are changed in a release, and the steps
  are in the README's [Packaging: macOS](README.md#packaging-macos-dmg) section.
- **Schema.** `core/schemas/beerjson` is a vendored copy of the upstream BeerJSON schema, pinned to a commit. Do not edit
  it by hand. Its own README explains how it is updated.
- **Generated files.** Everything under `build/` is generated and is not committed. Do not add build output, logs, or
  screenshots to a pull request.

## Tests

- Add a test for each behavior you change or add. Engine tests live in `core/tests/`, with one `test_*.cpp` file per area.
  Add to the file that covers your area rather than creating a new one, unless the area is new.
- A bug fix should come with a test that fails without the fix.
- Calculation tests should use values you can check by hand or against a published source, and say where the expected value
  comes from.
- The engine's tests should pass on every platform. Keep platform code out of `core/`, which makes that possible.

## Code style

- Match the code around your change. Follow the naming, comment density, and idioms of the file you are editing, rather
  than a new style of your own.
- C++ code in `core/` and `apps/linux/` builds with `-Wall -Wextra -Wpedantic`. Fix every warning your change adds.
- Keep a change focused. Do not reformat code you did not otherwise touch, and do not mix a refactor with a behavior
  change in one pull request.
- Write comments that explain why, not what. A comment that repeats the code is not needed.

## Documentation

If your change alters behavior that a document describes, update that document in the same pull request. Examples are a
new BeerXML field in [docs/formats/beerxml-import.md](docs/formats/beerxml-import.md), or a new command in
[docs/commands.md](docs/commands.md). Keep the README's build steps accurate when you change a build.

## Integration testing

A feature is not merged when its own tests pass. Before a feature pull request is merged, the maintainers build it and test
it together with the rest of the project, on the platforms it touches. This is where problems between the engine, the
apps, the file formats, and the update path show up.

- Maintainers report each failure as a comment on the pull request. The comment says what they ran, the platform, and the
  result, with the output or a screenshot where it helps.
- Fix each reported failure on the same branch and push the change to the same pull request. Do not open a new one.
- Say in a reply what you changed to fix a report, or why you think the report is not a bug in your change. Either answer
  lets the maintainers re-test without guessing.
- When a fix changes behavior that a test or a document describes, update them in the same push.
- A pull request stays open until the maintainers have re-tested the fix. Expect more than one round for a large feature.
- If a failure is in code your change did not touch, say so in the pull request. It may belong in a separate issue, and
  the maintainers will decide that with you.

Keep the branch up to date with `main` while this happens. Merge or rebase it when the maintainers ask, so they test the
code that will actually land.

## Pull requests

- Keep each pull request to one change. A pull request that touches the engine and one app is fine when the app depends on
  the engine change. Split anything else.
- Describe what changed and why, and say how you tested it. List the commands you ran and the platforms you checked.
- If you could not test a platform, say so in the description. The Windows and Linux apps have not yet been run on their
  target platforms, and it helps to know which parts were checked.
- Make sure `ctest` passes and that the app you changed still builds.

## Licensing

Kettle Logic is released under the [MIT License](LICENSE). By contributing, you agree that your contribution is released
under the same license. The vendored BeerJSON schema in `core/schemas/beerjson` keeps its own license, and it is not
changed by a contribution.
