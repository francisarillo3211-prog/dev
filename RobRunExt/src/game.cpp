#include "game.hpp"
#include "config.hpp"
#include "memory.hpp"
#include <cmath>
#include <unordered_map>

namespace game {

static uintptr_t K_Player, K_Char, K_View;
static uint32_t  F_BattleChar, F_Views, F_Name, F_IsLocal, F_IsPolice;
static uintptr_t K_Camera;
static uint32_t  F_Cached;

bool init_sdk() {
    K_Player = il2::R_klass("PEM", "Game.Battle", "PlayerEntityModel");
    K_Char   = il2::R_klass("BC",  "Game.Battle.EntityModel", "BattleCharacter");
    K_View   = il2::R_klass("BCV", "Game.Battle.EntityModel.BattleCharacter", "BattleCharacterEntityView");
    K_Camera = il2::R_klass("CAM", "UnityEngine", "Camera");

    if (!K_Player || !K_Char || !K_View) return false;

    F_BattleChar = il2::R_field("PEM", "Game.Battle", "PlayerEntityModel", "BattleCharacter");
    if (!F_BattleChar) F_BattleChar = il2::R_field("PEM", "Game.Battle.EntityModel", "EntityModel", "BattleCharacter");

    F_Views = il2::R_field("BC", "Game.Battle.EntityModel", "BattleCharacter", "Views");
    if (!F_Views) F_Views = il2::R_field("BC", "Game.Battle.EntityModel", "BattleCharacter", "_views");

    for (const char* n : { "Name", "Nickname", "PlayerName", "DisplayName" }) {
        F_Name = il2::R_field("PEM", "Game.Battle", "PlayerEntityModel", n);
        if (F_Name) break;
    }
    for (const char* n : { "IsLocal", "IsLocalPlayer", "IsSelf" }) {
        F_IsLocal = il2::R_field("PEM", "Game.Battle", "PlayerEntityModel", n);
        if (F_IsLocal) break;
    }
    for (const char* n : { "IsPolice", "IsCop", "Police" }) {
        F_IsPolice = il2::R_field("PEM", "Game.Battle", "PlayerEntityModel", n);
        if (F_IsPolice) break;
    }
    F_Cached = il2::R_field("CAM", "UnityEngine", "Camera", "m_CachedPtr");

    return true;
}

bool in_match() {
    static std::vector<uintptr_t> cache;
    static uint32_t stamp = 0;
    if (GetTickCount() - stamp > 1500) {
        cache = il2::scan_objects(K_Player, 4);
        stamp = GetTickCount();
    }
    return !cache.empty() && il2::is_obj(cache[0], K_Player);
}

bool get_camera(Camera& cam) {
    auto cams = il2::scan_objects(K_Camera, 32);
    for (uintptr_t c : cams) {
        uintptr_t native = mem::rd<uintptr_t>(c + F_Cached);
        if (!mem::valid(native)) continue;

        // managed Camera -> native matrices
        if (mem::read(native + 0x2A0, cam.view.m, sizeof(cam.view.m)) &&
            mem::read(native + 0x2E0, cam.proj.m, sizeof(cam.proj.m)) &&
            std::isfinite(cam.proj.m[0]) && cam.proj.m[0] != 0 &&
            std::isfinite(cam.view.m[12])) {
            cam.pos = { cam.view.m[12], cam.view.m[13], cam.view.m[14] };
            cam.valid = true;
            return true;
        }
    }
    return false;
}

void collect(std::vector<Player>& out, const Vec3& eye) {
    static std::vector<uintptr_t> cache;
    static uint32_t stamp = 0;
    if (GetTickCount() - stamp > 600) {
        cache = il2::scan_objects(K_Player, 256);
        stamp = GetTickCount();
    }

    out.clear();
    for (uintptr_t e : cache) {
        if (!il2::is_obj(e, K_Player)) continue;

        uintptr_t ch = mem::rd<uintptr_t>(e + F_BattleChar);
        if (!mem::valid(ch)) continue;
        uintptr_t vw = mem::rd<uintptr_t>(ch + F_Views);
        if (!mem::valid(vw)) continue;
        uintptr_t tf = mem::rd<uintptr_t>(vw + cfg.off_head_transform);
        if (!mem::valid(tf)) continue;

        float h[3];
        if (!il2::get_position(tf, h)) continue;

        Player p;
        p.entity = e;
        p.view = vw;
        p.head = { h[0], h[1], h[2] };
        p.feet = { h[0], h[1] - 1.75f, h[2] };
        p.dist = std::sqrt((h[0]-eye.x)*(h[0]-eye.x) + (h[1]-eye.y)*(h[1]-eye.y) + (h[2]-eye.z)*(h[2]-eye.z));
        if (p.dist > cfg.max_dist) continue;

        if (F_Name) {
            uintptr_t sp = mem::rd<uintptr_t>(e + F_Name);
            if (mem::valid(sp)) p.name = mem::read_str(sp, 32);
        }
        if (p.name.empty()) p.name = "PLAYER";

        if (F_IsLocal) p.local = mem::rd<uint8_t>(e + F_IsLocal) != 0;
        if (F_IsPolice) p.police = mem::rd<uint8_t>(e + F_IsPolice) != 0;

        out.push_back(p);
    }
}

bool world_to_screen(const Camera& cam, const Vec3& w, int width, int height, Vec2& out) {
    float v[4] = { w.x, w.y, w.z, 1.f };
    float view[4], clip[4];
    for (int r = 0; r < 4; ++r)
        view[r] = cam.view.m[0*4+r]*v[0] + cam.view.m[1*4+r]*v[1] + cam.view.m[2*4+r]*v[2] + cam.view.m[3*4+r]*v[3];
    for (int r = 0; r < 4; ++r)
        clip[r] = cam.proj.m[0*4+r]*view[0] + cam.proj.m[1*4+r]*view[1] + cam.proj.m[2*4+r]*view[2] + cam.proj.m[3*4+r]*view[3];

    if (clip[3] < 0.01f) return false;

    float nx = clip[0] / clip[3], ny = clip[1] / clip[3];
    out.x = (nx * 0.5f + 0.5f) * width;
    out.y = (1.f - (ny * 0.5f + 0.5f)) * height;
    return true;
}

} // namespace game