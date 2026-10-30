import SwiftUI

/// Asks on the first launch which interface the user wants. It sets `Preferences.simpleModeKey`, and
/// tells the user where to change the answer later.
struct InterfaceChoiceSheet: View {
    @AppStorage(Preferences.simpleModeKey) private var simpleMode = true
    @AppStorage(Preferences.interfaceChosenKey) private var chosen = false
    @State private var selection = true

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

            HStack {
                Spacer()
                Button("Continue") {
                    simpleMode = selection
                    chosen = true
                }
                .keyboardShortcut(.defaultAction)
                .controlSize(.large)
            }
        }
        .padding(24)
        .frame(width: 460)
        .interactiveDismissDisabled()
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
