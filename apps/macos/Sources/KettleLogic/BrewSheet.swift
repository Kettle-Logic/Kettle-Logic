import AppKit
import SwiftUI

// The brew sheet: a printed page for the brew day, and the entry screen that reads its values back. The engine lays
// the sheet out and draws the PDF, and reads the typed values; this file only shows the boxes and puts the values
// into the session. The codes match the other platforms' sheets.

/// One box on the sheet, as the engine lays it out.
struct BrewSheetBox: Decodable, Identifiable, Hashable {
    let code: String
    let label: String
    let kind: String
    let unit: String
    let expected: Double?
    let expectedText: String

    var id: String { code }
}

struct BrewSheetSection: Decodable, Identifiable, Hashable {
    let code: String
    let title: String
    let boxes: [BrewSheetBox]

    var id: String { code }
}

/// The whole sheet for a recipe, in the user's units.
struct BrewSheetLayout: Decodable {
    let recipeName: String
    let batchText: String
    let units: String
    let sections: [BrewSheetSection]
}

/// The values read back from a sheet: numbers in SI (liters, degrees C, SG, pH), dates as yyyy-MM-dd, and the
/// boxes that could not be read.
struct BrewSheetReading: Decodable {
    struct Issue: Decodable {
        let code: String
        let message: String
    }

    let numbers: [String: Double]
    let dates: [String: String]
    let issues: [Issue]
}

extension BrewSession {
    /// Puts the values read from a sheet into the session. A value on the sheet replaces the session's, and an empty
    /// box keeps it. When the sheet has any readings, they replace the session's readings. A reading with no date
    /// takes the brew day.
    mutating func apply(_ sheet: BrewSheetReading) {
        if let day = BrewSheetDate.date(sheet.dates["A1"]) { brewDate = day }

        if let value = sheet.numbers["B1"] { mashPh = value }
        if let value = sheet.numbers["B2"] { spargeRunoffPh = value }
        if let value = sheet.numbers["B3"] { endOfRunningsGravity = value }

        if let value = sheet.numbers["C1"] { preBoilLiters = value }
        if let value = sheet.numbers["C2"] { preBoilGravity = value }

        if let value = sheet.numbers["D1"] { batchLiters = value }
        if let value = sheet.numbers["D2"] { originalGravity = value }

        if let value = sheet.numbers["E1"] { finalGravity = value }
        if let value = sheet.numbers["E2"] { bottlingLiters = value }

        // Reading row k is codes 3k-2 (date), 3k-1 (temperature), and 3k (gravity).
        var entered: [FermentationReading] = []
        for row in 1...6 {
            let date = sheet.dates["F\(3 * row - 2)"]
            let temperature = sheet.numbers["F\(3 * row - 1)"]
            let gravity = sheet.numbers["F\(3 * row)"]
            guard date != nil || temperature != nil || gravity != nil else { continue }

            var reading = FermentationReading()
            reading.date = BrewSheetDate.date(date) ?? brewDate
            reading.temperatureC = temperature
            reading.gravity = gravity
            entered.append(reading)
        }
        if !entered.isEmpty { readings = entered }
    }
}

/// A yyyy-MM-dd date, at midday so it lands on the same day in any time zone.
enum BrewSheetDate {
    static func date(_ text: String?) -> Date? {
        guard let text else { return nil }
        let parts = text.split(separator: "-").compactMap { Int($0) }
        guard parts.count == 3 else { return nil }
        return Calendar.current.date(from: DateComponents(year: parts[0], month: parts[1], day: parts[2], hour: 12))
    }
}

/// The entry screen: a box for each code on the sheet. Nothing is entered until every box reads cleanly.
struct BrewSheetEntryView: View {
    let document: RecipeDocument
    let recipe: RecipeSnapshot
    let imperial: Bool
    let onEnter: (BrewSheetReading) -> Void

    @Environment(\.dismiss) private var dismiss
    @State private var layout: BrewSheetLayout?
    @State private var entries: [String: String] = [:]
    @State private var issues: [BrewSheetReading.Issue] = []
    @State private var failure: String?

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text("Enter Brew Sheet")
                .font(.title2.bold())
                .padding([.horizontal, .top], 20)
            Text("Type each value into the box with the same code. Leave empty boxes blank.")
                .foregroundStyle(.secondary)
                .padding(.horizontal, 20)
                .padding(.bottom, 8)

            if let layout {
                Form {
                    ForEach(layout.sections) { section in
                        Section("\(section.code)   \(section.title)") {
                            ForEach(section.boxes) { box in
                                LabeledContent("\(box.code)   \(box.label) (\(box.unit))") {
                                    TextField("", text: binding(box.code))
                                        .multilineTextAlignment(.trailing)
                                        .frame(width: 120)
                                }
                            }
                        }
                    }
                }
                .formStyle(.grouped)
            }

            if !issues.isEmpty {
                VStack(alignment: .leading, spacing: 4) {
                    Text("Nothing was entered. These boxes need fixing:")
                        .bold()
                    ForEach(issues, id: \.code) { issue in
                        Text("\(issue.code): \(issue.message)")
                    }
                }
                .foregroundStyle(.red)
                .padding(.horizontal, 20)
                .padding(.top, 8)
            }
            if let failure {
                Text(failure).foregroundStyle(.red).padding(.horizontal, 20).padding(.top, 8)
            }

            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }
                    .keyboardShortcut(.cancelAction)
                Button("Enter") { enter() }
                    .keyboardShortcut(.defaultAction)
                    .buttonStyle(.borderedProminent)
            }
            .padding(20)
        }
        .frame(width: 520, height: 620)
        .onAppear(perform: load)
    }

    private func binding(_ code: String) -> Binding<String> {
        Binding(get: { entries[code] ?? "" }, set: { entries[code] = $0 })
    }

    private func load() {
        do {
            layout = try document.brewSheet(recipe.id, imperial: imperial)
        } catch {
            failure = error.localizedDescription
        }
    }

    private func enter() {
        do {
            let reading = try document.readBrewSheet(recipe.id, imperial: imperial, entries: entries)
            if reading.issues.isEmpty {
                onEnter(reading)
                dismiss()
            } else {
                issues = reading.issues
            }
        } catch {
            failure = error.localizedDescription
        }
    }
}
