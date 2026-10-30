import Foundation

/// A release version, such as "0.2.0". Only numbers separated by dots are supported, so there are no pre-release tags.
struct SemanticVersion: Comparable, Hashable, CustomStringConvertible, Sendable {
    private let numbers: [Int]

    init?(_ text: String) {
        let parts = text.split(separator: ".", omittingEmptySubsequences: false).compactMap { Int($0) }
        guard !parts.isEmpty, parts.count <= 3, parts.allSatisfy({ $0 >= 0 }),
              parts.count == text.split(separator: ".", omittingEmptySubsequences: false).count
        else { return nil }
        numbers = parts + Array(repeating: 0, count: 3 - parts.count)
    }

    static func < (lhs: Self, rhs: Self) -> Bool {
        lhs.numbers.lexicographicallyPrecedes(rhs.numbers)
    }

    private init(numbers: [Int]) {
        self.numbers = numbers
    }

    /// The version used when a version is missing or unreadable, so it sorts below every real one.
    static let zero = SemanticVersion(numbers: [0, 0, 0])

    var description: String { numbers.map(String.init).joined(separator: ".") }
}

/// The `updates.json` published with each release. It lists the newest app and the newest engine, each with the
/// address and SHA-256 of its file. See docs/updates.md for the format.
struct UpdateManifest: Decodable, Equatable {
    /// The layout this app reads. A manifest with a newer schema is refused rather than misread.
    static let supportedSchema = 1

    let schema: Int
    let app: AppRelease?
    let engine: EngineRelease?
}

/// The newest app: a zip of `KettleLogic.app`. The app bundles its own engine, which has interface `engineAbi`.
struct AppRelease: Decodable, Equatable {
    let version: String
    let engineAbi: Int
    let url: URL
    let sha256: String
}

/// The newest engine: the engine library. It works only with an app whose interface number is `abi`.
struct EngineRelease: Decodable, Equatable {
    let version: String
    let abi: Int
    let url: URL
    let sha256: String
}

/// The updates that apply to the copy of the app that is running, and the installs that can be offered.
struct UpdateOffer: Equatable {
    /// A newer app, if the manifest has one.
    let app: AppRelease?
    /// A newer engine this app can load, if the manifest has one.
    let engine: EngineRelease?
    /// The engine that goes with `app`. It is set when the new app's interface matches a newer engine, so the two
    /// install together.
    let engineWithApp: EngineRelease?

    /// The offer for this copy of the app. `appVersion` is the app's own version, and `engineVersion` is the version
    /// of the engine it loaded. `interface` is the interface number this app was built with.
    init?(manifest: UpdateManifest, appVersion: SemanticVersion, engineVersion: SemanticVersion, interface: Int) {
        let newApp = manifest.app.flatMap { release -> AppRelease? in
            guard let version = SemanticVersion(release.version), version > appVersion else { return nil }
            return release
        }
        let newEngine = manifest.engine.flatMap { release -> EngineRelease? in
            guard let version = SemanticVersion(release.version), version > engineVersion,
                  release.abi == interface
            else { return nil }
            return release
        }
        let engineForApp = newApp.flatMap { app in
            manifest.engine.flatMap { release -> EngineRelease? in
                guard let version = SemanticVersion(release.version), version > engineVersion,
                      release.abi == app.engineAbi
                else { return nil }
                return release
            }
        }

        app = newApp
        engine = newEngine
        engineWithApp = engineForApp
        if app == nil && engine == nil { return nil }
    }
}
