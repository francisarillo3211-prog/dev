#include "il2cpp.hpp"
#include "memory.hpp"
#include <unordered_set>
#include <cmath>

namespace il2 {

// ---- metadata layout (Unity 6 / metadata v31) -----------------------------
// GlobalMetadataHeader, int32 slots
enum { H_SANITY = 0, H_STR = 6, H_FIELDS = 24, H_TYPEDEFS = 40 };
static constexpr uint32_t SANITY = 0xFAB11BAF;

// MetadataRegistration
enum { MR_TYPES = 56, MR_FIELD_OFFSETS = 88 };

// TypeDefinition
static constexpr size_t TD_SIZE = 84;
enum { TD_NAME = 0, TD_NS = 4, TD_BYVAL = 8, TD_PARENT = 20, TD_FIELD_START = 36, TD_FIELD_COUNT = 72 };
// FieldDefinition
static constexpr size_t FD_SIZE = 12;

static uintptr_t g_header = 0;     // Il2CppGlobalMetadataHeader*
static uintptr_t g_mr     = 0;     // Il2CppMetadataRegistration*
static uintptr_t g_strings = 0;
static uintptr_t g_typedefs = 0;
static uint32_t  g_td_count = 0;
static uintptr_t g_fields = 0;
static uintptr_t g_field_offsets = 0;

static std::unordered_map<std::string, uintptr_t> s_klass;
static std::unordered_map<std::string, uint32_t>  s_field;

static std::string meta_str(uint32_t idx) { return mem::read_str(g_strings + idx); }

// find a qword inside the module that points at the metadata magic
static uintptr_t find_header() {
    auto rgs = mem::regions(mem::base, mem::base + mem::img_size);
    for (auto& [addr, size] : rgs) {
        std::vector<uint8_t> buf(size);
        if (!mem::read(addr, buf.data(), size)) continue;
        for (size_t i = 0; i + 8 <= size; i += 8) {
            uintptr_t cand = *(uintptr_t*)(buf.data() + i);
            if (!mem::valid(cand)) continue;
            uint32_t magic = mem::rd<uint32_t>(cand);
            if (magic == SANITY) return cand;
        }
    }
    return 0;
}

// find MetadataRegistration: a qword whose +88 points into a valid region and
// whose +56 array entry resolves to a readable pointer
static uintptr_t find_mr() {
    auto rgs = mem::regions(mem::base, mem::base + mem::img_size);
    for (auto& [addr, size] : rgs) {
        std::vector<uint8_t> buf(size);
        if (!mem::read(addr, buf.data(), size)) continue;
        for (size_t i = 0; i + 8 <= size; i += 8) {
            uintptr_t cand = *(uintptr_t*)(buf.data() + i);
            if (!mem::valid(cand)) continue;
            uintptr_t fo = mem::rd<uintptr_t>(cand + MR_FIELD_OFFSETS);
            uintptr_t ty = mem::rd<uintptr_t>(cand + MR_TYPES);
            if (!mem::valid(fo) || !mem::valid(ty)) continue;
            uintptr_t first_type = mem::rd<uintptr_t>(ty);
            if (!mem::valid(first_type)) continue;
            return cand;
        }
    }
    return 0;
}

bool init() {
    g_header = find_header();
    g_mr     = find_mr();
    if (!g_header || !g_mr) return false;

    int32_t str_off  = mem::rd<int32_t>(g_header + H_STR * 4);
    int32_t fld_off  = mem::rd<int32_t>(g_header + H_FIELDS * 4);
    int32_t td_off   = mem::rd<int32_t>(g_header + H_TYPEDEFS * 4);
    int32_t td_size  = mem::rd<int32_t>(g_header + H_TYPEDEFS * 4 + 4);
    if (str_off <= 0 || fld_off <= 0 || td_off <= 0 || td_size <= 0) return false;

    g_strings       = g_header + str_off;
    g_fields        = g_header + fld_off;
    g_typedefs      = g_header + td_off;
    g_td_count      = (uint32_t)(td_size / TD_SIZE);
    g_field_offsets = mem::rd<uintptr_t>(g_mr + MR_FIELD_OFFSETS);

    return g_td_count > 1000 && g_td_count < 400000;
}

uintptr_t klass_of(const char* ns, const char* name) {
    for (uint32_t i = 0; i < g_td_count; ++i) {
        uintptr_t td = g_typedefs + i * TD_SIZE;
        uint32_t name_idx = mem::rd<uint32_t>(td + TD_NAME);
        uint32_t ns_idx   = mem::rd<uint32_t>(td + TD_NS);
        if (meta_str(ns_idx) != ns) continue;
        if (meta_str(name_idx) != name) continue;

        int32_t byval = mem::rd<int32_t>(td + TD_BYVAL);
        if (byval < 0) return 0;
        uintptr_t type_ptr = mem::rd<uintptr_t>(mem::rd<uintptr_t>(g_mr + MR_TYPES) + (uintptr_t)byval * 8);
        if (!mem::valid(type_ptr)) return 0;
        uintptr_t klass = mem::rd<uintptr_t>(type_ptr);   // Il2CppType->data
        return mem::valid(klass) ? klass : 0;
    }
    return 0;
}

uint32_t field_off(uintptr_t klass, const char* field) {
    for (int depth = 0; depth < 8 && klass; ++depth) {
        // locate the type definition matching this class by name/namespace
        std::string cname = mem::read_str(mem::rd<uintptr_t>(klass + 0x10));
        std::string cns   = mem::read_str(mem::rd<uintptr_t>(klass + 0x18));
        for (uint32_t i = 0; i < g_td_count; ++i) {
            uintptr_t td = g_typedefs + i * TD_SIZE;
            if (meta_str(mem::rd<uint32_t>(td + TD_NAME)) != cname) continue;
            if (meta_str(mem::rd<uint32_t>(td + TD_NS))   != cns)   continue;

            uint32_t start = mem::rd<uint32_t>(td + TD_FIELD_START);
            uint16_t count = mem::rd<uint16_t>(td + TD_FIELD_COUNT);
            for (uint16_t f = 0; f < count; ++f) {
                uintptr_t fd = g_fields + (size_t)(start + f) * FD_SIZE;
                if (meta_str(mem::rd<uint32_t>(fd)) == field) {
                    uintptr_t arr = mem::rd<uintptr_t>(g_field_offsets + (uintptr_t)i * 8);
                    if (!mem::valid(arr)) return 0;
                    return mem::rd<uint32_t>(arr + (uintptr_t)f * 4);
                }
            }
            klass = mem::rd<uintptr_t>(mem::rd<uintptr_t>(g_mr + MR_TYPES) +
                                       (uintptr_t)mem::rd<int32_t>(td + TD_PARENT) * 8);
            klass = klass ? mem::rd<uintptr_t>(klass) : 0;
            break;
        }
        if (!klass) break;
        klass = mem::rd<uintptr_t>(klass); // not used further; loop re-reads
        break;
    }
    return 0;
}

uintptr_t R_klass(const char* key, const char* ns, const char* name) {
    auto it = s_klass.find(key);
    if (it != s_klass.end()) return it->second;
    uintptr_t k = klass_of(ns, name);
    s_klass[key] = k;
    return k;
}

uint32_t R_field(const char* key, const char* ns, const char* name, const char* field) {
    std::string k = std::string(key) + "::" + field;
    auto it = s_field.find(k);
    if (it != s_field.end()) return it->second;
    uintptr_t klass = R_klass(key, ns, name);
    uint32_t off = klass ? field_off(klass, field) : 0;
    s_field[k] = off;
    return off;
}

bool is_obj(uintptr_t obj, uintptr_t klass) {
    return mem::valid(obj) && klass && mem::rd<uintptr_t>(obj) == klass;
}

std::string klass_name(uintptr_t obj) {
    uintptr_t k = mem::rd<uintptr_t>(obj);
    if (!mem::valid(k)) return {};
    return mem::read_str(mem::rd<uintptr_t>(k + 0x10));
}

std::vector<uintptr_t> scan_objects(uintptr_t klass, size_t max) {
    std::vector<uintptr_t> out;
    std::unordered_set<uintptr_t> seen;
    const size_t CHUNK = 4 << 20;

    for (auto& [addr, size] : mem::regions()) {
        for (size_t off = 0; off < size && out.size() < max; off += CHUNK) {
            size_t len = min(CHUNK, size - off);
            std::vector<uint8_t> buf(len);
            if (!mem::read(addr + off, buf.data(), len)) continue;
            for (size_t i = 0; i + 8 <= len; i += 8) {
                uintptr_t v = *(uintptr_t*)(buf.data() + i);
                if (!mem::valid(v) || seen.count(v)) continue;
                if (mem::rd<uintptr_t>(v) != klass) continue;
                seen.insert(v);
                out.push_back(v);
                if (out.size() >= max) return out;
            }
        }
    }
    return out;
}

// TransformData: localPosition(0) localRotation(12) localScale(28) pad(40) = 48
static bool read_trs(uintptr_t hierarchy, int idx, Mat4& out) {
    float d[12];
    if (!mem::read(mem::rd<uintptr_t>(hierarchy + 0x18) + (uintptr_t)idx * 48, d, sizeof(d))) return false;
    if (!std::isfinite(d[0]) || !std::isfinite(d[1]) || !std::isfinite(d[2])) return false;

    float x = d[3], y = d[4], z = d[5], w = d[6];
    float sx = d[7], sy = d[8], sz = d[9];

    out.m[0]  = (1 - 2 * (y * y + z * z)) * sx; out.m[1]  = (2 * (x * y - z * w)) * sx;
    out.m[2]  = (2 * (x * z + y * w)) * sx;     out.m[3]  = 0;
    out.m[4]  = (2 * (x * y + z * w)) * sy;     out.m[5]  = (1 - 2 * (x * x + z * z)) * sy;
    out.m[6]  = (2 * (y * z - x * w)) * sy;     out.m[7]  = 0;
    out.m[8]  = (2 * (x * z - y * w)) * sz;     out.m[9]  = (2 * (y * z + x * w)) * sz;
    out.m[10] = (1 - 2 * (x * x + y * y)) * sz; out.m[11] = 0;
    out.m[12] = d[0]; out.m[13] = d[1]; out.m[14] = d[2]; out.m[15] = 1;
    return true;
}

static void mul(const Mat4& a, const Mat4& b, Mat4& o) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + r] * b.m[c * 4 + k];
            o.m[c * 4 + r] = s;
        }
}

bool get_matrix(uintptr_t transform, Mat4& out) {
    uintptr_t native = mem::rd<uintptr_t>(transform + 0x10);
    if (!mem::valid(native)) return false;
    uintptr_t hier = mem::rd<uintptr_t>(native + 0x28);
    if (!mem::valid(hier)) return false;

    int idx = mem::rd<int>(native + 0x30);
    int count = mem::rd<int>(hier + 0x38);
    if (idx < 0 || count < 1 || idx >= count || count > 2048) return false;

    Mat4 local{};
    if (!read_trs(hier, idx, local)) return false;
    out = local;

    for (int guard = 0; guard < 32; ++guard) {
        int parent = mem::rd<int>(mem::rd<uintptr_t>(hier + 0x20) + (uintptr_t)idx * 4);
        if (parent < 0 || parent >= count || parent == idx) break;
        Mat4 pm{};
        if (!read_trs(hier, parent, pm)) break;
        Mat4 tmp{};
        mul(pm, out, tmp);
        out = tmp;
        idx = parent;
    }
    return true;
}

bool get_position(uintptr_t transform, float* xyz) {
    Mat4 m{};
    if (!get_matrix(transform, m)) return false;
    xyz[0] = m.m[12]; xyz[1] = m.m[13]; xyz[2] = m.m[14];
    return std::isfinite(xyz[0]) && std::isfinite(xyz[1]) && std::isfinite(xyz[2]);
}

} // namespace il2