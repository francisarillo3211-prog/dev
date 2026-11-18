#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "il2cpp.hpp"

namespace game {

    struct Vec2 { float x, y; };
    struct Vec3 { float x, y, z; };

    struct Player {
        uintptr_t entity = 0;
        uintptr_t view   = 0;
        Vec3 head{}, feet{};
        std::string name;
        float dist = 0.f;
        bool  local = false;
        bool  police = false;
    };

    struct Camera {
        Mat4 view{};
        Mat4 proj{};
        Vec3 pos{};
        float fov = 60.f;
        bool  valid = false;
    };

    bool  init_sdk();
    bool  in_match();
    bool  get_camera(Camera& cam);
    void  collect(std::vector<Player>& out, const Vec3& eye);
    bool  world_to_screen(const Camera& cam, const Vec3& world, int w, int h, Vec2& out);
}