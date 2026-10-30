import Foundation

/// Fermentation readings as CSV. The file has a header, then one row per reading:
/// `date,day,temperature_c,gravity_sg`, with the date as yyyy-MM-dd and the day counted from brew day.
/// The day column is worked out on export and ignored on import, so it is always the day of the brew.
enum ReadingsCSV {
    static let header = "date,day,temperature_c,gravity_sg"

    static func export(_ readings: [FermentationReading], brewDate: Date) -> String {
        let rows = readings.sorted { $0.date < $1.date }.map { reading in
            [
                dateText(reading.date),
                String(day(of: reading.date, brewDate: brewDate)),
                reading.temperatureC.map { String($0) } ?? "",
                reading.gravity.map { String($0) } ?? "",
            ].joined(separator: ",")
        }
        return ([header] + rows).joined(separator: "\n") + "\n"
    }

    /// The readings in a CSV file, and the number of rows that could not be read.
    static func parse(_ text: String) -> (readings: [FermentationReading], skipped: Int) {
        var readings: [FermentationReading] = []
        var skipped = 0
        for line in text.split(whereSeparator: \.isNewline) {
            let fields = line.split(separator: ",", omittingEmptySubsequences: false)
                .map { $0.trimmingCharacters(in: .whitespaces) }
            if fields.first == "date" { continue }
            guard fields.count >= 4, let date = dateFrom(fields[0]) else {
                skipped += 1
                continue
            }
            readings.append(FermentationReading(
                date: date,
                temperatureC: Double(fields[2]),
                gravity: Double(fields[3])))
        }
        return (readings, skipped)
    }

    /// The whole days from brew day to the reading's day.
    static func day(of date: Date, brewDate: Date) -> Int {
        let calendar = Calendar.current
        return calendar.dateComponents([.day], from: calendar.startOfDay(for: brewDate),
                                       to: calendar.startOfDay(for: date)).day ?? 0
    }

    private static func dateText(_ date: Date) -> String {
        let parts = Calendar.current.dateComponents([.year, .month, .day], from: date)
        return String(format: "%04d-%02d-%02d", parts.year ?? 0, parts.month ?? 0, parts.day ?? 0)
    }

    /// A date from yyyy-MM-dd, at midday so it lands on the same day in any time zone.
    private static func dateFrom(_ text: String) -> Date? {
        let parts = text.split(separator: "-").compactMap { Int($0) }
        guard parts.count == 3 else { return nil }
        return Calendar.current.date(from: DateComponents(year: parts[0], month: parts[1], day: parts[2], hour: 12))
    }
}
