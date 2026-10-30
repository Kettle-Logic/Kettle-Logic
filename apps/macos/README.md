# Kettle Logic for macOS: building in Xcode

The macOS app is a SwiftUI package, `apps/macos/Package.swift`, with an Xcode project, `KettleLogic.xcodeproj`, that wraps it for signing and release. Both load the C++ engine at launch, from the shared library `libkettlelogic_engine.dylib`, through the C facade. Xcode can open either one, build it, run it, and debug it. The engine is built by CMake. The Xcode project runs CMake itself, but opening `Package.swift` does not, so that route needs the engine built first.

This guide covers day-to-day development in Xcode. For the command-line route, which also produces the app bundle, see [Building the front ends](../../README.md#building-the-front-ends) in the top-level README and `make-app.sh`.

## Requirements

- macOS 26 or newer. The package declares `.macOS(.v26)`.
- Xcode 26.2 or newer, which provides the Swift 6.2 toolchain and the macOS 26 SDK.
- Homebrew packages for the engine: `brew install boost cmake`.

## 1. Build the engine

The engine is the shared library `libkettlelogic_engine.dylib`, which CMake leaves in `build/core`. The app loads it when it starts. `Package.swift` finds it there, so build it in Terminal before you run the package. The Xcode project builds it for you, and copies it into the app.

From the repository root, in Terminal:

```sh
cmake -S core -B build/core
cmake --build build/core --target kettlelogic_engine
```

Run these once on a fresh clone, and again after every change under `core/`, when you work on `Package.swift` or run `swift run`. The `KettleLogic.xcodeproj` project does not need them, because its Build Engine phase runs the same CMake build. See [Changing the engine](#changing-the-engine) below. How updates replace the engine is in [docs/updates.md](../../docs/updates.md).

## 2. Open the package in Xcode

From the repository root:

```sh
open -a Xcode apps/macos/Package.swift
```

Or in Xcode, choose **File > Open...** and select `apps/macos/Package.swift`.

Xcode opens the package as a workspace and creates its project files under `apps/macos/.swiftpm/xcode/`. Those files are per-user state, so they are not meant to be committed.

## 3. Select the scheme and run

1. In the toolbar, set the scheme to **KettleLogic**.
2. Set the run destination to **My Mac**.
3. Press **⌘B** to build, or **⌘R** to build and run.

When the app starts from Xcode, it runs as a bare executable, the same as `swift run`. The Help window still works, because the app finds the `Help` folder in `apps/macos/Help` when it is not in a bundle.

To test the app as a Mac app, with the bundle layout that `make-app.sh` produces, build the bundle from Terminal instead:

```sh
apps/macos/make-app.sh
open apps/macos/build/KettleLogic.app
```

## Running the tests

The package has no test target yet. The engine's tests run with CTest:

```sh
cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

## Changing the engine

The app loads the engine when it starts, so after you change anything under `core/`:

1. Rebuild the engine in Terminal with the commands in step 1. Skip this when you build `KettleLogic.xcodeproj`, whose Build Engine phase does it.
2. Build and run the app again. The new engine is loaded at launch.

If you change the C facade, `core/include/kettlelogic/c/kettlelogic.h`, run `python3 core/tools/gen_engine_loader.py` to regenerate `Sources/CKettleLogic/engine_loader.c`. The Swift side calls the facade through that file, and the `engine_loader_current` test fails until it is regenerated. The header is the only place the C interface is defined. Also bump `KL_ENGINE_ABI_VERSION` in it when the interface changes.

## Where the engine is found

`Package.swift` looks for the engine in `build/core` at the repository root, and passes that path to the loader. It finds that folder from the path of the manifest file itself, so the location does not depend on the working directory. A run without an app bundle, such as Xcode running the package or `swift run`, loads the engine from there. The app bundle loads the copy in its `Contents/Frameworks` folder, or a newer copy that an update installed.

To use a different engine build, set `KETTLELOGIC_BUILD` to the folder that holds `libkettlelogic_engine.dylib`. The manifest reads this variable when the package loads, so it has to be in the environment that launches Xcode, not in the scheme. A variable exported in a shell does not reliably reach Xcode when it is opened from the Dock or Finder. Keep the default `build/core` unless you need another engine build.

## Signing

Open `KettleLogic.xcodeproj` in Xcode and build the KettleLogic scheme. The target is signed with the team set under Signing & Capabilities (automatic signing, Hardened Runtime on). A "Build Engine" run-script phase runs the CMake build first, so the engine does not need a separate step. The App Sandbox is off, because the app reads and writes recipes in `~/KettleLogic/Recipes`.

The target runs a "Embed Engine" phase that copies `libkettlelogic_engine.dylib` into the app's `Contents/Frameworks` folder, so the app finds its built-in engine there. The app icon comes from `Resources/Assets.xcassets/AppIcon.appiconset`. To distribute the app, use Product ▸ Archive, then Distribute App ▸ Developer ID, and give the exported app to `make-release.sh`. It notarizes the app and makes the DMG. See [docs/updates.md](../../docs/updates.md#publishing-a-release).

`Package.swift` and `make-app.sh` still work for command-line builds. They are separate from the Xcode project and sign ad hoc unless `SIGN_IDENTITY` is set.

## Troubleshooting

- **"No engine could be loaded" when the app starts**: the engine has not been built, or `build/core` has no `libkettlelogic_engine.dylib`. The message lists each copy that was tried and why it failed. Run step 1, then start the app again.
- **Linker errors about `kl_...` symbols**: `engine_loader.c` is not in the target, or it is out of date. Run `python3 core/tools/gen_engine_loader.py` after a header change. In Xcode, the loader is the file `Sources/CKettleLogic/engine_loader.c`, which the project compiles.
- **The C header is out of date in Xcode**: check that `Sources/CKettleLogic/include/module.modulemap` points at `core/include/kettlelogic/c/kettlelogic.h`. The path is relative to the modulemap file.
- **Xcode shows the wrong engine build**: check whether `KETTLELOGIC_BUILD` was set when Xcode was launched.
