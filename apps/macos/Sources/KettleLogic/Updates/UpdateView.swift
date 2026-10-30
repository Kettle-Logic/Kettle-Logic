import SwiftUI

/// The update window, from the app menu or after an automatic check finds something new.
struct UpdateView: View {
    @Bindable var center: UpdateCenter

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Software Update")
                .font(.title2.bold())

            VStack(alignment: .leading, spacing: 4) {
                Text("Kettle Logic \(appVersion)")
                Text("Engine \(engineVersion)")
                    .foregroundStyle(.secondary)
                ForEach(EngineLoader.skipped, id: \.self) { line in
                    Text(line)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .textSelection(.enabled)
                }
            }

            content

            HStack {
                Spacer()
                Button("Close") { center.dismiss() }
                    .keyboardShortcut(.cancelAction)
            }
        }
        .padding(24)
        .frame(width: 460)
    }

    @ViewBuilder
    private var content: some View {
        switch center.phase {
        case .idle:
            Button("Check for Updates") {
                Task { await center.check(automatically: false) }
            }

        case .checking:
            ProgressView("Checking for updates…")

        case .upToDate:
            Text("Kettle Logic is up to date.")
            Button("Check Again") {
                Task { await center.check(automatically: false) }
            }

        case .offered(let offer):
            offerContent(offer)

        case .installing(let step):
            ProgressView(step)

        case .restartNeeded:
            Text("The engine is installed. Restart Kettle Logic to use it.")

        case .failed(let message):
            Text(message)
                .foregroundStyle(.red)
                .textSelection(.enabled)
            Button("Check Again") {
                Task { await center.check(automatically: false) }
            }
        }
    }

    @ViewBuilder
    private func offerContent(_ offer: UpdateOffer) -> some View {
        VStack(alignment: .leading, spacing: 12) {
            if let app = offer.app {
                Text("Kettle Logic \(app.version) is available.")
            }
            if let engine = offer.engine {
                Text("Engine \(engine.version) is available.")
            }

            HStack {
                if offer.engineWithApp != nil, offer.app != nil {
                    Button("Update Both") { Task { await center.install(.both) } }
                        .buttonStyle(.borderedProminent)
                }
                if offer.app != nil {
                    Button(offer.engineWithApp != nil ? "Update App Only" : "Update App") {
                        Task { await center.install(.app) }
                    }
                    .buttonStyle(.borderedProminent)
                }
                if offer.engine != nil, offer.app == nil || offer.engineWithApp == nil {
                    Button("Update Engine") { Task { await center.install(.engine) } }
                        .buttonStyle(.borderedProminent)
                }
            }

            Text("Kettle Logic restarts to finish an update. Your recipes stay as they are. Updates are downloaded from "
                + "GitHub and checked against the list of updates before they are installed.")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }

    private var appVersion: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "?"
    }

    private var engineVersion: String {
        EngineLoader.loaded.map { "\($0.version)" + ($0.isBundled ? " (built in)" : "") } ?? "not loaded"
    }
}
