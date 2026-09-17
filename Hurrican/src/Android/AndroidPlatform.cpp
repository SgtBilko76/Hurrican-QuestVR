// Android (Meta Quest) platform glue, see AndroidPlatform.hpp

#include "AndroidPlatform.hpp"

#include <android/log.h>
#include <SDL.h>

#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <pthread.h>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr const char *LOG_TAG = "Hurrican";
constexpr const char *ASSET_LIST = "asset_list.txt";
constexpr const char *ASSET_VERSION = "asset_version.txt";
// NOTE: SDL_RWFromFile()/SDL_LoadFile() on Android look in the internal storage directory
// *before* the APK assets, so anything we write there must not shadow an APK asset name.
constexpr const char *INSTALLED_STAMP = "installed_assets.stamp";

void *StdioLogThread(void *arg) {
    int fd = *static_cast<int *>(arg);
    delete static_cast<int *>(arg);

    std::string line;
    char buf[1024];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                if (!line.empty())
                    __android_log_write(ANDROID_LOG_INFO, LOG_TAG, line.c_str());
                line.clear();
            } else {
                line.push_back(buf[i]);
            }
        }
    }
    return nullptr;
}

// Read a whole APK asset (relative path) into memory via SDL's asset-aware RWops.
bool LoadAsset(const std::string &rel, std::vector<char> &out) {
    size_t size = 0;
    void *data = SDL_LoadFile(rel.c_str(), &size);
    if (data == nullptr)
        return false;
    out.assign(static_cast<char *>(data), static_cast<char *>(data) + size);
    SDL_free(data);
    return true;
}

bool ReadTextFile(const fs::path &path, std::string &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::stringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

std::string Trim(const std::string &s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    size_t e = s.find_last_not_of(" \t\r\n");
    if (b == std::string::npos)
        return "";
    return s.substr(b, e - b + 1);
}

// Stream one asset from the APK to `dest` (64 KiB chunks, so big textures don't
// need a second in-memory copy).
bool CopyAsset(const std::string &rel, const fs::path &dest) {
    {
        std::error_code ec;
        fs::create_directories(dest.parent_path(), ec);
        fs::remove(dest, ec);  // must not exist while we read: SDL would open it instead of the APK asset
    }
    SDL_RWops *rw = SDL_RWFromFile(rel.c_str(), "rb");
    if (rw == nullptr) {
        AndroidPlatform::Log("asset missing in APK: %s (%s)", rel.c_str(), SDL_GetError());
        return false;
    }

    FILE *out = fopen(dest.string().c_str(), "wb");
    if (out == nullptr) {
        AndroidPlatform::Log("cannot write %s", dest.string().c_str());
        SDL_RWclose(rw);
        return false;
    }

    static char chunk[64 * 1024];
    bool ok = true;
    size_t n;
    while ((n = SDL_RWread(rw, chunk, 1, sizeof(chunk))) > 0) {
        if (fwrite(chunk, 1, n, out) != n) {
            ok = false;
            break;
        }
    }
    fclose(out);
    SDL_RWclose(rw);
    return ok;
}

}  // namespace

namespace AndroidPlatform {

void RedirectStdioToLogcat() {
    static bool done = false;
    if (done)
        return;
    done = true;

    int pipefd[2];
    if (pipe(pipefd) != 0)
        return;

    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);

    int *readFd = new int(pipefd[0]);
    pthread_t thread;
    if (pthread_create(&thread, nullptr, StdioLogThread, readFd) == 0)
        pthread_detach(thread);
}

void Log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, LOG_TAG, fmt, ap);
    va_end(ap);
}

std::string GetInternalStoragePath() {
    const char *p = SDL_AndroidGetInternalStoragePath();
    if (p == nullptr) {
        Log("SDL_AndroidGetInternalStoragePath failed: %s", SDL_GetError());
        return "/data/local/tmp/hurrican";
    }
    return p;
}

bool PrepareAssets(const std::string &base) {
    const fs::path root(base);
    {
        // leftovers from older builds that would shadow the APK files
        std::error_code ec;
        fs::remove(root / ASSET_VERSION, ec);
        fs::remove(root / ASSET_LIST, ec);
    }
    std::vector<char> stampData;
    if (!LoadAsset(ASSET_VERSION, stampData)) {
        Log("APK has no %s - was the Gradle syncGameAssets task run?", ASSET_VERSION);
        return false;
    }
    const std::string apkStamp = Trim(std::string(stampData.begin(), stampData.end()));

    std::string installedStamp;
    if (ReadTextFile(root / INSTALLED_STAMP, installedStamp) && Trim(installedStamp) == apkStamp &&
        fs::is_directory(root / "data") && fs::is_directory(root / "lang")) {
        Log("game data up to date (%s) in %s", apkStamp.c_str(), base.c_str());
        return true;
    }

    std::vector<char> listData;
    if (!LoadAsset(ASSET_LIST, listData)) {
        Log("APK has no %s", ASSET_LIST);
        return false;
    }

    Log("extracting game data (%s) to %s ...", apkStamp.c_str(), base.c_str());
    std::error_code ec;
    fs::remove(root / INSTALLED_STAMP, ec);  // a partial extraction must not count as complete

    std::istringstream list(std::string(listData.begin(), listData.end()));
    std::string line;
    int count = 0;
    int failed = 0;
    while (std::getline(list, line)) {
        line = Trim(line);
        if (line.empty())
            continue;
        const size_t tab = line.find('\t');
        const std::string rel = (tab == std::string::npos) ? line : line.substr(0, tab);
        if (!CopyAsset(rel, root / rel))
            failed++;
        if (++count % 100 == 0)
            Log("  %d files extracted ...", count);
    }

    if (failed > 0) {
        Log("extraction finished with %d failures (%d files)", failed, count);
        return false;
    }

    std::ofstream stamp(root / INSTALLED_STAMP, std::ios::binary | std::ios::trunc);
    stamp << apkStamp << '\n';
    Log("extracted %d files", count);
    return true;
}

}  // namespace AndroidPlatform
