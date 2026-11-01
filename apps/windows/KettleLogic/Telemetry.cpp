#include "pch.h"

#include "Telemetry.h"

#include "Platform.h"

#include "kettlelogic/app/app_updates.hpp"
#include "kettlelogic/version.hpp"

#include <winhttp.h>

#include <atomic>
#include <memory>
#include <stdexcept>

#pragma comment(lib, "winhttp.lib")

namespace kl::telemetry {

namespace {

namespace analytics = kettlelogic::app::analytics;
namespace updates = kettlelogic::app::updates;

std::wstring Wide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int size = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);
    return wide;
}

// Closes a WinHTTP handle when it goes out of scope.
struct HttpHandle {
    HINTERNET handle = nullptr;
    ~HttpHandle() {
        if (handle != nullptr) WinHttpCloseHandle(handle);
    }
};

// Posts through WinHTTP. Blocking, so it only runs on the queue's worker thread. Every step times out after 5 seconds,
// so closing the app never waits long.
class WinHttpTransport : public analytics::Transport {
public:
    void post(const std::string& url, const std::string& jsonBody) override {
        const std::wstring wideUrl = Wide(url);
        wchar_t host[256];
        wchar_t path[2048];
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof parts;
        parts.lpszHostName = host;
        parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
        parts.lpszUrlPath = path;
        parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
        // The query holds the ids, so it is part of the path to send.
        wchar_t extra[512];
        parts.lpszExtraInfo = extra;
        parts.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
        if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts)) {
            throw std::runtime_error("the report address is not valid");
        }
        const std::wstring target = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) +
                                    std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);

        const std::wstring agent = L"KettleLogic/" + Wide(updates::kAppVersion);
        HttpHandle session{WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0)};
        if (session.handle == nullptr) throw std::runtime_error("the network could not be opened");
        WinHttpSetTimeouts(session.handle, 5000, 5000, 5000, 5000);

        HttpHandle connection{WinHttpConnect(session.handle, std::wstring(parts.lpszHostName, parts.dwHostNameLength).c_str(),
                                             parts.nPort, 0)};
        if (connection.handle == nullptr) throw std::runtime_error("the report server could not be reached");

        HttpHandle request{WinHttpOpenRequest(connection.handle, L"POST", target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)};
        if (request.handle == nullptr) throw std::runtime_error("the report request could not be made");

        const std::wstring headers = L"Content-Type: application/json";
        if (!WinHttpSendRequest(request.handle, headers.c_str(), static_cast<DWORD>(headers.size()),
                                const_cast<char*>(jsonBody.data()), static_cast<DWORD>(jsonBody.size()),
                                static_cast<DWORD>(jsonBody.size()), 0) ||
            !WinHttpReceiveResponse(request.handle, nullptr)) {
            throw std::runtime_error("the report server did not answer");
        }
    }
};

// What `Start` builds. It lives until `Stop`.
struct State {
    WinHttpTransport http;
    analytics::QueuedTransport queue{http};
    analytics::Reporter reporter;

    State(kettlelogic::app::Preferences& preferences, analytics::Context context, std::filesystem::path marker)
        : reporter(preferences, queue, std::move(context), std::move(marker)) {}
};

std::unique_ptr<State> gState;

// The crash marker's path, ready before any crash, because the crash filter must not allocate.
wchar_t gMarkerPath[MAX_PATH * 2] = {};

// Whether a crash leaves a marker: set while the level sends crash reports.
std::atomic<bool> gLeaveMarker{false};

std::filesystem::path MarkerFile() {
    return platform::DataFolder() / L"crash-report.txt";
}

analytics::Context MakeContext() {
    analytics::Context context;
    context.platform = "windows";
    context.appVersion = updates::kAppVersion;
    context.engineVersion = kettlelogic::kEngineVersion;

    // Windows 10 and 11 both report major version 10, so the build number tells them apart: 22000 and later is 11.
    OSVERSIONINFOEXW info{sizeof info};
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    if (HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll")) {
        if (auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(::GetProcAddress(ntdll, "RtlGetVersion"))) {
            rtlGetVersion(reinterpret_cast<PRTL_OSVERSIONINFOW>(&info));
            context.osVersion = info.dwMajorVersion == 10 && info.dwBuildNumber >= 22000
                                    ? "11"
                                    : std::to_string(info.dwMajorVersion);
        }
    }

    // The language only, such as "en" from "en-US".
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (::GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0) {
        std::wstring locale = name;
        locale = locale.substr(0, locale.find_first_of(L"-_"));
        context.language = platform::U(locale);
    }
    return context;
}

// Writes "exception <code> app" to the marker with plain Win32 calls, which is as much as a crash filter should do.
void WriteCrashMarker(DWORD code) {
    char line[48] = "exception ";
    std::size_t length = 10;
    static const char hex[] = "0123456789abcdef";
    for (int shift = 28; shift >= 0; shift -= 4) line[length++] = hex[(code >> shift) & 0xF];
    static const char tail[] = " app\n";
    for (const char c : tail) {
        if (c != '\0') line[length++] = c;
    }
    const HANDLE file = ::CreateFileW(gMarkerPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        ::WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
        ::CloseHandle(file);
    }
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS* info) {
    if (gLeaveMarker.load() && info != nullptr && info->ExceptionRecord != nullptr) {
        WriteCrashMarker(info->ExceptionRecord->ExceptionCode);
    }
    return EXCEPTION_CONTINUE_SEARCH;  // Windows ends the app as it would have
}

void InstallCrashFilter() {
    const std::wstring path = MarkerFile().wstring();
    if (path.size() >= std::size(gMarkerPath)) return;
    path.copy(gMarkerPath, path.size());
    gMarkerPath[path.size()] = L'\0';
    ::SetUnhandledExceptionFilter(OnCrash);
}

bool Sends() {
    return gState && gState->reporter.configured() && gState->reporter.level() != analytics::Level::off;
}

}  // namespace

void Start(kettlelogic::app::Preferences& preferences) {
    if (gState) return;
    std::error_code ignored;
    std::filesystem::create_directories(platform::DataFolder(), ignored);
    gState = std::make_unique<State>(preferences, MakeContext(), MarkerFile());
    InstallCrashFilter();
    gLeaveMarker = Sends();
    // At Off the old marker is removed unsent, and nothing is sent now.
    gState->reporter.sendPendingCrash();
    gState->reporter.record(analytics::Event::appOpen);
}

void Stop() {
    gLeaveMarker = false;
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
    gLeaveMarker = Sends();
    if (!Sends()) gState->reporter.discardPendingCrash();
}

void ResetId() {
    if (gState) gState->reporter.resetClientId();
}

}  // namespace kl::telemetry
