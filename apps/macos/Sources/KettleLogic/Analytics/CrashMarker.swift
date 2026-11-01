import Darwin
import Foundation

/// The file a crash leaves for the next launch to report. Nothing network-related runs in a crash: the handler only
/// writes "signal 11 app" (or "exception NSException app") to this file, and the next launch sends it.
///
/// A signal handler may only call functions that are safe in one: no allocation, no locks. So the path and the lines it
/// writes are made before any crash, and the handler uses only `open`, `write`, `close`, `signal`, and `raise`.
enum CrashMarker {
    /// The signals that mean the app crashed. Swift's own traps (`fatalError`, an out-of-range index) raise SIGILL or
    /// SIGTRAP.
    private static let signals: [Int32] = [SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGTRAP]

    nonisolated(unsafe) private static var path: UnsafeMutablePointer<CChar>?
    // Raw tables, indexed by signal number, because a Swift array can retain and release, which a handler must not do.
    nonisolated(unsafe) private static let lines = UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>.allocate(capacity: 64)
    nonisolated(unsafe) private static let lengths = UnsafeMutablePointer<Int>.allocate(capacity: 64)
    nonisolated(unsafe) private static var enabled: Int32 = 0

    static var url: URL {
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        return support.appending(path: "KettleLogic/crash-report.txt", directoryHint: .notDirectory)
    }

    /// Prepares the handlers. The signal handler writes nothing until `setEnabled(true)`.
    static func install() {
        let file = url
        try? FileManager.default.createDirectory(
            at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
        path = strdup(file.path(percentEncoded: false))
        lines.initialize(repeating: nil, count: 64)
        lengths.initialize(repeating: 0, count: 64)
        for number in signals where number < 64 {
            let line = "signal \(number) app\n"
            lines[Int(number)] = strdup(line)
            lengths[Int(number)] = line.utf8.count
            signal(number) { received in
                if CrashMarker.enabled != 0, let path = CrashMarker.path, received > 0, received < 64,
                   let line = CrashMarker.lines[Int(received)] {
                    let descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0o600)
                    if descriptor >= 0 {
                        _ = write(descriptor, line, CrashMarker.lengths[Int(received)])
                        close(descriptor)
                    }
                }
                signal(received, SIG_DFL)
                raise(received)
            }
        }
        NSSetUncaughtExceptionHandler { _ in
            // Not a signal handler, so it may allocate. Only the fixed word is written, never the exception's reason.
            if CrashMarker.enabled != 0 {
                try? "exception NSException app\n".write(to: CrashMarker.url, atomically: false, encoding: .utf8)
            }
        }
    }

    /// Whether a crash leaves a marker: on while the level sends crash reports.
    static func setEnabled(_ on: Bool) {
        enabled = on ? 1 : 0
    }

    static func read() -> String? {
        try? String(contentsOf: url, encoding: .utf8)
    }

    static func discard() {
        try? FileManager.default.removeItem(at: url)
    }
}
