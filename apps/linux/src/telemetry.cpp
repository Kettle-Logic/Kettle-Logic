#include "telemetry.hpp"

#include "kettlelogic/app/app_updates.hpp"
#include "kettlelogic/c/kettlelogic.h"
#include "platform.hpp"

#include <libsoup/soup.h>

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <cstring>
#include <memory>
#include <stdexcept>

namespace kl::telemetry {

namespace {

namespace analytics = kettlelogic::app::analytics;
namespace updates = kettlelogic::app::updates;

// Posts with libsoup, which the GNOME runtime provides. Blocking, so it only runs on the queue's worker thread. The
// request times out, so closing the app never waits long.
class SoupTransport : public analytics::Transport {
public:
    void post(const std::string& url, const std::string& jsonBody) override {
        SoupSession* session = soup_session_new();
        soup_session_set_user_agent(session, ("KettleLogic/" + std::string(updates::kAppVersion)).c_str());
        soup_session_set_timeout(session, 5);
        SoupMessage* message = soup_message_new("POST", url.c_str());
        if (message == nullptr) {
            g_object_unref(session);
            throw std::runtime_error("the report address is not valid");
        }
        GBytes* payload = g_bytes_new(jsonBody.data(), jsonBody.size());
        soup_message_set_request_body_from_bytes(message, "application/json", payload);
        g_bytes_unref(payload);

        GError* error = nullptr;
        GBytes* body = soup_session_send_and_read(session, message, nullptr, &error);
        const bool failed = error != nullptr;
        if (error != nullptr) g_error_free(error);
        if (body != nullptr) g_bytes_unref(body);
        g_object_unref(message);
        g_object_unref(session);
        if (failed) throw std::runtime_error("the report could not be sent");
    }
};

// What `Start` builds. It lives until `Stop`.
struct State {
    SoupTransport soup;
    analytics::QueuedTransport queue{soup};
    analytics::Reporter reporter;
    kettlelogic::app::Preferences& preferences;

    State(kettlelogic::app::Preferences& prefs, analytics::Context context, std::filesystem::path marker)
        : reporter(prefs, queue, std::move(context), std::move(marker)), preferences(prefs) {}
};

std::unique_ptr<State> gState;

// The crash marker's path, ready before any crash, because a crash handler must not allocate.
char gMarkerPath[4096] = {};

std::filesystem::path MarkerFile() {
    return platform::DataFolder() / "crash-report.txt";
}

std::string Major(const char* version) {
    if (version == nullptr) return "";
    std::string text(version);
    return text.substr(0, text.find('.'));
}

analytics::Context MakeContext() {
    analytics::Context context;
    context.platform = "linux";
    context.appVersion = updates::kAppVersion;
    context.engineVersion = kl_engine_version();
    char* osVersion = g_get_os_info(G_OS_INFO_KEY_VERSION_ID);
    context.osVersion = Major(osVersion);
    g_free(osVersion);
    // The language only, such as "en" from "en_US.UTF-8".
    const char* const* languages = g_get_language_names();
    if (languages != nullptr && languages[0] != nullptr) {
        std::string language(languages[0]);
        language = language.substr(0, language.find_first_of("_.@"));
        context.language = language == "C" ? "" : language;
    }
    return context;
}

// A crash handler may only call functions that are safe in a signal handler: no allocation, no locks, no stdio. It
// writes "signal <number> app" to the marker with open and write, and lets the crash go on. The next launch sends it.
void WriteCrashMarker(int signalNumber) {
    char line[48] = "signal ";
    std::size_t length = std::strlen(line);
    char digits[12];
    int count = 0;
    unsigned value = signalNumber < 0 ? 0u : static_cast<unsigned>(signalNumber);
    do {
        digits[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0 && count < 11);
    while (count > 0) line[length++] = digits[--count];
    static const char tail[] = " app\n";
    std::memcpy(line + length, tail, sizeof tail - 1);
    length += sizeof tail - 1;

    const int file = ::open(gMarkerPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (file >= 0) {
        const ssize_t written = ::write(file, line, length);
        (void)written;
        ::close(file);
    }
}

// Whether a crash leaves a marker: set while the level sends crash reports. A signal handler may read it.
volatile sig_atomic_t gLeaveMarker = 0;

void OnCrashSignal(int signalNumber) {
    if (gLeaveMarker != 0) WriteCrashMarker(signalNumber);
    ::signal(signalNumber, SIG_DFL);
    ::raise(signalNumber);
}

void InstallCrashHandlers() {
    const std::string path = MarkerFile().string();
    if (path.size() >= sizeof gMarkerPath) return;
    std::memcpy(gMarkerPath, path.c_str(), path.size() + 1);
    for (const int signalNumber : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL}) {
        struct sigaction action {};
        action.sa_handler = OnCrashSignal;
        sigemptyset(&action.sa_mask);
        sigaction(signalNumber, &action, nullptr);
    }
}

}  // namespace

void Start(kettlelogic::app::Preferences& preferences) {
    if (gState) return;
    std::error_code ignored;
    std::filesystem::create_directories(platform::DataFolder(), ignored);
    gState = std::make_unique<State>(preferences, MakeContext(), MarkerFile());
    InstallCrashHandlers();
    gLeaveMarker = gState->reporter.configured() && gState->reporter.level() != analytics::Level::off ? 1 : 0;
    // At Off the old marker is removed unsent, and nothing is sent now.
    gState->reporter.sendPendingCrash();
    gState->reporter.record(analytics::Event::appOpen);
}

void Stop() {
    gLeaveMarker = 0;
    gState.reset();
}

void Record(analytics::Event event, std::vector<analytics::Param> params) {
    if (gState) gState->reporter.record(event, std::move(params));
}

void RecordError(const std::string& domain, const std::string& code, const std::string& where) {
    if (gState) gState->reporter.recordError(domain, code, where);
}

void LevelChanged() {
    if (!gState) return;
    const bool sends = gState->reporter.configured() && gState->reporter.level() != analytics::Level::off;
    gLeaveMarker = sends ? 1 : 0;
    if (!sends) gState->reporter.discardPendingCrash();
}

void ResetId() {
    if (gState) gState->reporter.resetClientId();
}

}  // namespace kl::telemetry
