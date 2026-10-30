// swift-tools-version: 6.2
import Foundation
import PackageDescription

// The repository root, found from this file, so the package builds from any working directory.
let repoRoot = URL(fileURLWithPath: #filePath)
    .deletingLastPathComponent()  // apps/macos
    .deletingLastPathComponent()  // apps
    .deletingLastPathComponent()  // the repository
let engineHeaders = repoRoot.appending(path: "core/include").path
// Where CMake leaves the engine. KETTLELOGIC_BUILD overrides it, for another engine build.
let engineBuild = ProcessInfo.processInfo.environment["KETTLELOGIC_BUILD"]
    ?? repoRoot.appending(path: "build/core").path

// The engine is not linked into the app. It is a separate library, loaded at launch from the
// newest copy that works: an update installs one, and the app bundles one as a fallback. See
// docs/updates.md, and Sources/CKettleLogic/engine_loader.c, which is generated from the facade's header.
let package = Package(
    name: "KettleLogic",
    platforms: [.macOS(.v26)],
    products: [
        .executable(name: "KettleLogic", targets: ["KettleLogic"]),
    ],
    targets: [
        // The loader that stands in for the engine's C facade, and the facade's header as a module.
        // The header lives with the engine, so it is included from there.
        .target(
            name: "CKettleLogic",
            path: "Sources/CKettleLogic",
            cSettings: [
                // The facade's header lives with the engine, outside this package, so it is found by path.
                .unsafeFlags(["-I", engineHeaders]),
                .define("KETTLELOGIC_BUILD_ENGINE", to: "\"\(engineBuild)/libkettlelogic_engine.dylib\""),
            ]
        ),
        .executableTarget(
            name: "KettleLogic",
            dependencies: ["CKettleLogic"],
            path: "Sources/KettleLogic"
        ),
    ]
)
