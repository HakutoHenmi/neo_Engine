#pragma once
#include <Windows.h>
#include <cstdio>

namespace Engine::StartupTiming {
inline bool Enabled() {
    static const bool enabled = wcsstr(GetCommandLineW(), L"--startup-smoke") ||
        wcsstr(GetCommandLineW(), L"--startup-profile");
    return enabled;
}
inline double ProcessSeconds() {
    FILETIME created{}, exited{}, kernel{}, user{}, now{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
    GetSystemTimePreciseAsFileTime(&now);
    ULARGE_INTEGER start{}, end{};
    start.LowPart = created.dwLowDateTime; start.HighPart = created.dwHighDateTime;
    end.LowPart = now.dwLowDateTime; end.HighPart = now.dwHighDateTime;
    return double(end.QuadPart - start.QuadPart) / 10000000.0;
}
inline void Mark(const char* stage) {
    if (!Enabled()) return;
    static bool first = true;
    FILE* file = nullptr;
    fopen_s(&file, "tests/out/startup-timing.txt", first ? "w" : "a");
    if (file) { fprintf(file, "%.3f %s\n", ProcessSeconds(), stage); fclose(file); first = false; }
}
}
