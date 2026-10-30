import Foundation

/// The folder where new recipes are saved: `~/KettleLogic/Recipes` unless the user chose
/// another one in the settings. It is created the first time it is needed. A document saved
/// there is named after its first recipe, so renaming the recipe renames the file.
enum RecipeLibrary {
    static var folder: URL {
        let chosen = UserDefaults.standard.string(forKey: Preferences.recipeFolderKey) ?? ""
        return chosen.isEmpty ? defaultFolder : URL(fileURLWithPath: chosen, isDirectory: true)
    }

    /// `~/KettleLogic/Recipes`, the folder used until the user chooses another.
    static var defaultFolder: URL {
        FileManager.default.homeDirectoryForCurrentUser
            .appending(path: "KettleLogic/Recipes", directoryHint: .isDirectory)
    }

    /// The folder, created if it does not exist yet.
    static func ensureFolder() throws -> URL {
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        return folder
    }

    /// The recipe files in the folder, by name. A sidecar or a backup copy is not a recipe of its own.
    /// The folder is not created here, so a missing folder has no files.
    static func recipeFiles() -> [URL] {
        let companions = ["kettlelogic.json", "previous.json", "autosave.json"]
        let files = (try? FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)) ?? []
        return files
            .filter { url in
                let name = url.lastPathComponent
                return url.pathExtension == "json" && !companions.contains { name.hasSuffix(".\($0)") }
            }
            .sorted { $0.lastPathComponent.localizedStandardCompare($1.lastPathComponent) == .orderedAscending }
    }

    /// Whether a file is in the recipe folder, so the app may rename and remove it.
    static func contains(_ url: URL) -> Bool {
        url.deletingLastPathComponent().standardizedFileURL == folder.standardizedFileURL
    }

    /// A file name for a recipe called `name`, that is not already taken: "Pale Ale.json",
    /// then "Pale Ale 2.json", and so on. `current` is the file being renamed, if any.
    static func fileURL(named name: String, keeping current: URL? = nil) throws -> URL {
        let folder = try ensureFolder()
        let base = safeFileName(name)
        var candidate = folder.appending(path: "\(base).json")
        var number = 2
        while FileManager.default.fileExists(atPath: candidate.path), candidate != current {
            candidate = folder.appending(path: "\(base) \(number).json")
            number += 1
        }
        return candidate
    }

    /// The name a recipe's file is given, without its extension. Characters that cannot be in
    /// a file name are replaced, and a blank name becomes "Recipe".
    static func safeFileName(_ name: String) -> String {
        let cleaned = name
            .components(separatedBy: CharacterSet(charactersIn: "/:\\"))
            .joined(separator: "-")
            .trimmingCharacters(in: .whitespacesAndNewlines)
        return cleaned.isEmpty ? "Recipe" : cleaned
    }

    /// Removes a document's BeerJSON file and its sidecar.
    static func removeFiles(of url: URL) {
        let stem = url.deletingPathExtension().lastPathComponent
        let sidecar = url.deletingLastPathComponent().appending(path: "\(stem).kettlelogic.json")
        try? FileManager.default.removeItem(at: url)
        try? FileManager.default.removeItem(at: sidecar)
    }
}
