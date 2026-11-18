#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace mem {
    inline HANDLE  proc = nullptr;
    inline DWORD   pid  = 0;
    inline uintptr_t base = 0;
    inline size_t  img_size = 0;
    inline std::string last_error;

    DWORD find_pid(const wchar_t* name);
    bool  attach(const wchar_t* name);
    bool  read(uintptr_t addr, void* out, size_t size);
    bool  write(uintptr_t addr, const void* in, size_t size);

    template <class T> T    rd(uintptr_t a)         { T v{}; read(a, &v, sizeof(T)); return v; }
    template <class T> bool rd(uintptr_t a, T& out) { return read(a, &out, sizeof(T)); }

    std::string  read_str(uintptr_t addr, size_t max = 96);
    uintptr_t    module_base(const wchar_t* mod);
    bool         valid(uintptr_t p);
    bool         alive();
    bool         is_admin();
    std::wstring list_matching(const wchar_t* needle);
    std::wstring list_modules();
    std::vector<std::pair<uintptr_t, size_t>> regions(uintptr_t lo = 0x10000, uintptr_t hi = 0x7FFFFFFFFFFF);
}