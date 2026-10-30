import CKettleLogic
import Foundation

/// The engine the app is using.
struct LoadedEngine: Equatable {
    let version: SemanticVersion
    /// Whether this is the copy bundled with the app, rather than one an update installed.
    let isBundled: Bool
}

/// Loads the engine before anything else uses it. The candidates are the copies that updates installed, and the copy
/// bundled with the app. The newest that loads is used, and a copy that fails is skipped, so a bad update cannot stop
/// the app. The bundled copy is the last choice, so the app always has an engine. See docs/updates.md.
@MainActor
enum EngineLoader {
    /// The interface number this app was built against. A copy of the engine with another number is skipped.
    static let interface = Int(KL_ENGINE_ABI_VERSION)

    /// The engine in use, once `load()` succeeds.
    private(set) static var loaded: LoadedEngine?

    /// Copies that were tried and did not load, with the reason. The update window lists them.
    private(set) static var skipped: [String] = []

    private struct Candidate {
        /// The version, for choosing the order. The engine reports its own version once it loads.
        let version: SemanticVersion
        let url: URL
        let isBundled: Bool
    }

    /// Loads the engine. Returns nil on success. Otherwise returns a message for the user, and the app should not start.
    static func load() -> String? {
        var failures: [String] = []
        for candidate in candidates() {
            var error: UnsafeMutablePointer<CChar>?
            if kl_engine_load(candidate.url.path, &error) == 0 {
                let version = SemanticVersion(String(cString: kl_engine_version())) ?? .zero
                loaded = LoadedEngine(version: version, isBundled: candidate.isBundled)
                skipped = failures
                // Installed copies older than the one in use are superseded, and never picked again.
                UpdateStorage.removeInstalledEngines(olderThan: version)
                return nil
            }
            let reason = error.map { String(cString: $0) } ?? "the engine could not be loaded"
            free(error)
            failures.append("Engine \(candidate.version) (\(candidate.url.lastPathComponent)): \(reason)")
        }
        skipped = failures
        return failures.isEmpty
            ? "No engine was found in the app."
            : "No engine could be loaded.\n" + failures.joined(separator: "\n")
    }

    /// The copies to try, newest first. An installed copy wins a tie, because it was installed on purpose. The copy in
    /// the build folder is used only when the app has no bundle, so it is never the one an update replaces.
    private static func candidates() -> [Candidate] {
        var list = UpdateStorage.installedEngines().map {
            Candidate(version: $0.version, url: $0.url, isBundled: false)
        }
        if let url = Bundle.main.privateFrameworksURL?.appending(path: UpdateStorage.engineFileName),
           FileManager.default.fileExists(atPath: url.path) {
            let text = Bundle.main.object(forInfoDictionaryKey: "KLEngineVersion") as? String
            list.append(Candidate(version: text.flatMap(SemanticVersion.init) ?? .zero, url: url, isBundled: true))
        }
        if let path = kl_build_engine_path() {
            list.append(Candidate(version: .zero, url: URL(fileURLWithPath: String(cString: path)), isBundled: true))
        }
        return list.sorted { first, second in
            first.version != second.version ? first.version > second.version : !first.isBundled && second.isBundled
        }
    }
}
