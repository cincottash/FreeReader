#include "StartupTrace.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

void startupTrace(const char* stage)
{
    const char* tmp = std::getenv("TEMP");
    if (!tmp) tmp = std::getenv("TMPDIR");
    if (!tmp) tmp = "/tmp";
    char path[1024];
    std::snprintf(path, sizeof path, "%s/FreeReader-startup.log", tmp);
    if (FILE* probe = std::fopen(path, "rb")) {
        std::fseek(probe, 0, SEEK_END);
        const long size = std::ftell(probe);
        std::fclose(probe);
        if (size > 256 * 1024) std::remove(path);
    }
    FILE* f = std::fopen(path, "ab");
    if (!f) return;
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const int ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    char when[32];
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = int(getpid());
#endif
    std::fprintf(f, "%s.%03d  pid %d  %s\n", when, ms, pid, stage);
    std::fclose(f);
}
