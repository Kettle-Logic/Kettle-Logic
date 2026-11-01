import SwiftUI

/// Asks on the first launch, in three steps: how much anonymous usage and error data to share, which colors to use, and
/// which interface the user wants. It sets `Preferences.telemetryLevelKey`, `Preferences.appearanceKey`, and
/// `Preferences.simpleModeKey`, and tells the user where to change each answer later. The first answer starts the app's
/// reports, and the last marks the setup done.
struct InterfaceChoiceSheet: View {
    private enum Step: Int {
        case telemetry = 1
        case appearance
        case interface
    }

    @AppStorage(Preferences.simpleModeKey) private var simpleMode = true
    @AppStorage(Preferences.interfaceChosenKey) private var chosen = false
    @AppStorage(Preferences.appearanceKey) private var appearance = Appearance.system.rawValue
    @AppStorage(Preferences.telemetryLevelKey) private var telemetryLevel = TelemetryLevel.limited
    @AppStorage(Preferences.telemetryNoticeShownKey) private var telemetryNoticeShown = false
    @State private var step = Step.telemetry
    @State private var selection = true
    @State private var telemetrySelection = TelemetryLevel.current

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            VStack(alignment: .leading, spacing: 4) {
                Text("Welcome to Kettle Logic").font(.title2.bold())
                Text("Step \(step.rawValue) of 3").foregroundStyle(.secondary)
            }

            switch step {
            case .telemetry: telemetryStep
            case .appearance: appearanceStep
            case .interface: interfaceStep
            }

            HStack {
                if step != .telemetry {
                    Button("Back") { step = Step(rawValue: step.rawValue - 1) ?? .telemetry }
                        .controlSize(.large)
                }
                Spacer()
                Button(step == .interface ? "Done" : "Next") { advance() }
                    .keyboardShortcut(.defaultAction)
                    .controlSize(.large)
            }
        }
        .padding(24)
        .frame(width: 460)
        .interactiveDismissDisabled()
    }

    private func advance() {
        switch step {
        case .telemetry:
            // Limited is selected, as it is when the question is never asked.
            telemetryLevel = telemetrySelection
            telemetryNoticeShown = true
            Analytics.start()
            step = .appearance
        case .appearance:
            step = .interface
        case .interface:
            simpleMode = selection
            chosen = true
        }
    }

    private var telemetryStep: some View {
        VStack(alignment: .leading, spacing: 18) {
            Text("Help improve Kettle Logic by sharing anonymous data?").foregroundStyle(.secondary)
            VStack(spacing: 10) {
                ForEach(TelemetryLevel.allCases) { level in
                    choice(isSelected: telemetrySelection == level, title: level.title, detail: level.detail) {
                        telemetrySelection = level
                    }
                }
            }
            note("hand.raised", """
                You can change this any time in **Settings** under **Privacy**. Nothing about your \
                recipes is ever sent.
                """)
        }
    }

    private var appearanceStep: some View {
        VStack(alignment: .leading, spacing: 18) {
            Text("Which colors would you like?").foregroundStyle(.secondary)
            VStack(spacing: 10) {
                // The theme changes as it is picked, so the choice can be judged on this sheet.
                ForEach(Appearance.allCases) { option in
                    choice(isSelected: appearance == option.rawValue, title: option.setupTitle, detail: option.setupDetail) {
                        appearance = option.rawValue
                        Appearance.apply()
                    }
                }
            }
            note("circle.lefthalf.filled", "You can change this any time in **Settings** under **Appearance**.")
        }
    }

    private var interfaceStep: some View {
        VStack(alignment: .leading, spacing: 18) {
            Text("How much would you like to see?").foregroundStyle(.secondary)
            VStack(spacing: 10) {
                choice(
                    isSelected: selection, title: "Simple",
                    detail: "Recommended if you are new to brewing. Shows ingredients, results, style, and a "
                        + "brew-day procedure, and assumes typical values for the rest.") { selection = true }
                choice(
                    isSelected: !selection, title: "Full",
                    detail: "Every setting: mash steps, water chemistry, yeast starters, boil-off, "
                        + "and detailed ingredient fields.") { selection = false }
            }
            note("gearshape", """
                You can change this any time in **Settings** (Kettle Logic ▸ Settings, or ⌘,) under \
                **Interface**, or from **View ▸ Simple Mode**.
                """)
        }
    }

    private func note(_ symbol: String, _ text: LocalizedStringKey) -> some View {
        Label {
            Text(text)
        } icon: {
            Image(systemName: symbol)
        }
        .font(.callout)
        .foregroundStyle(.secondary)
    }

    private func choice(isSelected: Bool, title: String, detail: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(alignment: .top, spacing: 12) {
                Image(systemName: isSelected ? "largecircle.fill.circle" : "circle")
                    .foregroundStyle(isSelected ? Color.accentColor : Color.secondary)
                    .imageScale(.large)
                VStack(alignment: .leading, spacing: 3) {
                    Text(title).font(.headline)
                    Text(detail)
                        .font(.callout)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
                Spacer(minLength: 0)
            }
            .padding(12)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(
                RoundedRectangle(cornerRadius: 10)
                    .stroke(isSelected ? Color.accentColor : Color.secondary.opacity(0.3), lineWidth: isSelected ? 2 : 1))
            .contentShape(RoundedRectangle(cornerRadius: 10))
        }
        .buttonStyle(.plain)
        .accessibilityAddTraits(isSelected ? .isSelected : [])
    }
}

private extension Appearance {
    /// The name and description the setup step shows. Settings calls the first one System.
    var setupTitle: String { self == .system ? "Auto" : title }

    var setupDetail: String {
        switch self {
        case .system: "Follows the Mac's setting."
        case .light: "Light colors in every window."
        case .dark: "Dark colors in every window."
        }
    }
}
