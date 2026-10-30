import Foundation
import Security

/// Checks that a downloaded file is validly signed by the same developer as the running app. A release is signed with
/// the team's certificate, so an update must be signed by that team too. A development build has no team (it is signed
/// ad hoc), and then an update must have no team either. The hash in the manifest is checked separately.
enum CodeSignature {
    /// Throws unless the file at `url` is validly signed, and by the team that signed the running app.
    static func requireSameDeveloper(_ url: URL) throws {
        var code: SecStaticCode?
        guard SecStaticCodeCreateWithPath(url as CFURL, [], &code) == errSecSuccess, let code else {
            throw UpdateError.unsigned(url.lastPathComponent)
        }
        guard SecStaticCodeCheckValidity(code, [], nil) == errSecSuccess else {
            throw UpdateError.unsigned(url.lastPathComponent)
        }
        guard teamIdentifier(of: code) == runningTeamIdentifier() else {
            throw UpdateError.differentDeveloper(url.lastPathComponent)
        }
    }

    /// The team that signed the running app, or nil for a build without one.
    static func runningTeamIdentifier() -> String? {
        var running: SecCode?
        guard SecCodeCopySelf([], &running) == errSecSuccess, let running else { return nil }
        var code: SecStaticCode?
        guard SecCodeCopyStaticCode(running, [], &code) == errSecSuccess, let code else { return nil }
        return teamIdentifier(of: code)
    }

    private static func teamIdentifier(of code: SecStaticCode) -> String? {
        var information: CFDictionary?
        guard SecCodeCopySigningInformation(code, SecCSFlags(rawValue: kSecCSSigningInformation), &information)
                == errSecSuccess,
              let fields = information as? [String: Any]
        else { return nil }
        return fields[kSecCodeInfoTeamIdentifier as String] as? String
    }
}
