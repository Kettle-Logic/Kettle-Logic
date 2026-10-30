import Foundation

/// Where updates keep what they install, in Application Support. The app reads it at launch: the engine copies are
/// loaded by `EngineLoader`.
enum UpdateStorage {
    static let engineFileName = "libkettlelogic_engine.dylib"

    static var root: URL {
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        return support.appending(path: "KettleLogic", directoryHint: .isDirectory)
    }

    /// Engine copies that updates installed, one folder per version.
    static var enginesFolder: URL {
        root.appending(path: "Engines", directoryHint: .isDirectory)
    }

    /// Downloads wait here while they are checked. Each install starts with an empty folder.
    static var stagingFolder: URL {
        root.appending(path: "Staging", directoryHint: .isDirectory)
    }

    /// The installed engine copies, newest first.
    static func installedEngines() -> [(version: SemanticVersion, url: URL)] {
        let folders = (try? FileManager.default.contentsOfDirectory(at: enginesFolder, includingPropertiesForKeys: nil)) ?? []
        return folders
            .compactMap { folder -> (version: SemanticVersion, url: URL)? in
                guard let version = SemanticVersion(folder.lastPathComponent) else { return nil }
                let file = folder.appending(path: engineFileName)
                guard FileManager.default.fileExists(atPath: file.path) else { return nil }
                return (version, file)
            }
            .sorted { $0.version > $1.version }
    }

    /// Installs `file` as the engine for `version`. It is copied under a temporary name and then renamed, so an
    /// interrupted install never leaves a partial engine where the loader would find it.
    static func installEngine(_ file: URL, version: SemanticVersion) throws {
        let manager = FileManager.default
        let partial = enginesFolder.appending(path: ".\(version).partial", directoryHint: .isDirectory)
        let folder = enginesFolder.appending(path: version.description, directoryHint: .isDirectory)
        try? manager.removeItem(at: partial)
        try manager.createDirectory(at: partial, withIntermediateDirectories: true)
        try manager.copyItem(at: file, to: partial.appending(path: engineFileName))
        try? manager.removeItem(at: folder)
        try manager.moveItem(at: partial, to: folder)
    }

    /// Removes the installed engines older than `version`. They are superseded, and the loader would not use them.
    static func removeInstalledEngines(olderThan version: SemanticVersion) {
        for engine in installedEngines() where engine.version < version {
            try? FileManager.default.removeItem(at: engine.url.deletingLastPathComponent())
        }
    }

    /// An empty staging folder, replacing any left by an earlier attempt.
    static func freshStagingFolder() throws -> URL {
        try? FileManager.default.removeItem(at: stagingFolder)
        try FileManager.default.createDirectory(at: stagingFolder, withIntermediateDirectories: true)
        return stagingFolder
    }
}
