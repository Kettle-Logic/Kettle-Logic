#include "kettlelogic/app/analytics.hpp"

#include "kettlelogic/json.hpp"

#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <system_error>
#include <thread>

namespace kettlelogic::app::analytics {

namespace {

constexpr std::size_t kMaxValueLength = 100;

std::string numberText(double value) {
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
    return result.ec == std::errc() ? std::string(buffer, result.ptr) : std::string("0");
}

json::Value numberValue(double value) {
    return json::Value::number(numberText(value));
}

std::string newClientId() {
    std::random_device device;
    std::mt19937_64 random(
        (static_cast<std::uint64_t>(device()) << 32) ^ device() ^
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::uint64_t high = random();
    const std::uint64_t low = random();
    // A random (version 4) UUID.
    static const char digits[] = "0123456789abcdef";
    std::string id;
    for (int i = 0; i < 32; ++i) {
        const std::uint64_t source = i < 16 ? high : low;
        unsigned nibble = static_cast<unsigned>((source >> ((i % 16) * 4)) & 0xF);
        if (i == 12) nibble = 4;
        if (i == 16) nibble = (nibble & 0x3) | 0x8;
        id += digits[nibble];
        if (i == 7 || i == 11 || i == 15 || i == 19) id += '-';
    }
    return id;
}

void addWord(json::Object& object, const char* key, const std::string& value) {
    if (!value.empty()) object.push_back(json::Member{key, json::Value::string(clean(value))});
}

}  // namespace

Endpoint buildEndpoint() {
    Endpoint endpoint;
#ifdef KETTLELOGIC_GA_MEASUREMENT_ID
    endpoint.measurementId = KETTLELOGIC_GA_MEASUREMENT_ID;
#endif
#ifdef KETTLELOGIC_GA_API_SECRET
    endpoint.apiSecret = KETTLELOGIC_GA_API_SECRET;
#endif
#ifdef KETTLELOGIC_GA_DEBUG
    endpoint.debug = true;
#endif
    return endpoint;
}

Level parseLevel(std::string_view text) {
    if (text == "off") return Level::off;
    if (text == "enhanced") return Level::enhanced;
    return Level::limited;
}

const char* levelText(Level level) {
    switch (level) {
        case Level::off: return "off";
        case Level::enhanced: return "enhanced";
        case Level::limited: break;
    }
    return "limited";
}

Level minimumLevel(Event event) {
    switch (event) {
        case Event::appOpen:
        case Event::appError:
        case Event::appCrash:
        case Event::updateCheck: return Level::limited;
        case Event::featureUsed:
        case Event::recipeAction:
        case Event::exportRecipe:
        case Event::importRecipe: break;
    }
    return Level::enhanced;
}

const char* eventName(Event event) {
    switch (event) {
        case Event::appOpen: return "app_open";
        case Event::appError: return "app_error";
        case Event::appCrash: return "app_crash";
        case Event::updateCheck: return "update_check_result";
        case Event::featureUsed: return "feature_used";
        case Event::recipeAction: return "recipe_action";
        case Event::exportRecipe: return "export_recipe";
        case Event::importRecipe: return "import_recipe";
    }
    return "app_error";
}

Param Param::word(std::string key, std::string value) {
    Param param;
    param.key = std::move(key);
    param.text = std::move(value);
    return param;
}

Param Param::count(std::string key, double value) {
    Param param;
    param.key = std::move(key);
    param.number = value;
    param.numeric = true;
    return param;
}

std::string clean(std::string_view value) {
    // A path or an address is personal, so the whole value goes rather than part of it.
    if (value.find_first_of("/\\@") != std::string_view::npos) return "[removed]";
    if (value.size() <= kMaxValueLength) return std::string(value);
    std::size_t end = kMaxValueLength;
    while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xC0) == 0x80) --end;  // do not cut a UTF-8 character
    return std::string(value.substr(0, end));
}

struct QueuedTransport::Impl {
    Transport& inner;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::pair<std::string, std::string>> waiting;
    bool stopping = false;
    std::thread worker;

    explicit Impl(Transport& transport) : inner(transport) {
        worker = std::thread([this] { run(); });
    }

    void run() {
        std::unique_lock lock(mutex);
        while (true) {
            wake.wait(lock, [this] { return stopping || !waiting.empty(); });
            if (waiting.empty()) return;  // stopping, and everything has been sent
            auto [url, body] = std::move(waiting.front());
            waiting.pop_front();
            lock.unlock();
            try {
                inner.post(url, body);
            } catch (const std::exception&) {
                // A report that cannot be sent is dropped.
            }
            lock.lock();
        }
    }
};

QueuedTransport::QueuedTransport(Transport& inner) : impl_(new Impl(inner)) {}

QueuedTransport::~QueuedTransport() {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stopping = true;
    }
    impl_->wake.notify_all();
    impl_->worker.join();
    delete impl_;
}

void QueuedTransport::post(const std::string& url, const std::string& jsonBody) {
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->waiting.size() >= 50) return;
        impl_->waiting.emplace_back(url, jsonBody);
    }
    impl_->wake.notify_one();
}

std::string crashMarkerLine(const CrashMarker& marker) {
    return marker.domain + " " + marker.code + " " + marker.where;
}

std::optional<CrashMarker> parseCrashMarker(std::string_view text) {
    std::istringstream in{std::string(text)};
    CrashMarker marker;
    if (!(in >> marker.domain >> marker.code >> marker.where)) return std::nullopt;
    return marker;
}

Reporter::Reporter(Preferences& preferences, Transport& transport, Context context, std::filesystem::path markerFile,
                   Endpoint endpoint)
    : preferences_(preferences),
      transport_(transport),
      context_(std::move(context)),
      markerFile_(std::move(markerFile)),
      endpoint_(std::move(endpoint)),
      sessionId_(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                     .count()) {}

Level Reporter::level() const {
    return parseLevel(preferences_.string(PrefKey::telemetryLevel));
}

std::string Reporter::url() const {
    return std::string("https://www.google-analytics.com/") + (endpoint_.debug ? "debug/mp/collect" : "mp/collect") +
           "?measurement_id=" + endpoint_.measurementId + "&api_secret=" + endpoint_.apiSecret;
}

std::string Reporter::clientId() {
    std::string id = preferences_.string(PrefKey::analyticsClientId);
    if (id.empty()) {
        id = newClientId();
        preferences_.setString(PrefKey::analyticsClientId, id);
    }
    return id;
}

void Reporter::resetClientId() {
    preferences_.setString(PrefKey::analyticsClientId, newClientId());
}

std::string Reporter::body(Event event, const std::vector<Param>& params) {
    json::Object eventParams;
    addWord(eventParams, "platform", context_.platform);
    addWord(eventParams, "app_version", context_.appVersion);
    addWord(eventParams, "engine_version", context_.engineVersion);
    addWord(eventParams, "os_version", context_.osVersion);
    addWord(eventParams, "language", context_.language);
    for (const Param& param : params) {
        if (param.numeric) {
            eventParams.push_back(json::Member{param.key, numberValue(param.number)});
        } else {
            addWord(eventParams, param.key.c_str(), param.text);
        }
    }
    // GA4 only counts an event toward a session and its engagement when these are present.
    eventParams.push_back(json::Member{"engagement_time_msec", numberValue(1)});
    eventParams.push_back(json::Member{"session_id", numberValue(static_cast<double>(sessionId_))});

    json::Object eventObject;
    eventObject.push_back(json::Member{"name", json::Value::string(eventName(event))});
    eventObject.push_back(json::Member{"params", json::Value::object(std::move(eventParams))});

    json::Array events;
    events.push_back(json::Value::object(std::move(eventObject)));

    json::Object root;
    root.push_back(json::Member{"client_id", json::Value::string(clientId())});
    // Reports are never used to personalize ads.
    root.push_back(json::Member{"non_personalized_ads", json::Value::boolean(true)});
    root.push_back(json::Member{"events", json::Value::array(std::move(events))});
    return json::serialize(json::Value::object(std::move(root)), 0);
}

void Reporter::record(Event event, std::vector<Param> params) {
    if (!configured() || level() < minimumLevel(event)) return;
    try {
        transport_.post(url(), body(event, params));
    } catch (const std::exception&) {
        // A report that cannot be sent is dropped.
    }
}

void Reporter::recordError(const std::string& domain, const std::string& code, const std::string& where) {
    record(Event::appError, {Param::word("error_domain", domain), Param::word("error_code", code),
                             Param::word("where", where), Param::count("fatal", 0)});
}

void Reporter::sendPendingCrash() {
    std::string text;
    {
        std::ifstream in(markerFile_, std::ios::binary);
        if (!in) return;
        std::stringstream buffer;
        buffer << in.rdbuf();
        text = buffer.str();
    }
    discardPendingCrash();  // removed first, so a crash while sending cannot send the same report twice
    const std::optional<CrashMarker> marker = parseCrashMarker(text);
    if (!marker) return;
    record(Event::appCrash, {Param::word("error_domain", marker->domain), Param::word("error_code", marker->code),
                             Param::word("where", marker->where), Param::count("fatal", 1)});
}

void Reporter::discardPendingCrash() {
    std::error_code ignored;
    std::filesystem::remove(markerFile_, ignored);
}

}  // namespace kettlelogic::app::analytics
