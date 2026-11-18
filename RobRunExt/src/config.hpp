#pragma once
#include <windows.h>
#include <string>
#include <fstream>

struct Settings {
    bool  esp         = true;
    bool  esp_box     = true;
    bool  esp_skeleton= false;
    bool  esp_name    = true;
    bool  esp_dist    = true;
    bool  esp_health  = true;
    bool  esp_snapline= false;
    bool  skip_local  = true;

    bool  aim         = true;
    int   aim_key     = VK_RBUTTON;
    int   aim_bone    = 0;      // 0 = head, 1 = chest
    float aim_fov     = 120.f;
    float aim_smooth  = 3.f;
    bool  aim_draw_fov= true;

    float max_dist    = 400.f;
    int   menu_key    = VK_F1;
    int   hide_key    = VK_INSERT;
    int   kill_key    = VK_END;

    // game offsets (verify after patches)
    uint32_t off_head_transform = 0xE8;
};

inline Settings cfg;
inline const char* CFG_PATH = "RobRunExt.ini";

inline void save_cfg() {
    std::ofstream f(CFG_PATH);
    f << cfg.esp << ' ' << cfg.esp_box << ' ' << cfg.esp_skeleton << ' ' << cfg.esp_name << ' '
      << cfg.esp_dist << ' ' << cfg.esp_health << ' ' << cfg.esp_snapline << '\n'
      << cfg.aim << ' ' << cfg.aim_key << ' ' << cfg.aim_bone << ' ' << cfg.aim_fov << ' '
      << cfg.aim_smooth << ' ' << cfg.aim_draw_fov << '\n'
      << cfg.max_dist << ' ' << std::hex << cfg.off_head_transform;
}

inline void load_cfg() {
    std::ifstream f(CFG_PATH);
    if (!f) return;
    f >> cfg.esp >> cfg.esp_box >> cfg.esp_skeleton >> cfg.esp_name
      >> cfg.esp_dist >> cfg.esp_health >> cfg.esp_snapline
      >> cfg.aim >> cfg.aim_key >> cfg.aim_bone >> cfg.aim_fov
      >> cfg.aim_smooth >> cfg.aim_draw_fov
      >> cfg.max_dist >> std::hex >> cfg.off_head_transform;
}