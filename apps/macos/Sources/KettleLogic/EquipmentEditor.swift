import SwiftUI

/// Makes or changes a brewing system. The figures are typed as text, and each is checked before the system is
/// saved, so a value the engine would refuse when a recipe applies the system is caught here.
struct EquipmentEditor: View {
    let request: EquipmentRequest
    let library: EquipmentLibrary

    @Environment(\.dismiss) private var dismiss
    @AppStorage(Preferences.unitsKey) private var units = UnitSystem.metric
    @State private var draft: EquipmentProfile
    @State private var boilMinutes: String
    @State private var errorMessage: String?

    init(request: EquipmentRequest, library: EquipmentLibrary) {
        self.request = request
        self.library = library
        // The volumes are typed in the unit system the user chose, so they start as text in that unit.
        let system = UnitSystem.current
        var shown = request.profile
        shown.batchLiters = system.volumeText(liters: shown.batchLiters)
        shown.trubLossLiters = system.volumeText(liters: shown.trubLossLiters)
        _draft = State(initialValue: shown)
        _boilMinutes = State(initialValue: String(request.profile.boilMinutes))
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text(request.title).font(.headline)
            Form {
                Section {
                    TextField("Name", text: $draft.name)
                    TextField("Batch size (\(units.volumeSymbol))", text: $draft.batchLiters)
                    TextField("Brewhouse efficiency (%)", text: $draft.efficiencyPercent)
                    TextField("Boil-off (% per hour)", text: $draft.evaporationPercent)
                    TextField("Trub and chiller loss (\(units.volumeSymbol))", text: $draft.trubLossLiters)
                    TextField("Boil length (minutes)", text: $boilMinutes)
                    Picker("Mash system", selection: $draft.mashSystem) {
                        Text("All in one").tag("all in one")
                        Text("Separate tun").tag("separate tun")
                    }
                } footer: {
                    Text("The figures are starting points for a recipe, so measure your own system. "
                        + "A recipe applies them with its batch size, efficiency, and boil-off in one undo step.")
                }

                Section("Note") {
                    TextField("Where the figures come from", text: $draft.note, axis: .vertical)
                        .lineLimit(2...4)
                }
            }
            .formStyle(.grouped)
            .textFieldStyle(.roundedBorder)

            if let errorMessage {
                Text(errorMessage).foregroundStyle(.red)
            }

            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }
                    .keyboardShortcut(.cancelAction)
                Button("Save", action: save)
                    .keyboardShortcut(.defaultAction)
            }
        }
        .padding(20)
        .frame(width: 460)
    }

    private func save() {
        var profile = draft
        profile.batchLiters = trimmed(draft.batchLiters)
        profile.efficiencyPercent = trimmed(draft.efficiencyPercent)
        profile.evaporationPercent = trimmed(draft.evaporationPercent)
        profile.trubLossLiters = trimmed(draft.trubLossLiters)
        profile.note = trimmed(draft.note)

        // The volumes are checked in the unit the user typed them in.
        let figures: [(text: String, range: ClosedRange<Double>, message: String)] = [
            (profile.batchLiters, volumeRange(0.1...1000), "The batch size must be from \(amount(0.1)) to \(amount(1000))."),
            (profile.efficiencyPercent, 0.1...100, "The brewhouse efficiency must be from 0.1 to 100 %."),
            (profile.evaporationPercent, 0...99.9, "The boil-off must be from 0 to 99.9 % per hour."),
            (profile.trubLossLiters, volumeRange(0...100), "The trub and chiller loss must be from 0 to \(amount(100))."),
        ]
        if let bad = figures.first(where: { !isPlainNumber($0.text, in: $0.range) }) {
            errorMessage = bad.message
            return
        }
        guard let minutes = Int(trimmed(boilMinutes)), (1...600).contains(minutes) else {
            errorMessage = "The boil length must be a whole number of minutes, from 1 to 600."
            return
        }
        profile.boilMinutes = minutes
        profile.batchLiters = litersText(profile.batchLiters, original: request.profile.batchLiters)
        profile.trubLossLiters = litersText(profile.trubLossLiters, original: request.profile.trubLossLiters)

        do {
            try library.save(profile, replacing: request.replacing)
            dismiss()
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    /// Whether `text` is a plain decimal number, such as "19" or "9.5", within `range`. The engine reads exact
    /// decimal text, so a sign, a comma, or an exponent is refused.
    private func isPlainNumber(_ text: String, in range: ClosedRange<Double>) -> Bool {
        guard !text.isEmpty, text.allSatisfy({ "0123456789.".contains($0) }),
              text.filter({ $0 == "." }).count <= 1, let value = Double(text) else { return false }
        return range.contains(value)
    }

    /// A range of liters, in the unit the user types volumes in.
    private func volumeRange(_ liters: ClosedRange<Double>) -> ClosedRange<Double> {
        liters.lowerBound * units.volumeFactor ... liters.upperBound * units.volumeFactor
    }

    /// A volume in liters, written in the unit the user types volumes in.
    private func amount(_ liters: Double) -> String {
        units.displayVolume(liters: liters).formatted(.number.precision(.significantDigits(1...3)))
            + " " + units.volumeSymbol
    }

    /// The liters for a volume typed in the user's unit. A volume left as it was shown keeps its exact liters.
    private func litersText(_ shown: String, original: String) -> String {
        shown == units.volumeText(liters: original) ? original : units.litersText(volume: shown)
    }
}
