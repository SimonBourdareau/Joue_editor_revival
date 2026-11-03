#include "updatecheck.hpp"

#include <cstdio>
#include <cstdlib>
#include <thread>

#include "joue/update.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

namespace {

bool safeRepository(const std::string& repository) {
    if (repository.empty() || repository.find('/') == std::string::npos) return false;
    for (char c : repository) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == '/';
        if (!ok) return false;
    }
    return true;
}

#if defined(_WIN32)
std::string fetchLatest(const std::string& repository) {
    std::string result;
    HINTERNET session = WinHttpOpen(L"JoueRevival", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return result;
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);
    HINTERNET connection = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connection) {
        std::wstring path = L"/repos/" + std::wstring(repository.begin(), repository.end()) + L"/releases/latest";
        HINTERNET request = WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (request) {
            if (WinHttpSendRequest(request, L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr)) {
                DWORD status = 0, size = sizeof status;
                WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
                while (status == 200) {
                    DWORD available = 0;
                    if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
                    std::string chunk(available, '\0');
                    DWORD read = 0;
                    if (!WinHttpReadData(request, &chunk[0], available, &read)) break;
                    chunk.resize(read);
                    result += chunk;
                }
            }
            WinHttpCloseHandle(request);
        }
        WinHttpCloseHandle(connection);
    }
    WinHttpCloseHandle(session);
    return result;
}
#else
std::string fetchLatest(const std::string& repository) {
    std::string command = "curl -fsSL --max-time 6 -H 'Accept: application/vnd.github+json' -A JoueRevival 'https://api.github.com/repos/" + repository + "/releases/latest' 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return {};
    std::string out;
    char buffer[4096];
    std::size_t n;
    while ((n = std::fread(buffer, 1, sizeof buffer, pipe)) > 0) out.append(buffer, n);
    pclose(pipe);
    return out;
}
#endif

}  // namespace

void UpdateCheck::start(const std::string& repository) {
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        if (shared_->running) return;
        shared_->running = true;
        shared_->ready = false;
    }
    std::shared_ptr<Shared> shared = shared_;
    std::thread([shared, repository] {
        std::string json = safeRepository(repository) ? fetchLatest(repository) : std::string();
        std::string tag = joue::jsonStringValue(json, "tag_name"), url = joue::jsonStringValue(json, "html_url");
        std::lock_guard<std::mutex> lock(shared->mutex);
        shared->ok = !tag.empty();
        shared->tag = tag;
        shared->url = url;
        shared->ready = true;
        shared->running = false;
    }).detach();
}

bool UpdateCheck::running() {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    return shared_->running;
}

bool UpdateCheck::takeResult(bool& ok, std::string& tag, std::string& url) {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    if (!shared_->ready) return false;
    shared_->ready = false;
    ok = shared_->ok;
    tag = shared_->tag;
    url = shared_->url;
    return true;
}

void openExternal(const std::string& target) {
#if defined(_WIN32)
    ShellExecuteA(nullptr, "open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    std::string quoted = "'";
    for (char c : target) {
        if (c == '\'') quoted += "'\\''";
        else quoted += c;
    }
    quoted += "'";
#if defined(__APPLE__)
    std::string command = "open " + quoted + " >/dev/null 2>&1 &";
#else
    std::string command = "xdg-open " + quoted + " >/dev/null 2>&1 &";
#endif
    if (std::system(command.c_str()) != 0) return;
#endif
}

std::string executableDirectory() {
    std::string path;
#if defined(_WIN32)
    char buffer[MAX_PATH * 2] = {0};
    DWORD n = GetModuleFileNameA(nullptr, buffer, sizeof buffer);
    path.assign(buffer, n);
#elif defined(__APPLE__)
    char buffer[4096] = {0};
    uint32_t size = sizeof buffer;
    if (_NSGetExecutablePath(buffer, &size) == 0) path = buffer;
#else
    char buffer[4096] = {0};
    ssize_t n = readlink("/proc/self/exe", buffer, sizeof buffer - 1);
    if (n > 0) path.assign(buffer, static_cast<std::size_t>(n));
#endif
    std::size_t cut = path.find_last_of("/\\");
    return cut == std::string::npos ? std::string(".") : path.substr(0, cut);
}
