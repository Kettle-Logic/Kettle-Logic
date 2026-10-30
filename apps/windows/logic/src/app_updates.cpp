#include "kettlelogic/app/app_updates.hpp"

#include "kettlelogic/crypto/sha256.hpp"
#include "kettlelogic/json.hpp"

#include <charconv>
#include <stdexcept>

namespace kettlelogic::app::updates {

namespace {

// The layout this app reads. A list with a newer schema is refused, rather than read wrongly.
constexpr int kSupportedSchema = 1;

std::optional<int> parseNumber(std::string_view text) {
    // Digits only: from_chars would also take a sign, which a version part never has.
    if (text.empty() || text.size() > 6 || text.front() < '0' || text.front() > '9') {
        return std::nullopt;
    }
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

// The entry for one platform, or nothing when the list has none. A malformed entry is an error, not an absence.
std::optional<Release> readRelease(const json::Value& root, const char* key, bool needsHash) {
    const json::Value* entry = root.find(key);
    if (entry == nullptr || entry->isNull()) {
        return std::nullopt;
    }
    if (!entry->isObject()) {
        throw std::runtime_error(std::string("the update list's \"") + key + "\" entry is not an object");
    }
    const json::Value* version = entry->find("version");
    const json::Value* url = entry->find("url");
    if (version == nullptr || !version->isString() || url == nullptr || !url->isString()) {
        throw std::runtime_error(std::string("the update list's \"") + key + "\" entry needs a version and a url");
    }
    Release release;
    const std::optional<Version> parsed = Version::parse(version->asString());
    if (!parsed) {
        throw std::runtime_error("the update list gives the version \"" + version->asString() + "\", which is not valid");
    }
    release.version = *parsed;
    release.url = url->asString();

    const json::Value* sha = entry->find("sha256");
    if (sha != nullptr && sha->isString()) {
        release.sha256 = sha->asString();
    }
    if (needsHash && release.sha256.size() != 64) {
        throw std::runtime_error(std::string("the update list's \"") + key + "\" entry needs a sha256");
    }
    return release;
}

}  // namespace

std::optional<Version> Version::parse(std::string_view text) {
    Version version;
    int* parts[] = {&version.major, &version.minor, &version.patch};
    int count = 0;
    std::size_t start = 0;
    while (true) {
        const std::size_t dot = text.find('.', start);
        const std::string_view part = text.substr(start, dot == std::string_view::npos ? dot : dot - start);
        const std::optional<int> number = parseNumber(part);
        if (!number || count >= 3) {
            return std::nullopt;
        }
        *parts[count++] = *number;
        if (dot == std::string_view::npos) {
            break;
        }
        start = dot + 1;
    }
    return version;
}

std::string Version::text() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

Manifest parseManifest(std::string_view json) {
    json::Value root;
    try {
        root = json::parse(json);
    } catch (const json::ParseError&) {
        throw std::runtime_error("the update list is not valid JSON");
    }
    if (!root.isObject()) {
        throw std::runtime_error("the update list is not an object");
    }
    const json::Value* schema = root.find("schema");
    if (schema == nullptr || !schema->isNumber()) {
        throw std::runtime_error("the update list has no schema");
    }
    const std::optional<int> number = parseNumber(schema->numberLexeme());
    if (!number || *number > kSupportedSchema) {
        throw std::runtime_error("the update list is for a newer version of Kettle Logic. Download the latest version from its GitHub page.");
    }

    Manifest manifest;
    manifest.windows = readRelease(root, "windows", /*needsHash=*/true);
    manifest.linux = readRelease(root, "linux", /*needsHash=*/false);
    return manifest;
}

std::optional<Release> newerRelease(const Manifest& manifest, Platform platform, const Version& current) {
    const std::optional<Release>& release = platform == Platform::windows ? manifest.windows : manifest.linux;
    if (!release || !(release->version > current)) {
        return std::nullopt;
    }
    return release;
}

bool matchesSha256(std::string_view bytes, std::string_view expectedHex) {
    return crypto::sha256Hex(bytes) == expectedHex;
}

CheckResult check(Transport& transport, Platform platform, const std::string& manifestUrl) {
    CheckResult result;
    try {
        const Manifest manifest = parseManifest(transport.get(manifestUrl));
        const std::optional<Version> current = Version::parse(kAppVersion);
        if (!current) {
            throw std::runtime_error("this build's version is not valid");
        }
        if (std::optional<Release> release = newerRelease(manifest, platform, *current)) {
            result.status = CheckResult::Status::available;
            result.release = std::move(release);
        } else {
            result.status = CheckResult::Status::upToDate;
        }
    } catch (const std::exception& error) {
        result.status = CheckResult::Status::failed;
        result.message = error.what();
    }
    return result;
}

}  // namespace kettlelogic::app::updates
