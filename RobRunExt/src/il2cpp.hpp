#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

struct Mat4 { float m[16]; };

namespace il2 {

    bool init();

    // Resolve Il2CppClass* by namespace + name (walks global-metadata).
    uintptr_t klass_of(const char* ns, const char* name);

    // Field offset by name; walks the parent chain. Returns 0 when missing.
    uint32_t field_off(uintptr_t klass, const char* field);

    // Cached resolution so we don't re-walk metadata every tick.
    uintptr_t R_klass(const char* key, const char* ns, const char* name);
    uint32_t  R_field(const char* key, const char* ns, const char* name, const char* field);

    bool        is_obj(uintptr_t obj, uintptr_t klass);
    std::string klass_name(uintptr_t obj);
    std::vector<uintptr_t> scan_objects(uintptr_t klass, size_t max = 512);

    // Transform (managed Transform -> native TransformHierarchy -> TransformData)
    bool get_matrix(uintptr_t transform, Mat4& out);
    bool get_position(uintptr_t transform, float* xyz);
}