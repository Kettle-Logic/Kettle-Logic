#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>

// Whole-app updates for the Windows and Linux apps. The update list (updates.json, see docs/updates.md) says where
// the newest app is. This module reads it and decides whether it is newer. It has no platform API: each app supplies
// a `Transport` that fetches the list, and its own UI, and it decides whether to install a download.
namespace kettlelogic::app::updates {

// The app's version. Keep it equal to the Linux app's project version (apps/linux/CMakeLists.txt) and the Windows
// app's KettleLogicUiVersion (apps/windows/KettleLogic/KettleLogic.Paths.props). The tests check that they agree.
inline constexpr const char* kAppVersion = "0.1.0";

// Where the update list lives. A build can point elsewhere with KETTLELOGIC_UPDATE_MANIFEST_URL.
#ifdef KETTLELOGIC_UPDATE_MANIFEST_URL
inline constexpr const char* kManifestUrl = KETTLELOGIC_UPDATE_MANIFEST_URL;
#else
inline constexpr const char* kManifestUrl =
    "https://github.com/kettle-logic/kettle-logic/releases/download/updates/updates.json";
#endif

// A semantic version: major.minor.patch, the numbers only.
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;

    // Parses "1.2.3", "1.2", or "1". Returns nothing for anything else.
    static std::optional<Version> parse(std::string_view text);
    std::string text() const;

    friend auto operator<=>(const Version&, const Version&) = default;
};

enum class Platform { windows, linux };

// One platform's newest app. `sha256` is the lowercase hex digest of the installer; a Linux release has none, because
// the Flatpak runtime installs it. `url` is the file to download (Windows), or the release page (Linux).
struct Release {
    Version version;
    std::string url;
    std::string sha256;
};

struct Manifest {
    std::optional<Release> windows;
    std::optional<Release> linux;
};

// Reads updates.json. Entries for other platforms are ignored, so one list serves them all. Throws
// std::runtime_error when the text is not a list this app can read, including a list with a newer schema.
Manifest parseManifest(std::string_view json);

// The release for `platform`, if it is newer than `current`.
std::optional<Release> newerRelease(const Manifest& manifest, Platform platform, const Version& current);

// Whether `bytes` hash to `expectedHex` (lowercase, as the manifest gives it).
bool matchesSha256(std::string_view bytes, std::string_view expectedHex);

// How the app fetches the update list. Blocking: the caller runs it off the UI thread.
class Transport {
public:
    virtual ~Transport() = default;
    // Returns the body of a successful GET. Throws std::runtime_error on any other outcome.
    virtual std::string get(const std::string& url) = 0;
};

struct CheckResult {
    enum class Status { upToDate, available, failed };
    Status status = Status::failed;
    std::optional<Release> release;  // set when `status` is available
    std::string message;             // set when `status` is failed
};

// Fetches the list through `transport` and compares it with `kAppVersion`. Never throws.
CheckResult check(Transport& transport, Platform platform, const std::string& manifestUrl = kManifestUrl);

}  // namespace kettlelogic::app::updates
