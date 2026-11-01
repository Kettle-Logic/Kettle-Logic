#pragma once

#include "kettlelogic/app/preferences.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Anonymous usage and error reports for the Windows and Linux apps, sent to Google Analytics 4 through its Measurement
// Protocol. This module decides what may be sent and builds the request. It has no platform API: each app supplies a
// `Transport` that posts it, and its own crash hooks. The macOS app has its own Swift version of the same rules.
//
// What is sent is fixed here. An event has a name from `Event` and parameters from `Param`, whose values are numbers
// or short fixed words, never text the user typed. A recipe, a name, or a path never leaves the computer. See
// docs/privacy.md.
namespace kettlelogic::app::analytics {

// How much the app sends. Each level includes the ones before it.
enum class Level {
    off,       // nothing is sent
    limited,   // errors and crashes, and that the app was opened
    enhanced,  // also which features are used
};

Level parseLevel(std::string_view text);  // anything unknown is `limited`, the default
const char* levelText(Level level);       // "off", "limited", "enhanced"

// Every event the app can send, and the least level that sends it.
enum class Event {
    appOpen,           // limited
    appError,          // limited: a problem the app recovered from
    appCrash,          // limited: sent on the next launch after one
    updateCheck,       // limited
    featureUsed,       // enhanced
    recipeAction,      // enhanced
    exportRecipe,      // enhanced
    importRecipe,      // enhanced
};
Level minimumLevel(Event event);
const char* eventName(Event event);

// A parameter on an event. Values are kept short and free of paths (see `clean`).
struct Param {
    std::string key;
    std::string text;           // used when `numeric` is false
    double number = 0;          // used when `numeric` is true
    bool numeric = false;

    static Param word(std::string key, std::string value);
    static Param count(std::string key, double value);
};

// A value cut to 100 characters, with anything that looks like a path or an address replaced by "[removed]".
std::string clean(std::string_view value);

// The Google Analytics stream reports go to. A build supplies its own with -DKETTLELOGIC_GA_MEASUREMENT_ID=G-... and
// -DKETTLELOGIC_GA_API_SECRET=... (apps/windows/CMakeLists.txt), and -DKETTLELOGIC_GA_DEBUG=ON to post to GA's
// /debug/mp/collect, which validates reports instead of recording them. A build with no ids sends nothing, so
// development builds stay quiet. A test supplies its own.
struct Endpoint {
    std::string measurementId;
    std::string apiSecret;
    bool debug = false;

    bool configured() const { return !measurementId.empty() && !apiSecret.empty(); }
};

// The stream this build was given. It is read in the library's own source, so every app that links it agrees.
Endpoint buildEndpoint();

// Who is sending, added to every event.
struct Context {
    std::string platform;       // "windows" or "linux"
    std::string appVersion;
    std::string engineVersion;
    std::string osVersion;      // the major version only
    std::string language;       // the language only, such as "en"
};

// How the app posts a report. Blocking: the caller runs it off the UI thread. Throws std::runtime_error on failure,
// which `Reporter` swallows, because losing a report is not a reason to bother the user.
class Transport {
public:
    virtual ~Transport() = default;
    virtual void post(const std::string& url, const std::string& jsonBody) = 0;
};

// Posts through `inner` on a worker thread, so a slow network never holds up the app. At most 50 reports wait; more
// are dropped. The destructor sends what is waiting and then returns, so the inner transport must time out.
class QueuedTransport : public Transport {
public:
    explicit QueuedTransport(Transport& inner);
    ~QueuedTransport() override;
    QueuedTransport(const QueuedTransport&) = delete;
    QueuedTransport& operator=(const QueuedTransport&) = delete;

    void post(const std::string& url, const std::string& jsonBody) override;

private:
    struct Impl;
    Impl* impl_;
};

// A crash the last run left behind, found on this launch.
struct CrashMarker {
    std::string domain;  // "signal", "exception", or "unhandled"
    std::string code;    // a number or short name, such as "11" or "access_violation"
    std::string where;   // a fixed tag for the part of the app, such as "ui" or "engine"
};

// The one line a crash handler writes: "domain code where". Handlers call this only to build the text, and write it
// with calls that are safe in a crash.
std::string crashMarkerLine(const CrashMarker& marker);
std::optional<CrashMarker> parseCrashMarker(std::string_view text);

class Reporter {
public:
    // `markerFile` is where a crash handler leaves its marker (see `sendPendingCrash`).
    Reporter(Preferences& preferences, Transport& transport, Context context, std::filesystem::path markerFile,
             Endpoint endpoint = buildEndpoint());

    Level level() const;
    // Whether this build has an id to send to at all.
    bool configured() const { return endpoint_.configured(); }

    // Sends `event` if the level allows it. Never throws and never blocks the caller longer than the transport does.
    void record(Event event, std::vector<Param> params = {});
    // A problem the app recovered from. `domain` and `code` are fixed words or numbers, not messages.
    void recordError(const std::string& domain, const std::string& code, const std::string& where);

    // Sends the crash the last run left, then removes the marker. At Off the marker is removed unsent.
    void sendPendingCrash();
    // Removes the marker without sending it, as when the user turns reports off.
    void discardPendingCrash();

    // The install's random id, made on first use.
    std::string clientId();
    // Replaces the id, so later reports are not linked to earlier ones.
    void resetClientId();

    // The request body for `event`, and the address it is posted to. Public so a test can check them.
    std::string body(Event event, const std::vector<Param>& params);
    std::string url() const;

private:
    Preferences& preferences_;
    Transport& transport_;
    Context context_;
    std::filesystem::path markerFile_;
    Endpoint endpoint_;
    long long sessionId_;
};

}  // namespace kettlelogic::app::analytics
