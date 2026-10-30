import Foundation

/// Which installs the user chose from an offer.
enum UpdateChoice: Equatable {
    case app
    case engine
    case both
}

/// Checks GitHub for updates, and installs the ones the user picks. The window (`UpdateView`) shows its state.
@MainActor
@Observable
final class UpdateCenter {
    enum Phase: Equatable {
        case idle
        case checking
        case upToDate
        case offered(UpdateOffer)
        case installing(String)
        /// An engine was installed, but this build cannot reopen itself, so the user restarts it.
        case restartNeeded
        case failed(String)
    }

    private(set) var phase: Phase = .idle

    /// Whether the update window is showing.
    var isPresented = false

    /// The manifest's address, from the app's Info.plist (`KLUpdateManifestURL`). Empty in a build with no address.
    static var manifestURL: URL? {
        (Bundle.main.object(forInfoDictionaryKey: "KLUpdateManifestURL") as? String).flatMap { text in
            text.isEmpty ? nil : URL(string: text)
        }
    }

    /// Checks for updates. An automatic check, at launch, shows the window only when there is something to install,
    /// and fails quietly. A check the user asked for always shows the window.
    func check(automatically: Bool) async {
        guard let url = Self.manifestURL else {
            if !automatically {
                phase = .failed(UpdateError.notConfigured.localizedDescription)
                isPresented = true
            }
            return
        }
        if !automatically {
            isPresented = true
        }
        phase = .checking
        do {
            let manifest = try await Self.fetchManifest(url)
            guard let appVersion = SemanticVersion(Self.currentAppVersion) else {
                throw UpdateError.badVersion(Self.currentAppVersion)
            }
            let offer = UpdateOffer(
                manifest: manifest,
                appVersion: appVersion,
                engineVersion: EngineLoader.loaded?.version ?? .zero,
                interface: EngineLoader.interface
            )
            if let offer {
                phase = .offered(offer)
                isPresented = true
            } else {
                phase = .upToDate
                if automatically {
                    isPresented = false
                }
            }
        } catch {
            if automatically {
                phase = .idle
            } else {
                phase = .failed(error.localizedDescription)
            }
        }
    }

    /// Downloads, checks, and installs what `choice` names. The engine is installed before the app, so the new app
    /// finds it. Then the app restarts, so the engine that was installed is the one that loads.
    func install(_ choice: UpdateChoice) async {
        guard case .offered(let offer) = phase else { return }
        do {
            let folder = try UpdateStorage.freshStagingFolder()
            let engine = switch choice {
            case .app: nil as EngineRelease?
            case .engine: offer.engine
            case .both: offer.engineWithApp
            }
            let app = choice == .engine ? nil : offer.app

            if app != nil, !UpdateInstaller.canRelaunch || !UpdateInstaller.canReplaceRunningApp {
                let folderPath = Bundle.main.bundleURL.deletingLastPathComponent().path
                throw UpdateError.notWritable(folderPath)
            }

            if let engine {
                phase = .installing("Downloading the engine")
                let file = try await UpdateInstaller.download(engine.url, sha256: engine.sha256, into: folder)
                phase = .installing("Checking the engine")
                try CodeSignature.requireSameDeveloper(file)
                guard let version = SemanticVersion(engine.version) else {
                    throw UpdateError.badVersion(engine.version)
                }
                try UpdateStorage.installEngine(file, version: version)
            }

            if let app {
                phase = .installing("Downloading Kettle Logic")
                let zip = try await UpdateInstaller.download(app.url, sha256: app.sha256, into: folder)
                phase = .installing("Unpacking Kettle Logic")
                let unpackedApp = try await Task.detached {
                    try UpdateInstaller.unpackApp(from: zip, into: folder)
                }.value
                phase = .installing("Checking Kettle Logic")
                try CodeSignature.requireSameDeveloper(unpackedApp)
                try UpdateInstaller.relaunch(replacingWith: unpackedApp)
            } else if UpdateInstaller.canRelaunch {
                try UpdateInstaller.relaunch(replacingWith: nil)
            } else {
                phase = .restartNeeded
            }
        } catch {
            phase = .failed(error.localizedDescription)
        }
    }

    /// Dismisses the window. A finished or failed attempt is cleared, so the next check starts fresh.
    func dismiss() {
        isPresented = false
        if case .failed = phase { phase = .idle }
    }

    private static var currentAppVersion: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
    }

    private static func fetchManifest(_ url: URL) async throws -> UpdateManifest {
        var request = URLRequest(url: url)
        request.timeoutInterval = 20
        request.cachePolicy = .reloadIgnoringLocalCacheData
        let (data, response) = try await URLSession.shared.data(for: request)
        guard (response as? HTTPURLResponse)?.statusCode == 200 else { throw UpdateError.manifest }
        guard let manifest = try? JSONDecoder().decode(UpdateManifest.self, from: data) else {
            throw UpdateError.manifest
        }
        guard manifest.schema <= UpdateManifest.supportedSchema else { throw UpdateError.newerManifest }
        return manifest
    }
}
