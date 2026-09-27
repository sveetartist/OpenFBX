#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <array>
#include <regex>
#include <memory>
#include "UpdateChecker.h"

namespace openfbx {
UpdateResult CompareReleaseVersion(const std::string& tag, const std::string& current)
{
    const std::regex pattern(R"(^[vV]?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(\+[0-9A-Za-z.-]+)?$)");
    std::smatch latestMatch, currentMatch;
    if (!std::regex_match(tag, latestMatch, pattern) || !std::regex_match(current, currentMatch, pattern))
        return {"Release tag must use vMAJOR.MINOR.PATCH."};
    try {
        std::array<unsigned long long, 3> latest{}, installed{};
        for (int i = 0; i < 3; ++i) {
            latest[i] = std::stoull(latestMatch[i + 1].str());
            installed[i] = std::stoull(currentMatch[i + 1].str());
        }
        if (latest > installed) return {"Update available: " + tag, true};
        return {"You are up to date (latest release: " + tag + ")."};
    } catch (...) { return {"Could not compare the release version."}; }
}

UpdateResult CheckForUpdates(const std::string& current)
{
    using Handle = std::unique_ptr<void, decltype(&WinHttpCloseHandle)>;
    const UpdateResult failed{"Could not check for updates. Check your connection and retry."};
    Handle session(WinHttpOpen(L"OpenFBX Update Checker", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0), WinHttpCloseHandle);
    if (!session) return failed;
    WinHttpSetTimeouts(session.get(), 5000, 5000, 5000, 5000);
    Handle connection(WinHttpConnect(session.get(), L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0), WinHttpCloseHandle);
    if (!connection) return failed;
    Handle request(WinHttpOpenRequest(connection.get(), L"GET", L"/repos/sveetartist/OpenFBX/releases/latest",
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE), WinHttpCloseHandle);
    if (!request) return failed;
    if (!WinHttpSendRequest(request.get(), L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1),
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.get(), nullptr)) return failed;
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) return failed;
    if (status == 404) return {"No public release found on GitHub."};
    if (status == 403 || status == 429) return {"GitHub request limit reached. Please try again later."};
    if (status != 200) return {"GitHub returned HTTP " + std::to_string(status) + ". Please retry later."};
    std::string body;
    char buffer[8192];
    const ULONGLONG deadline = GetTickCount64() + 15000;
    for (;;) {
        DWORD count = 0;
        if (GetTickCount64() > deadline || !WinHttpReadData(request.get(), buffer, sizeof(buffer), &count)) return failed;
        if (!count) break;
        body.append(buffer, count);
        if (body.size() > 2 * 1024 * 1024) return {"GitHub response was too large."};
    }
    std::smatch match;
    if (!std::regex_search(body, match, std::regex(R"tag("tag_name"\s*:\s*"([^"\\]*)")tag")))
        return {"Could not read the GitHub release version."};
    return CompareReleaseVersion(match[1].str(), current);
}
}
