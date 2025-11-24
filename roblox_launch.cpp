#include "roblox_launch.h"
#include "account_manager.h"
#include "log.h"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>

static const wchar_t* kRobloxOrigin = L"https://www.roblox.com";
static const wchar_t* kRobloxReferer = L"https://www.roblox.com/camel";
static const wchar_t* kDesktopUserAgent = L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";

// Helper to convert wide string to string
static std::string WideToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// Helper to convert string to wide string
static std::wstring StringToWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

static bool ExtractHeaderFromRaw(const std::wstring& rawHeaders, const wchar_t* headerName, std::string& outValue) {
    if (rawHeaders.empty() || !headerName) {
        return false;
    }

    std::string utf8Raw = WideToString(rawHeaders);
    std::string lowerRaw = utf8Raw;
    std::transform(lowerRaw.begin(), lowerRaw.end(), lowerRaw.begin(),
        [](unsigned char c) { return static_cast<char>(::tolower(c)); });

    std::wstring headerNameW(headerName);
    std::string lowerHeader = WideToString(headerNameW);
    std::transform(lowerHeader.begin(), lowerHeader.end(), lowerHeader.begin(),
        [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    lowerHeader += ":";

    size_t pos = lowerRaw.find(lowerHeader);
    if (pos == std::string::npos) {
        return false;
    }

    size_t valueStart = pos + lowerHeader.length();
    while (valueStart < utf8Raw.size() && (utf8Raw[valueStart] == ' ' || utf8Raw[valueStart] == '\t')) {
        ++valueStart;
    }

    size_t valueEnd = utf8Raw.find("\r\n", valueStart);
    if (valueEnd == std::string::npos) {
        valueEnd = utf8Raw.size();
    }

    outValue = utf8Raw.substr(valueStart, valueEnd - valueStart);
    return true;
}

static bool QueryCustomHeader(HINTERNET hRequest, const wchar_t* headerName, std::string& outValue) {
    if (!hRequest || !headerName) {
        return false;
    }

    wchar_t buffer[256];
    DWORD bufferLen = sizeof(buffer);
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CUSTOM, headerName, buffer, &bufferLen, WINHTTP_NO_HEADER_INDEX)) {
        outValue = WideToString(std::wstring(buffer));
        return true;
    }

    DWORD error = GetLastError();
    if (error == ERROR_INSUFFICIENT_BUFFER && bufferLen > 0) {
        std::vector<wchar_t> dynamicBuffer(bufferLen / sizeof(wchar_t));
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CUSTOM, headerName, dynamicBuffer.data(), &bufferLen, WINHTTP_NO_HEADER_INDEX)) {
            outValue = WideToString(std::wstring(dynamicBuffer.data()));
            return true;
        }
    }

    DWORD rawLen = 0;
    if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &rawLen, WINHTTP_NO_HEADER_INDEX)) {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || rawLen == 0) {
            return false;
        }
    }

    std::vector<wchar_t> rawBuffer(rawLen / sizeof(wchar_t));
    if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, rawBuffer.data(), &rawLen, WINHTTP_NO_HEADER_INDEX)) {
        return false;
    }

    std::wstring rawHeaders(rawBuffer.data());
    return ExtractHeaderFromRaw(rawHeaders, headerName, outValue);
}

static std::string ExtractQueryValue(const std::string& url, const std::string& key) {
    if (url.empty() || key.empty()) {
        return "";
    }

    std::string lowerUrl = url;
    std::transform(lowerUrl.begin(), lowerUrl.end(), lowerUrl.begin(),
        [](unsigned char c) { return static_cast<char>(::tolower(c)); });

    std::string lowerKey = key;
    std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(),
        [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    lowerKey += "=";

    size_t pos = lowerUrl.find(lowerKey);
    while (pos != std::string::npos) {
        bool boundary = (pos == 0) || lowerUrl[pos - 1] == '?' || lowerUrl[pos - 1] == '&' || lowerUrl[pos - 1] == '#';
        if (boundary) {
            size_t start = pos + lowerKey.length();
            size_t end = start;
            while (end < url.size() && url[end] != '&' && url[end] != '#') {
                ++end;
            }
            return url.substr(start, end - start);
        }
        pos = lowerUrl.find(lowerKey, pos + 1);
    }

    return "";
}

static bool ParseUrl(const std::string& url, std::wstring& hostOut, std::wstring& pathOut) {
    size_t schemePos = url.find("://");
    size_t hostStart = (schemePos == std::string::npos) ? 0 : schemePos + 3;
    if (hostStart >= url.size()) {
        return false;
    }

    size_t hostEnd = url.find('/', hostStart);
    if (hostEnd == std::string::npos) {
        hostEnd = url.size();
    }

    std::string host = url.substr(hostStart, hostEnd - hostStart);
    std::string path = (hostEnd < url.size()) ? url.substr(hostEnd) : "/";

    if (host.empty()) {
        return false;
    }

    hostOut = StringToWide(host);
    pathOut = StringToWide(path);
    return true;
}

static std::string ResolveShareLinkIfNeeded(const std::string& joinLink) {
    if (joinLink.find("/share?") == std::string::npos) {
        return joinLink;
    }

    std::wstring hostW, pathW;
    if (!ParseUrl(joinLink, hostW, pathW)) {
        return joinLink;
    }

    HINTERNET hSession = WinHttpOpen(L"MultiRoblox/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        return joinLink;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, hostW.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return joinLink;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", pathW.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return joinLink;
    }

    const DWORD headerFlags = WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE;
    std::wstring wUserAgent = L"User-Agent: " + std::wstring(kDesktopUserAgent);
    WinHttpAddRequestHeaders(hRequest, wUserAgent.c_str(), -1L, headerFlags);

    std::string resolved = joinLink;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        if (WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD statusCode = 0;
            DWORD statusCodeSize = sizeof(statusCode);
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX)) {
                if (statusCode >= 300 && statusCode < 400) {
                    std::string location;
                    if (QueryCustomHeader(hRequest, L"location", location) && !location.empty()) {
                        if (!location.empty() && location[0] == '/') {
                            resolved = "https://" + WideToString(hostW) + location;
                        } else {
                            resolved = location;
                        }
                    }
                } else if (statusCode == 200) {
                    DWORD urlLen = 0;
                    if (!WinHttpQueryOption(hRequest, WINHTTP_OPTION_URL, NULL, &urlLen) && GetLastError() == ERROR_INSUFFICIENT_BUFFER && urlLen > 0) {
                        std::vector<wchar_t> urlBuf(urlLen / sizeof(wchar_t));
                        if (WinHttpQueryOption(hRequest, WINHTTP_OPTION_URL, urlBuf.data(), &urlLen)) {
                            resolved = WideToString(std::wstring(urlBuf.data()));
                        }
                    }
                }
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resolved;
}

static std::string GetCSRFToken(const char* cookie) {
    HINTERNET hSession = WinHttpOpen(L"MultiRoblox/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        log_win_error("WinHttpOpen (CSRF)", GetLastError());
        return "";
    }

    HINTERNET hConnect = WinHttpConnect(hSession, L"auth.roblox.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        log_win_error("WinHttpConnect (CSRF)", GetLastError());
        WinHttpCloseHandle(hSession);
        return "";
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/v2/logout", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        log_win_error("WinHttpOpenRequest (CSRF)", GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    const DWORD headerFlags = WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE;
    std::string cookieHeader = ".ROBLOSECURITY=" + std::string(cookie);
    std::wstring wCookieHeader = L"Cookie: " + StringToWide(cookieHeader);
    WinHttpAddRequestHeaders(hRequest, wCookieHeader.c_str(), -1L, headerFlags);
    
    // Add User-Agent
    std::wstring wUserAgent = L"User-Agent: " + std::wstring(kDesktopUserAgent);
    WinHttpAddRequestHeaders(hRequest, wUserAgent.c_str(), -1L, headerFlags);

    std::string csrfToken;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpReceiveResponse(hRequest, NULL);
        
        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);

        // 403 is expected for logout endpoint to get CSRF token
        if (statusCode == 403) {
            wchar_t buffer[256];
            DWORD bufferLen = sizeof(buffer);
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CUSTOM, L"x-csrf-token", buffer, &bufferLen, WINHTTP_NO_HEADER_INDEX)) {
                csrfToken = WideToString(buffer);
            } else {
                log_error("CSRF token header missing in 403 response.");
            }
        } else {
            char errorMsg[128];
            snprintf(errorMsg, sizeof(errorMsg), "Unexpected status code for CSRF: %d", statusCode);
            log_error(errorMsg);
        }
    } else {
        log_win_error("WinHttpSendRequest (CSRF)", GetLastError());
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return csrfToken;
}

static std::string GetAuthTicket(const char* cookie, const std::string& csrfToken) {
    HINTERNET hSession = WinHttpOpen(L"MultiRoblox/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    HINTERNET hConnect = WinHttpConnect(hSession, L"auth.roblox.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return "";
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/v1/authentication-ticket", NULL, kRobloxOrigin, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    const DWORD headerFlags = WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE;
    std::string cookieHeader = ".ROBLOSECURITY=" + std::string(cookie);
    std::wstring wCookieHeader = L"Cookie: " + StringToWide(cookieHeader);
    WinHttpAddRequestHeaders(hRequest, wCookieHeader.c_str(), -1L, headerFlags);

    std::wstring wCsrfHeader = L"x-csrf-token: " + StringToWide(csrfToken);
    WinHttpAddRequestHeaders(hRequest, wCsrfHeader.c_str(), -1L, headerFlags);
    
    WinHttpAddRequestHeaders(hRequest, L"Content-Type: application/json", -1L, headerFlags);
    WinHttpAddRequestHeaders(hRequest, L"Accept: application/json", -1L, headerFlags);
    std::wstring wReferer = L"Referer: " + std::wstring(kRobloxReferer);
    WinHttpAddRequestHeaders(hRequest, wReferer.c_str(), -1L, headerFlags);
    std::wstring wOrigin = L"Origin: " + std::wstring(kRobloxOrigin);
    WinHttpAddRequestHeaders(hRequest, wOrigin.c_str(), -1L, headerFlags);
    std::wstring wUserAgent = L"User-Agent: " + std::wstring(kDesktopUserAgent);
    WinHttpAddRequestHeaders(hRequest, wUserAgent.c_str(), -1L, headerFlags);
    WinHttpAddRequestHeaders(hRequest, L"RBXAuthenticationNegotiation: 1", -1L, headerFlags);

    std::string authTicket;
    const char* data = "{}"; // Empty JSON body
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)data, (DWORD)strlen(data), (DWORD)strlen(data), 0)) {
        if (WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD statusCode = 0;
            DWORD statusCodeSize = sizeof(statusCode);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);

            if (statusCode == 200) {
                std::string ticketHeader;
                if (QueryCustomHeader(hRequest, L"rbx-authentication-ticket", ticketHeader)) {
                    authTicket = ticketHeader;
                } else {
                    log_error("Auth ticket header missing in 200 response after inspecting raw headers. Response body:");
                    std::string response;
                    char buffer[4096];
                    DWORD bytesRead = 0;
                    while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
                        response.append(buffer, bytesRead);
                    }
                    log_error(response.c_str());
                }
            } else {
                char errorMsg[128];
                snprintf(errorMsg, sizeof(errorMsg), "Failed to get Auth Ticket. Status: %d", statusCode);
                log_error(errorMsg);
                
                // Read response body for debugging
                std::string response;
                char buffer[4096];
                DWORD bytesRead = 0;
                while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
                    response.append(buffer, bytesRead);
                }
                if (!response.empty()) {
                    log_error(response.c_str());
                }
            }
        }
    } else {
        log_win_error("WinHttpSendRequest (AuthTicket)", GetLastError());
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return authTicket;
}

static std::string ParsePlaceIdFromLink(const std::string& link) {
    bool allDigits = !link.empty() && std::all_of(link.begin(), link.end(), [](unsigned char c) { return isdigit(c); });
    if (allDigits) {
        return link;
    }

    std::string placeId = ExtractQueryValue(link, "placeid");
    if (!placeId.empty()) {
        std::string digits;
        for (char ch : placeId) {
            if (!isdigit(static_cast<unsigned char>(ch))) {
                break;
            }
            digits += ch;
        }
        if (!digits.empty()) {
            return digits;
        }
    }

    const char* markers[] = { "/games/", "/experiences/", "/game/" };
    for (const char* marker : markers) {
        size_t pos = link.find(marker);
        if (pos == std::string::npos) {
            continue;
        }

        size_t start = pos + strlen(marker);
        size_t end = start;
        while (end < link.length() && isdigit(static_cast<unsigned char>(link[end]))) {
            ++end;
        }
        if (end > start) {
            return link.substr(start, end - start);
        }
    }

    return "";
}

static std::string ParsePrivateServerLinkCode(const std::string& link) {
    std::string code = ExtractQueryValue(link, "privateServerLinkCode");
    if (code.empty()) {
        code = ExtractQueryValue(link, "linkCode");
    }
    if (code.empty()) {
        code = ExtractQueryValue(link, "privateServerInvitationId");
    }
    if (code.empty()) {
        // Handle share links formatted as ?code=...&type=Server
        std::string type = ExtractQueryValue(link, "type");
        if (!type.empty()) {
            std::string lowerType = type;
            std::transform(lowerType.begin(), lowerType.end(), lowerType.begin(),
                [](unsigned char c) { return static_cast<char>(::tolower(c)); });
            if (lowerType == "server") {
                code = ExtractQueryValue(link, "code");
            }
        }
        if (code.empty()) {
            code = ExtractQueryValue(link, "code");
        }
    }
    return code;
}

#include <tlhelp32.h>
#include <set>

// Helper to get all currently running RobloxPlayerBeta.exe PIDs
static void GetRobloxPids(std::set<DWORD>& pids) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);
        if (Process32First(hSnapshot, &pe32)) {
            do {
                if (_stricmp(pe32.szExeFile, "RobloxPlayerBeta.exe") == 0) {
                    pids.insert(pe32.th32ProcessID);
                }
            } while (Process32Next(hSnapshot, &pe32));
        }
        CloseHandle(hSnapshot);
    }
}

DWORD RL_LaunchAccount(const RbxAccount* account, const char* joinLink) {
    if (!account || !joinLink) return 0;

    std::string csrf = GetCSRFToken(account->roblosecurity);
    if (csrf.empty()) {
        log_error("Failed to get CSRF token for account");
        return 0;
    }

    std::string ticket = GetAuthTicket(account->roblosecurity, csrf);
    if (ticket.empty()) {
        log_error("Failed to get Auth Ticket for account");
        return 0;
    }

    std::string originalLink = joinLink ? std::string(joinLink) : std::string();
    std::string userPlaceId = ExtractQueryValue(originalLink, "placeId");

    std::string resolvedLink = ResolveShareLinkIfNeeded(originalLink);

    std::string placeId = ParsePlaceIdFromLink(resolvedLink);
    if (placeId.empty() && !userPlaceId.empty()) {
        placeId = userPlaceId;
    }

    std::string linkCode = ParsePrivateServerLinkCode(resolvedLink);
    if (linkCode.empty() && originalLink.find("/share?") != std::string::npos) {
        std::string shareCode = ExtractQueryValue(originalLink, "code");
        if (!shareCode.empty()) {
            linkCode = shareCode;
        }
    }

    if (placeId.empty()) {
        char errorMsg[512];
        _snprintf(errorMsg, sizeof(errorMsg), "Could not parse Place ID from link: %.300s", joinLink ? joinLink : "(null)");
        log_error(errorMsg);
        return 0;
    }

    // Capture existing Roblox PIDs BEFORE launching
    std::set<DWORD> existingPids;
    GetRobloxPids(existingPids);

    // Construct roblox-player: URL
    // Format: roblox-player:1+launchmode:play+gameinfo:TICKET+launchtime:TIME+browsertrackerid:ID+robloxLocale:en_us+gameLocale:en_us+channel:+launch_exp:InApp+placelauncherurl:https%3A%2F%2Fassetgame.roblox.com%2Fgame%2FPlaceLauncher.ashx%3Frequest%3DRequestPrivateGame%26browserTrackerId%3DID%26placeId%3DPLACEID%26linkCode%3DLINKCODE
    
    std::stringstream ss;
    ss << "roblox-player:1+launchmode:play+gameinfo:" << ticket;
    ss << "+launchtime:" << time(NULL) * 1000;
    ss << "+browsertrackerid:123456789"; // Dummy ID
    ss << "+robloxLocale:en_us+gameLocale:en_us+channel:+launch_exp:InApp";
    
    if (!linkCode.empty()) {
        // Private server launch; include both linkCode and accessCode for compatibility
        ss << "+placelauncherurl:https%3A%2F%2Fassetgame.roblox.com%2Fgame%2FPlaceLauncher.ashx%3Frequest%3DRequestPrivateGame"
           "%26browserTrackerId%3D123456789"
           "%26placeId%3D" << placeId <<
           "%26linkCode%3D" << linkCode <<
           "%26accessCode%3D" << linkCode <<
           "%26privateServerLinkCode%3D" << linkCode;
    } else {
        // Standard play launch (if no private link code)
        ss << "+placelauncherurl:https%3A%2F%2Fassetgame.roblox.com%2Fgame%2FPlaceLauncher.ashx%3Frequest%3DRequestGame%26browserTrackerId%3D123456789%26placeId%3D" << placeId;
    }

    std::string launchUrl = ss.str();

    // Log the final URL (sanitized) for debugging access issues (e.g., 524)
    {
        std::string logMsg = "Launching placeId=" + placeId;
        if (!linkCode.empty()) {
            logMsg += " linkCode=" + linkCode;
        }
        logMsg += " resolvedLink=" + resolvedLink;
        logMsg += " url=" + launchUrl;
        log_error(logMsg.c_str());
    }
    
    // Use ShellExecuteEx to get the process handle of the launcher
    SHELLEXECUTEINFOA sei = {0};
    sei.cbSize = sizeof(SHELLEXECUTEINFOA);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = "open";
    sei.lpFile = launchUrl.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExA(&sei)) {
        char errBuf[256];
        _snprintf(errBuf, sizeof(errBuf), "ShellExecuteEx failed (%lu) for launch URL.", GetLastError());
        log_error(errBuf);
        return 0;
    }

    if (sei.hProcess) {
        CloseHandle(sei.hProcess);
    }

    // Poll for NEW Roblox process
    DWORD newPid = 0;
    // Wait up to 30 seconds (launcher update might take time)
    for (int i = 0; i < 60; ++i) {
        Sleep(500);
        
        std::set<DWORD> currentPids;
        GetRobloxPids(currentPids);

        for (DWORD pid : currentPids) {
            if (existingPids.find(pid) == existingPids.end()) {
                // Found a new process!
                newPid = pid;
                break;
            }
        }

        if (newPid != 0) break;
    }

    if (newPid == 0) {
        log_error("Timed out waiting for new RobloxPlayerBeta.exe process.");
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "Detected new Roblox process: %lu", newPid);
        log_error(msg);
    }

    return newPid;
}
