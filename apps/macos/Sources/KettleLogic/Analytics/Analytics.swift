import Darwin
import Foundation

/// How much anonymous usage and error data the app sends. Each level includes the ones before it.
enum TelemetryLevel: String, CaseIterable, Identifiable {
    /// Nothing is sent.
    case off
    /// Errors and crashes, and that the app was opened.
    case limited
    /// Also which features are used.
    case enhanced

    var id: Self { self }

    var title: String {
        switch self {
        case .off: "Off"
        case .limited: "Limited"
        case .enhanced: "Enhanced"
        }
    }

    var detail: String {
        switch self {
        case .off:
            "Nothing is sent."
        case .limited:
            "Crash and error reports, and that the app was opened, with the app and system version. "
                + "Nothing about your recipes."
        case .enhanced:
            "Also which features are used, such as exporting or a tool. Still anonymous, and never your recipes."
        }
    }

    /// The level in the user defaults. Anything unset or unknown is Limited, the default.
    static var current: TelemetryLevel {
        TelemetryLevel(rawValue: UserDefaults.standard.string(forKey: Preferences.telemetryLevelKey) ?? "") ?? .limited
    }

    private var rank: Int {
        switch self {
        case .off: 0
        case .limited: 1
        case .enhanced: 2
        }
    }

    func includes(_ other: TelemetryLevel) -> Bool { rank >= other.rank }
}

/// Every event the app can send. The raw value is its name in Google Analytics.
enum AnalyticsEvent: String {
    case appOpen = "app_open"
    case appError = "app_error"
    case appCrash = "app_crash"
    case updateCheck = "update_check_result"
    case featureUsed = "feature_used"
    case recipeAction = "recipe_action"
    case exportRecipe = "export_recipe"
    case importRecipe = "import_recipe"

    /// The least level that sends this event.
    var minimumLevel: TelemetryLevel {
        switch self {
        case .appOpen, .appError, .appCrash, .updateCheck: .limited
        case .featureUsed, .recipeAction, .exportRecipe, .importRecipe: .enhanced
        }
    }
}

/// A parameter value: a number, or a short fixed word. Never text the user typed.
enum AnalyticsValue {
    case word(String)
    case count(Double)
}

/// Anonymous usage and error reports, sent to Google Analytics 4 through its Measurement Protocol. This is the macOS
/// side of the same rules the Windows and Linux apps follow (apps/windows/logic/.../analytics.hpp). What is sent is
/// fixed: an event name from `AnalyticsEvent`, and parameters that are numbers or short fixed words. A recipe, a name,
/// or a path never leaves the computer. See docs/privacy.md.
///
/// A build with no measurement ID and API secret in its Info.plist (`KLGAMeasurementID`, `KLGAAPISecret`) sends nothing,
/// so development builds stay quiet.
@MainActor
enum Analytics {
    private static var started = false

    // MARK: Where reports go

    private static func plist(_ key: String) -> String {
        Bundle.main.object(forInfoDictionaryKey: key) as? String ?? ""
    }

    private static var measurementID: String { plist("KLGAMeasurementID") }
    private static var apiSecret: String { plist("KLGAAPISecret") }

    /// Whether this build has ids to send to at all.
    static var isConfigured: Bool { !measurementID.isEmpty && !apiSecret.isEmpty }

    /// A build can validate its reports instead of recording them: set `KLGADebug` to YES in the Info.plist.
    private static var endpoint: URL? {
        let debug = (Bundle.main.object(forInfoDictionaryKey: "KLGADebug") as? String) == "YES"
        var parts = URLComponents(string: "https://www.google-analytics.com/\(debug ? "debug/mp/collect" : "mp/collect")")
        parts?.queryItems = [
            URLQueryItem(name: "measurement_id", value: measurementID),
            URLQueryItem(name: "api_secret", value: apiSecret),
        ]
        return parts?.url
    }

    // MARK: Starting and settings

    /// Starts reporting: sends the crash the last run left, if any, and records that the app was opened. Call it once
    /// the user's telemetry choice is known. A second call does nothing.
    static func start() {
        guard !started else { return }
        started = true
        CrashMarker.install()
        CrashMarker.setEnabled(sends)
        // At Off the old marker is removed unsent, and nothing is sent now.
        sendPendingCrash()
        record(.appOpen)
    }

    /// Settings changed the level. Turning reports off also removes any crash marker, so nothing is sent later.
    static func levelChanged() {
        guard started else { return }
        CrashMarker.setEnabled(sends)
        if !sends { CrashMarker.discard() }
    }

    /// Replaces the random id that groups this install's reports, so later reports are not linked to earlier ones.
    static func resetClientID() {
        UserDefaults.standard.set(UUID().uuidString.lowercased(), forKey: Preferences.analyticsClientIdKey)
    }

    private static var sends: Bool { isConfigured && TelemetryLevel.current != .off }

    private static var clientID: String {
        if let id = UserDefaults.standard.string(forKey: Preferences.analyticsClientIdKey), !id.isEmpty {
            return id
        }
        let id = UUID().uuidString.lowercased()
        UserDefaults.standard.set(id, forKey: Preferences.analyticsClientIdKey)
        return id
    }

    private static let sessionID = Int(Date().timeIntervalSince1970)

    // MARK: Recording

    /// Sends `event` if the level allows it. Never throws and never waits for the network.
    static func record(_ event: AnalyticsEvent, _ parameters: [String: AnalyticsValue] = [:]) {
        guard isConfigured, TelemetryLevel.current.includes(event.minimumLevel), let url = endpoint,
              let body = body(event, parameters)
        else { return }
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.timeoutInterval = 5
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.httpBody = body
        // A report that cannot be sent is dropped.
        Task.detached(priority: .utility) { _ = try? await URLSession.shared.data(for: request) }
    }

    /// A problem the app recovered from. `domain` and `code` are fixed words or numbers, not messages.
    static func recordError(domain: String, code: String, where place: String) {
        record(.appError, [
            "error_domain": .word(domain), "error_code": .word(code), "where": .word(place), "fatal": .count(0),
        ])
    }

    /// The request body for `event`. Public to the module so a test can check it.
    static func body(_ event: AnalyticsEvent, _ parameters: [String: AnalyticsValue]) -> Data? {
        var params: [String: Any] = [:]
        func add(_ key: String, _ value: String) {
            if !value.isEmpty { params[key] = clean(value) }
        }
        add("platform", "macos")
        add("app_version", plist("CFBundleShortVersionString"))
        add("engine_version", plist("KLEngineVersion"))
        add("os_version", String(ProcessInfo.processInfo.operatingSystemVersion.majorVersion))
        add("language", Locale.current.language.languageCode?.identifier ?? "")
        for (key, value) in parameters {
            switch value {
            case .word(let text): add(key, text)
            case .count(let number): params[key] = number
            }
        }
        // GA4 only counts an event toward a session and its engagement when these are present.
        params["engagement_time_msec"] = 1
        params["session_id"] = sessionID

        let root: [String: Any] = [
            "client_id": clientID,
            // Reports are never used to personalize ads.
            "non_personalized_ads": true,
            "events": [["name": event.rawValue, "params": params]],
        ]
        return try? JSONSerialization.data(withJSONObject: root)
    }

    /// A value cut to 100 characters, with anything that looks like a path or an address replaced by "[removed]".
    nonisolated static func clean(_ value: String) -> String {
        if value.contains(where: { "/\\@".contains($0) }) { return "[removed]" }
        return String(value.prefix(100))
    }

    // MARK: Crashes

    private static func sendPendingCrash() {
        guard let text = CrashMarker.read() else { return }
        CrashMarker.discard()  // removed first, so a crash while sending cannot send the same report twice
        let parts = text.split(whereSeparator: { $0 == " " || $0 == "\n" }).map(String.init)
        guard parts.count == 3 else { return }
        record(.appCrash, [
            "error_domain": .word(parts[0]), "error_code": .word(parts[1]), "where": .word(parts[2]),
            "fatal": .count(1),
        ])
    }
}
