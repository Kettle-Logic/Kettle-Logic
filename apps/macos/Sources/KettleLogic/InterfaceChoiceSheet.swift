import SwiftUI

/// Asks on the first launch which interface the user wants, and how much anonymous usage and error data to share. It
/// sets `Preferences.simpleModeKey` and `Preferences.telemetryLevelKey`, and tells the user where to change the answers
/// later. Answering starts the app's reports.
struct InterfaceChoiceSheet: View {
    @AppStorage(Preferences.simpleModeKey) private var simpleMode = true
    @AppStorage(Preferences.interfaceChosenKey) private var chosen = false
    @AppStorage(Preferences.telemetryLevelKey) private var telemetryLevel = TelemetryLevel.limited
    @AppStorage(Preferences.telemetryNoticeShownKey) private var telemetryNoticeShown = false
    @State private var selection = true
    @State private var telemetrySelection = TelemetryLevel.current

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            VStack(alignment: .leading, spacing: 4) {
                Text("Welcome to Kettle Logic").font(.title2.bold())
                Text("How much would you like to see?").foregroundStyle(.secondary)
            }

            VStack(spacing: 10) {
                choice(
                    simple: true,
                    title: "Simple",
                    detail: "Recommended if you are new to brewing. Shows ingredients, results, style, and a "
                        + "brew-day procedure, and assumes typical values for the rest.")
                choice(
                    simple: false,
                    title: "Full",
                    detail: "Every setting: mash steps, water chemistry, yeast starters, boil-off, "
                        + "and detailed ingredient fields.")
            }

            Label {
                Text("You can change this any time in **Settings** (Kettle Logic ▸ Settings, or ⌘,) under "
                    + "**Interface**, or from **View ▸ Simple Mode**.")
            } icon: {
                Image(systemName: "gearshape")
            }
            .font(.callout)
            .foregroundStyle(.secondary)

            // Limited is selected, as it is when the question is never asked.
            Text("Help improve Kettle Logic by sharing anonymous data?").foregroundStyle(.secondary)
            VStack(spacing: 10) {
                ForEach(TelemetryLevel.allCases) { level in
                    telemetryChoice(level)
                }
            }
            Label {
                Text("You can change this any time in **Settings** under **Privacy**. Nothing about your recipes is "
                    + "ever sent.")
            } icon: {
                Image(systemName: "hand.raised")
            }
            .font(.callout)
            .foregroundStyle(.secondary)

            HStack {
                Spacer()
                Button("Continue") {
                    simpleMode = selection
                    telemetryLevel = telemetrySelection
                    telemetryNoticeShown = true
                    chosen = true
                    Analytics.start()
                }
                .keyboardShortcut(.defaultAction)
                .controlSize(.large)
            }
        }
        .padding(24)
        .frame(width: 460)
        .interactiveDismissDisabled()
    }

    private func telemetryChoice(_ level: TelemetryLevel) -> some View {
        let isSelected = telemetrySelection == level
        return Button {
            telemetrySelection = level
        } label: {
            HStack(alignment: .top, spacing: 12) {
                Image(systemName: isSelected ? "largecircle.fill.circle" : "circle")
                    .foregroundStyle(isSelected ? Color.accentColor : Color.secondary)
                    .imageScale(.large)
                VStack(alignment: .leading, spacing: 3) {
                    Text(level.title).font(.headline)
                    Text(level.detail)
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

    private func choice(simple: Bool, title: String, detail: String) -> some View {
        let isSelected = selection == simple
        return Button {
            selection = simple
        } label: {
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
