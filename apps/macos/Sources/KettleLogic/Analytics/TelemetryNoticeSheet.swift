import SwiftUI

/// Tells a user who upgraded from a version that never asked that anonymous error reports are on, and lets them change
/// that. Closing it starts the app's reports.
struct TelemetryNoticeSheet: View {
    @AppStorage(Preferences.telemetryLevelKey) private var telemetryLevel = TelemetryLevel.limited
    @AppStorage(Preferences.telemetryNoticeShownKey) private var noticeShown = false

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Help Improve Kettle Logic").font(.title2.bold())
            Text("Kettle Logic can send anonymous error reports and basic usage data, to help find and fix problems. "
                + "Nothing about your recipes is ever sent, and nothing identifies you. It is set to Limited. Choose "
                + "another level here, or change it any time in **Settings** under **Privacy**.")

            Picker("Share anonymous data", selection: $telemetryLevel) {
                ForEach(TelemetryLevel.allCases) { Text($0.title).tag($0) }
            }
            .pickerStyle(.segmented)

            VStack(alignment: .leading, spacing: 4) {
                ForEach(TelemetryLevel.allCases) { level in
                    (Text("\(level.title): ").bold() + Text(level.detail))
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
            }

            HStack {
                Spacer()
                Button("OK") {
                    noticeShown = true
                    Analytics.start()
                    Analytics.levelChanged()
                }
                .keyboardShortcut(.defaultAction)
                .controlSize(.large)
            }
        }
        .padding(24)
        .frame(width: 460)
        .interactiveDismissDisabled()
    }
}
