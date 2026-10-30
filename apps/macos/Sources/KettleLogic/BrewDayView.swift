import AppKit
import SwiftUI

/// What a brew-day window shows: one generated procedure, with its recipe and boil length.
/// Each generated procedure has its own id, so generating again opens a new window.
struct BrewDayPlan: Codable, Hashable {
    let id: UUID
    let recipeName: String
    let boilMinutes: Int
    let steps: [ProcedureStep]

    /// Where this procedure's ticked steps are kept. It comes from the recipe name and the steps, not from `id`,
    /// so reopening the same procedure finds its progress, and a changed recipe starts clean. FNV-1a, since
    /// Swift's own hash differs on every launch.
    var progressKey: String {
        var hash: UInt64 = 0xcbf2_9ce4_8422_2325
        for byte in ([recipeName, String(boilMinutes)] + steps.map(\.text)).joined(separator: "\u{1F}").utf8 {
            hash = (hash ^ UInt64(byte)) &* 0x100_0000_01b3
        }
        return "brewDayProgress.\(String(hash, radix: 16))"
    }
}

/// A large checklist of the procedure, for reading at the kettle. Tap a step to tick it off.
/// The first step not yet ticked is the current one. The display stays awake while it is open,
/// unless the toggle at the bottom turns that off.
struct BrewDayView: View {
    let plan: BrewDayPlan

    @State private var done: Set<Int> = []
    @State private var sleepLock: NSObjectProtocol?
    @AppStorage(Preferences.keepDisplayOnKey) private var keepDisplayOn = true

    /// The first step not yet ticked, or nil when all are done.
    private var current: Int? {
        plan.steps.indices.first { !done.contains($0) }
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                header
                ForEach(plan.steps.indices, id: \.self) { index in
                    stepButton(index)
                }
            }
            .padding(24)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .safeAreaInset(edge: .bottom) {
            VStack(spacing: 0) {
                Divider()
                Toggle("Keep display on", isOn: $keepDisplayOn)
                    .toggleStyle(.switch)
                    .padding(.horizontal, 24)
                    .padding(.vertical, 12)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
            .background(.bar)
        }
        .frame(minWidth: 480, minHeight: 560)
        .navigationTitle("Brew Day: \(plan.recipeName)")
        .toolbar {
            ToolbarItem {
                Button("Start Over", systemImage: "arrow.counterclockwise") { done = [] }
                    .disabled(done.isEmpty)
                    .help("Untick every step. Ticked steps are remembered if you close this window.")
            }
        }
        .onAppear {
            done = Set(UserDefaults.standard.array(forKey: plan.progressKey) as? [Int] ?? [])
            updateSleepLock()
        }
        .onChange(of: done) {
            if done.isEmpty {
                UserDefaults.standard.removeObject(forKey: plan.progressKey)
            } else {
                UserDefaults.standard.set(done.sorted(), forKey: plan.progressKey)
            }
        }
        .onChange(of: keepDisplayOn) { updateSleepLock() }
        .onDisappear { releaseSleepLock() }
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(plan.recipeName)
                .font(.system(size: 34, weight: .bold))
            Text("Boil \(plan.boilMinutes) minutes · \(done.count) of \(plan.steps.count) done")
                .font(.title3)
                .foregroundStyle(.secondary)
                .monospacedDigit()
        }
        .padding(.bottom, 6)
    }

    /// A step, with its timer underneath when it has one. The timer sits outside the tick
    /// button, so the two are not nested.
    private func stepButton(_ index: Int) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            stepTapTarget(index)
            if let minutes = plan.steps[index].timerMinutes {
                StepTimer(minutes: minutes)
                    .padding(.leading, 18)
            }
        }
    }

    private func stepTapTarget(_ index: Int) -> some View {
        let step = plan.steps[index]
        let isDone = done.contains(index)
        let isCurrent = index == current
        return Button {
            if isDone { done.remove(index) } else { done.insert(index) }
        } label: {
            HStack(alignment: .top, spacing: 16) {
                Image(systemName: isDone ? "checkmark.circle.fill" : "circle")
                    .font(.system(size: 32))
                    .foregroundStyle(isDone ? Color.green : Color.secondary)
                VStack(alignment: .leading, spacing: 6) {
                    Text(step.minute.map { "\(step.phase) · \($0) min" } ?? step.phase)
                        .font(.headline)
                        .foregroundStyle(.secondary)
                        .monospacedDigit()
                    Text(step.text)
                        .font(.system(size: isCurrent ? 28 : 22, weight: isCurrent ? .semibold : .regular))
                        .foregroundStyle(step.warning ? Color.red : Color.primary)
                        .strikethrough(isDone)
                }
            }
            .padding(18)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(
                RoundedRectangle(cornerRadius: 14)
                    .fill(isCurrent ? Color.accentColor.opacity(0.18) : Color.secondary.opacity(0.08)))
            .opacity(isDone ? 0.5 : 1)
            .contentShape(RoundedRectangle(cornerRadius: 14))
        }
        .buttonStyle(.plain)
        .accessibilityHint(isDone ? "Tap to untick" : "Tap to tick off")
    }

    /// Holds a sleep assertion while the display should stay on, and drops it when it should not.
    private func updateSleepLock() {
        guard keepDisplayOn else {
            releaseSleepLock()
            return
        }
        if sleepLock == nil {
            sleepLock = ProcessInfo.processInfo.beginActivity(
                options: [.idleDisplaySleepDisabled, .userInitiated], reason: "Brewing a recipe")
        }
    }

    private func releaseSleepLock() {
        if let sleepLock { ProcessInfo.processInfo.endActivity(sleepLock) }
        sleepLock = nil
    }
}

/// A countdown for a step that runs for a set time. It starts when tapped, can be paused and
/// resumed, and says so when the time is up.
struct StepTimer: View {
    let minutes: Int

    /// When a running timer reaches zero. Nil unless the timer is running.
    @State private var end: Date?
    /// Seconds left on a paused timer. Nil unless the timer is paused.
    @State private var secondsLeft: TimeInterval?

    var body: some View {
        if let end {
            TimelineView(.periodic(from: .now, by: 1)) { context in
                let finished = context.date >= end
                HStack(spacing: 12) {
                    if finished {
                        Label("Time's up", systemImage: "bell.fill")
                            .font(.title2.weight(.semibold))
                            .foregroundStyle(.orange)
                    } else {
                        Text(timerInterval: context.date...end, countsDown: true)
                            .font(.system(size: 28, weight: .semibold))
                            .monospacedDigit()
                        Button("Pause") { pause() }
                            .controlSize(.large)
                    }
                    Button("Restart") { start() }
                        .controlSize(.large)
                }
            }
        } else if let secondsLeft {
            HStack(spacing: 12) {
                Text(clock(secondsLeft))
                    .font(.system(size: 28, weight: .semibold))
                    .foregroundStyle(.secondary)
                    .monospacedDigit()
                Button("Resume") { resume() }
                    .controlSize(.large)
                Button("Restart") { start() }
                    .controlSize(.large)
            }
        } else {
            HStack(spacing: 12) {
                Button {
                    start()
                } label: {
                    Label("Start \(minutes)-minute timer", systemImage: "timer")
                }
                .controlSize(.large)
            }
        }
    }

    private func start() {
        end = Date.now.addingTimeInterval(TimeInterval(minutes * 60))
        secondsLeft = nil
    }

    private func pause() {
        guard let end else { return }
        secondsLeft = max(0, end.timeIntervalSinceNow)
        self.end = nil
    }

    private func resume() {
        guard let secondsLeft else { return }
        end = Date.now.addingTimeInterval(secondsLeft)
        self.secondsLeft = nil
    }

    /// The time left as m:ss, rounded up so a paused timer never reads 0:00 while time remains.
    private func clock(_ seconds: TimeInterval) -> String {
        let total = Int(seconds.rounded(.up))
        return String(format: "%d:%02d", total / 60, total % 60)
    }
}
