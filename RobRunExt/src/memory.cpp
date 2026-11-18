#include "memory.hpp"
#include <TlHelp32.h>
#include <psapi.h>
#include <unordered_set>

namespace mem {

static std::string g_log;
static void logm(const std::string& s) { g_log += "  " + s + "\n"; }
static std::string hex(uintptr_t v) {
    char b[32]; sprintf_s(b, "0x%llX", (unsigned long long)v); return b;
}

DWORD find_pid(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{ sizeof(pe) };
    DWORD found = 0;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        if (!_wcsicmp(pe.szExeFile, name)) { found = pe.th32ProcessID; break; }
    CloseHandle(snap);
    return found;
}

std::wstring list_matching(const wchar_t* needle) {
    std::wstring out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return L"(snapshot failed)";
    PROCESSENTRY32W pe{ sizeof(pe) };
    int n = 0;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        std::wstring low = pe.szExeFile;
        for (auto& c : low) c = towlower(c);
        if (low.find(needle) != std::wstring::npos) {
            out += L"  " + std::wstring(pe.szExeFile) + L" (pid " + std::to_wstring(pe.th32ProcessID) + L")\n";
            if (++n >= 12) break;
        }
    }
    CloseHandle(snap);
    return n ? out : L"  (none)\n";
}

bool is_admin() {
    BOOL admin = FALSE;
    PSID sid = nullptr;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &sid)) {
        CheckTokenMembership(nullptr, sid, &admin);
        FreeSid(sid);
    }
    return admin != FALSE;
}

static uintptr_t peb_base() {
    struct PBI { PVOID r1; PVOID Peb; PVOID r2[2]; ULONG_PTR pid; PVOID r3; };
    PBI pbi{};
    using Fn = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    static Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");
    if (!fn) return 0;
    ULONG len = 0;
    if (fn(proc, 0, &pbi, sizeof(pbi), &len) != 0) return 0;
    return (uintptr_t)pbi.Peb;
}

static std::wstring entry_name(uintptr_t entry) {
    uint16_t len = rd<uint16_t>(entry + 0x58);
    uintptr_t buf = rd<uintptr_t>(entry + 0x60);
    if (!len || len > 512 || !buf) return {};
    wchar_t name[256]{};
    if (!read(buf, name, len)) return {};
    return std::wstring(name, len / 2);
}

static bool walk_ldr(const wchar_t* want, uintptr_t* out_base, std::wstring* dump) {
    uintptr_t peb = peb_base();
    logm("peb_base = " + hex(peb));
    if (!peb) { logm("NtQueryInformationProcess failed"); return false; }

    uintptr_t ldr = 0;
    if (!read(peb + 0x18, &ldr, sizeof(ldr))) { logm("ReadProcessMemory(peb+0x18) failed"); return false; }
    logm("ldr = " + hex(ldr));
    if (!ldr) return false;

    uintptr_t head = ldr + 0x10;
    uintptr_t cur = rd<uintptr_t>(head);
    logm("first entry = " + hex(cur));

    for (int i = 0; i < 1024 && cur && cur != head; ++i) {
        uintptr_t entry = cur - 0x10;
        std::wstring nm = entry_name(entry);
        if (dump && !nm.empty() && dump->size() < 1200) *dump += L"  " + nm + L"\n";
        if (want && !nm.empty() && !_wcsicmp(nm.c_str(), want)) {
            if (out_base) *out_base = rd<uintptr_t>(entry + 0x30);
            img_size = rd<size_t>(entry + 0x40);
            return true;
        }
        cur = rd<uintptr_t>(cur);
    }
    return false;
}

std::wstring list_modules() {
    std::wstring dump;
    walk_ldr(nullptr, nullptr, &dump);
    return dump.empty() ? L"  (nothing)\n" : dump;
}

static uintptr_t via_toolhelp(const wchar_t* mod) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) { logm("toolhelp: snapshot failed"); return 0; }
    MODULEENTRY32W me{ sizeof(me) };
    uintptr_t out = 0;
    for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me))
        if (!_wcsicmp(me.szModule, mod)) { img_size = me.modBaseSize; out = (uintptr_t)me.modBaseAddr; break; }
    CloseHandle(snap);
    logm(out ? "toolhelp: OK" : "toolhelp: not listed");
    return out;
}

static uintptr_t via_psapi(const wchar_t* mod) {
    HMODULE mods[2048]{};
    DWORD need = 0;
    if (!EnumProcessModulesEx(proc, mods, sizeof(mods), &need, LIST_MODULES_ALL)) {
        logm("psapi: EnumProcessModulesEx failed " + std::to_string(GetLastError()));
        return 0;
    }
    for (DWORD i = 0; i < need / sizeof(HMODULE); ++i) {
        wchar_t path[MAX_PATH]{};
        if (!GetModuleBaseNameW(proc, mods[i], path, MAX_PATH)) continue;
        if (!_wcsicmp(path, mod)) { logm("psapi: OK"); return (uintptr_t)mods[i]; }
    }
    logm("psapi: not listed (" + std::to_string(need / sizeof(HMODULE)) + " modules)");
    return 0;
}

static uintptr_t via_peb(const wchar_t* mod) {
    uintptr_t b = 0;
    bool ok = walk_ldr(mod, &b, nullptr);
    logm(ok ? "peb: OK" : "peb: not found");
    return b;
}

static uintptr_t via_mapped(const wchar_t* mod) {
    uintptr_t cur = 0x10000, found = 0;
    int imgs = 0, regions_seen = 0;
    MEMORY_BASIC_INFORMATION mbi{};
    while (cur < 0x7FFFFFFFFFFFull && VirtualQueryEx(proc, (LPCVOID)cur, &mbi, sizeof(mbi))) {
        ++regions_seen;
        if (mbi.Type == MEM_IMAGE) {
            ++imgs;
            wchar_t path[MAX_PATH]{};
            if (GetMappedFileNameW(proc, mbi.BaseAddress, path, MAX_PATH)) {
                std::wstring p = path;
                if (p.find(mod) != std::wstring::npos) {
                    uint16_t mz = rd<uint16_t>((uintptr_t)mbi.BaseAddress);
                    if (mz == 0x5A4D) { found = (uintptr_t)mbi.BaseAddress; img_size = mbi.RegionSize; break; }
                }
            }
        }
        cur = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }
    logm("mapped: " + std::string(found ? "OK" : "not found") +
         " (regions=" + std::to_string(regions_seen) + " images=" + std::to_string(imgs) + ")");
    return found;
}

uintptr_t module_base(const wchar_t* mod) {
    uintptr_t b = via_toolhelp(mod);
    if (!b) b = via_psapi(mod);
    if (!b) b = via_peb(mod);
    if (!b) b = via_mapped(mod);
    return b;
}

// ---- read probe ---------------------------------------------------------
static uintptr_t g_probe_addr = 0;

static bool find_probe_addr() {
    uintptr_t cur = 0x10000;
    MEMORY_BASIC_INFORMATION mbi{};
    while (VirtualQueryEx(proc, (LPCVOID)cur, &mbi, sizeof(mbi))) {
        if (mbi.Type == MEM_IMAGE) { g_probe_addr = (uintptr_t)mbi.BaseAddress; return true; }
        cur = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }
    return false;
}

static bool probe_at(uintptr_t addr, std::string* why) {
    uint8_t b[8]; SIZE_T got = 0;
    SetLastError(0);
    BOOL ok = ReadProcessMemory(proc, (LPCVOID)addr, b, 8, &got);
    if (why) *why = "ok=" + std::to_string(ok) + " got=" + std::to_string(got) +
                     " err=" + std::to_string(GetLastError());
    return ok && got == 8;
}

// ---- handle hijack ------------------------------------------------------
struct SHE_ENTRY {
    PVOID Object;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR HandleValue;
    ULONG GrantedAccess;
    USHORT BackTrace;
    USHORT TypeIndex;
    ULONG Attributes;
    ULONG Reserved;
};
struct SHE { ULONG_PTR Count; ULONG_PTR Reserved; SHE_ENTRY Entries[1]; };

static bool hijack(DWORD target) {
    using Fn = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
    static Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation");
    if (!fn) { logm("hijack: NtQuerySystemInformation missing"); return false; }

    // open our own handle FIRST so it exists in the snapshot we take next
    DWORD my_pid = GetCurrentProcessId();
    HANDLE self = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, my_pid);

    ULONG size = 8 << 20;
    std::vector<uint8_t> buf;
    LONG st = 0;
    for (int i = 0; i < 6; ++i) {
        buf.resize(size);
        st = fn(64, buf.data(), size, &size);
        if (st != (LONG)0xC0000004) break;
        size *= 2;
    }
    if (st != 0) { logm("hijack: query failed 0x" + hex(st)); if (self) CloseHandle(self); return false; }

    auto* sh = (SHE*)buf.data();
    logm("hijack: entries=" + std::to_string(sh->Count));

    USHORT proc_type = 0;
    if (self) {
        for (ULONG_PTR i = 0; i < sh->Count; ++i) {
            auto& e = sh->Entries[i];
            if ((DWORD)e.UniqueProcessId == my_pid && e.HandleValue == (ULONG_PTR)self) {
                proc_type = e.TypeIndex;
                break;
            }
        }
        CloseHandle(self);
    }
    logm("hijack: process type = " + std::to_string(proc_type));
    if (!proc_type) { logm("hijack: could not identify process type"); return false; }

    std::unordered_set<uint64_t> seen;
    int tried = 0, matches = 0, denied = 0;

    for (ULONG_PTR i = 0; i < sh->Count && tried < 6000; ++i) {
        auto& e = sh->Entries[i];
        if (e.TypeIndex != proc_type) continue;
        if ((DWORD)e.UniqueProcessId == my_pid) continue;

        uint64_t key = ((uint64_t)e.UniqueProcessId << 32) ^ (uint64_t)e.HandleValue;
        if (!seen.insert(key).second) continue;

        HANDLE owner = OpenProcess(PROCESS_DUP_HANDLE, FALSE, (DWORD)e.UniqueProcessId);
        if (!owner) continue;

        HANDLE dup = nullptr;
        if (DuplicateHandle(owner, (HANDLE)e.HandleValue, GetCurrentProcess(),
                            &dup, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            ++tried;
            DWORD opid = GetProcessId(dup);
            if (opid && opid != target) { CloseHandle(dup); CloseHandle(owner); continue; }

            ++matches;
            proc = dup;
            std::string why;
            bool ok = probe_at(g_probe_addr, &why);
            if (ok) {
                logm("hijack OK owner=" + std::to_string(e.UniqueProcessId) +
                     " access=" + hex(e.GrantedAccess));
                CloseHandle(owner);
                return true;
            }
            ++denied;
            if (denied <= 6)
                logm("hijack owner=" + std::to_string(e.UniqueProcessId) +
                     " access=" + hex(e.GrantedAccess) + ": " + why);
            CloseHandle(dup);
            proc = nullptr;
        }
        CloseHandle(owner);
    }

    logm("hijack: tried=" + std::to_string(tried) + " matches=" + std::to_string(matches) +
         " denied=" + std::to_string(denied));
    return false;
}

bool attach(const wchar_t* name) {
    last_error.clear();
    g_log.clear();
    pid = 0;

    for (int i = 0; i < 60000 && !pid; ++i) {
        pid = find_pid(name);
        if (!pid) Sleep(1);
    }
    if (!pid) { last_error = "process not found"; return false; }
    logm("pid " + std::to_string(pid));

    const DWORD masks[] = {
        PROCESS_ALL_ACCESS,
        PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
        PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION,
        PROCESS_VM_READ,
    };

    bool reads = false;
    for (DWORD m : masks) {
        HANDLE h = OpenProcess(m, FALSE, pid);
        if (!h) { logm("mask " + hex(m) + " open failed " + std::to_string(GetLastError())); continue; }

        proc = h;
        std::string why;
        if (!find_probe_addr()) {
            logm("mask " + hex(m) + ": no image region");
            CloseHandle(h);
            proc = nullptr;
            continue;
        }
        if (probe_at(g_probe_addr, &why)) { logm("mask " + hex(m) + " OK"); reads = true; break; }
        logm("mask " + hex(m) + " rpm: " + why);
        CloseHandle(h);
        proc = nullptr;
    }

    if (!reads) {
        logm("direct open stripped, trying hijack");
        if (g_probe_addr) reads = hijack(pid);
        else logm("hijack: no probe address");
    }
    if (!reads || !proc) { last_error = "ReadProcessMemory denied\n" + g_log; return false; }

    for (int i = 0; i < 40 && !base; ++i) {
        base = module_base(L"GameAssembly.dll");
        if (!base) Sleep(250);
    }
    if (!base) { last_error = "reads work, GameAssembly.dll not found\n" + g_log; return false; }

    logm("base = " + hex(base));
    last_error.clear();
    return true;
}

bool read(uintptr_t addr, void* out, size_t size) {
    SIZE_T got = 0;
    return ReadProcessMemory(proc, (LPCVOID)addr, out, size, &got) && got == size;
}

bool write(uintptr_t addr, const void* in, size_t size) {
    SIZE_T put = 0;
    return WriteProcessMemory(proc, (LPVOID)addr, in, size, &put) && put == size;
}

bool alive() {
    DWORD code = 0;
    return proc && GetExitCodeProcess(proc, &code) && code == STILL_ACTIVE;
}

std::string read_str(uintptr_t addr, size_t max) {
    if (!valid(addr)) return {};
    char buf[128]{};
    if (max > sizeof(buf) - 1) max = sizeof(buf) - 1;
    if (!read(addr, buf, max)) return {};
    buf[max] = 0;
    return std::string(buf);
}

bool valid(uintptr_t p) { return p >= 0x10000 && p < 0x7FFFFFFFFFFF && (p & 3) == 0; }

std::vector<std::pair<uintptr_t, size_t>> regions(uintptr_t lo, uintptr_t hi) {
    std::vector<std::pair<uintptr_t, size_t>> out;
    uintptr_t cur = lo;
    MEMORY_BASIC_INFORMATION mbi{};
    while (cur < hi && VirtualQueryEx(proc, (LPCVOID)cur, &mbi, sizeof(mbi))) {
        uintptr_t end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY)) &&
            !(mbi.Protect & PAGE_GUARD))
            out.emplace_back((uintptr_t)mbi.BaseAddress, mbi.RegionSize);
        cur = end;
    }
    return out;
}

} // namespace mem