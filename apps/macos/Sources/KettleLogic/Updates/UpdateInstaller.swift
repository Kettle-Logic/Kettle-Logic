import AppKit
import CryptoKit
import Foundation

/// Downloads and unpacks updates, and swaps in a new app. Nothing is installed until its hash and signature are checked.
enum UpdateInstaller {
    /// Downloads `url` into `folder`, and checks the file against the SHA-256 the manifest gives for it.
    static func download(_ url: URL, sha256 expected: String, into folder: URL) async throws -> URL {
        let (temporary, response) = try await URLSession.shared.download(from: url)
        guard (response as? HTTPURLResponse)?.statusCode == 200 else {
            throw UpdateError.download(url.lastPathComponent)
        }
        let file = folder.appending(path: url.lastPathComponent)
        try? FileManager.default.removeItem(at: file)
        try FileManager.default.moveItem(at: temporary, to: file)

        let actual = try await Task.detached { try sha256(of: file) }.value
        guard actual == expected.lowercased() else {
            try? FileManager.default.removeItem(at: file)
            throw UpdateError.hashMismatch(url.lastPathComponent)
        }
        return file
    }

    /// The SHA-256 of a file, as lowercase hex, read in chunks so a large file does not load at once.
    static func sha256(of file: URL) throws -> String {
        let handle = try FileHandle(forReadingFrom: file)
        defer { try? handle.close() }
        var hash = SHA256()
        while let chunk = try handle.read(upToCount: 1 << 20), !chunk.isEmpty {
            hash.update(data: chunk)
        }
        return hash.finalize().map { String(format: "%02x", $0) }.joined()
    }

    /// Unpacks the app from a zip, and returns the app's location. `ditto` keeps the code signature, which a plain
    /// unzip can lose.
    static func unpackApp(from zip: URL, into folder: URL) throws -> URL {
        try run("/usr/bin/ditto", ["-x", "-k", zip.path, folder.path])
        let contents = try FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)
        guard let app = contents.first(where: { $0.pathExtension == "app" }) else {
            throw UpdateError.notAnApp
        }
        return app
    }

    /// Whether this copy of the app is an app bundle that can be replaced and reopened. A build run from the shell is not.
    static var canRelaunch: Bool { Bundle.main.bundleURL.pathExtension == "app" }

    /// Whether the folder the app sits in can be written to, so the app can be replaced in it.
    static var canReplaceRunningApp: Bool {
        FileManager.default.isWritableFile(atPath: Bundle.main.bundleURL.deletingLastPathComponent().path)
    }

    /// Quits the app, and after it has quit, replaces it with `newApp` (when given) and opens it again. The app cannot
    /// replace its own bundle while it runs, so a small script does the work once this process has exited.
    @MainActor
    static func relaunch(replacingWith newApp: URL?) throws {
        let script = UpdateStorage.root.appending(path: "relaunch.sh")
        try FileManager.default.createDirectory(at: UpdateStorage.root, withIntermediateDirectories: true)
        try relaunchScript.write(to: script, atomically: true, encoding: .utf8)

        let helper = Process()
        helper.executableURL = URL(fileURLWithPath: "/bin/sh")
        helper.arguments = [
            script.path,
            String(ProcessInfo.processInfo.processIdentifier),
            Bundle.main.bundleURL.path,
            newApp?.path ?? "",
        ]
        try helper.run()
        NSApplication.shared.terminate(nil)
    }

    private static func run(_ tool: String, _ arguments: [String]) throws {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: tool)
        process.arguments = arguments
        try process.run()
        process.waitUntilExit()
        guard process.terminationStatus == 0 else { throw UpdateError.unpack }
    }

    /// Waits for the app to quit, puts the new app in place (keeping the old one until the swap has worked), and opens
    /// the app. Arguments: the app's process id, the app's path, and the new app's path or nothing.
    private static let relaunchScript = """
    #!/bin/sh
    # Kettle Logic's update helper. The app starts this just before it quits.
    pid="$1"
    app="$2"
    new="$3"
    while kill -0 "$pid" 2>/dev/null; do
        sleep 0.2
    done
    if [ -n "$new" ]; then
        rm -rf "$app.new" "$app.previous"
        if ditto "$new" "$app.new" && mv "$app" "$app.previous" && mv "$app.new" "$app"; then
            rm -rf "$app.previous"
        else
            # Put the old app back if the swap stopped partway.
            if [ -d "$app.previous" ] && [ ! -d "$app" ]; then
                mv "$app.previous" "$app"
            fi
            rm -rf "$app.new"
        fi
        # The download and the unpacked copy in the staging folder are not needed once the swap has been tried.
        rm -rf "$(dirname "$new")"
    fi
    open "$app"
    """
}

/// What an update can fail on, with a message for the user.
enum UpdateError: LocalizedError {
    case notConfigured
    case manifest
    case newerManifest
    case download(String)
    case hashMismatch(String)
    case unsigned(String)
    case differentDeveloper(String)
    case unpack
    case notAnApp
    case notWritable(String)
    case badVersion(String)

    var errorDescription: String? {
        switch self {
        case .notConfigured:
            "This build of Kettle Logic has no update address, so it cannot check for updates."
        case .manifest:
            "The list of updates could not be read. Check the connection, and try again."
        case .newerManifest:
            "The list of updates is for a newer version of Kettle Logic. Download the latest version from its GitHub page."
        case .download(let name):
            "\(name) could not be downloaded."
        case .hashMismatch(let name):
            "\(name) does not match the checksum in the update list, so it was not installed."
        case .unsigned(let name):
            "\(name) is not validly signed, so it was not installed."
        case .differentDeveloper(let name):
            "\(name) is signed by a different developer than this copy of Kettle Logic, so it was not installed."
        case .unpack:
            "The update could not be unpacked."
        case .notAnApp:
            "The update does not contain Kettle Logic."
        case .notWritable(let folder):
            "Kettle Logic cannot replace itself in \(folder). Move it to a folder you can write to, or download the update from its GitHub page."
        case .badVersion(let text):
            "The update lists a version that is not valid: \(text)."
        }
    }
}
