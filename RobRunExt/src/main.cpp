#include <windows.h>
#include <cstdio> 
#include <thread>
#include <mutex>
#include <vector>
#include <cmath>
#include "memory.hpp"
#include "il2cpp.hpp"
#include "game.hpp"
#include "overlay.hpp"
#include "config.hpp"
#include "imgui/imgui.h"

static std::mutex g_mtx;
static std::vector<game::Player> g_players;
static game::Camera g_cam;
static HWND g_game_hwnd = nullptr;
static bool g_ready = false;

static void draw_esp() {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    int cx = overlay::width / 2, cy = overlay::height / 2;

    if (cfg.aim && cfg.aim_draw_fov) {
        dl->AddCircle({ (float)cx, (float)cy }, cfg.aim_fov, IM_COL32(255, 255, 255, 60), 64, 1.5f);
    }

    std::lock_guard lk(g_mtx);
    if (!g_cam.valid) return;

    for (auto& p : g_players) {
        if (cfg.skip_local && p.local) continue;

        game::Vec2 top, bot;
        if (!game::world_to_screen(g_cam, p.head, overlay::width, overlay::height, top)) continue;
        if (!game::world_to_screen(g_cam, p.feet, overlay::width, overlay::height, bot)) continue;

        float h = bot.y - top.y;
        float w = h * 0.45f;
        if (h < 4) continue;

        ImU32 col = p.police ? IM_COL32(80, 160, 255, 230) : IM_COL32(255, 90, 90, 230);

        if (cfg.esp_box)
            dl->AddRect({ top.x - w / 2, top.y }, { top.x + w / 2, bot.y }, col, 0, 0, 1.6f);

        if (cfg.esp_snapline)
            dl->AddLine({ (float)cx, (float)(overlay::height - 1) }, { top.x, bot.y }, col, 1.f);

        if (cfg.esp_name)
            dl->AddText({ top.x - w / 2, top.y - 16 }, IM_COL32(255, 255, 255, 230), p.name.c_str());

        if (cfg.esp_dist) {
            char buf[32];
            sprintf_s(buf, "[%.0fm]", p.dist);
            dl->AddText({ top.x - w / 2, bot.y + 2 }, IM_COL32(200, 200, 200, 200), buf);
        }
    }
}

static void run_aimbot() {
    if (!cfg.aim || !(GetAsyncKeyState(cfg.aim_key) & 0x8000)) return;
    if (GetForegroundWindow() != g_game_hwnd) return;

    std::lock_guard lk(g_mtx);
    if (!g_cam.valid) return;

    int cx = overlay::width / 2, cy = overlay::height / 2;
    game::Player* best = nullptr;
    float best_d = cfg.aim_fov;

    for (auto& p : g_players) {
        if (p.local) continue;
        game::Vec3 aim = p.head;
        if (cfg.aim_bone == 1) aim.y -= 0.45f;

        game::Vec2 s;
        if (!game::world_to_screen(g_cam, aim, overlay::width, overlay::height, s)) continue;
        float d = std::sqrt((s.x - cx) * (s.x - cx) + (s.y - cy) * (s.y - cy));
        if (d < best_d) { best_d = d; best = &p; }
    }
    if (!best) return;

    game::Vec3 aim = best->head;
    if (cfg.aim_bone == 1) aim.y -= 0.45f;

    game::Vec2 s;
    if (!game::world_to_screen(g_cam, aim, overlay::width, overlay::height, s)) return;

    float dx = (s.x - cx) / cfg.aim_smooth;
    float dy = (s.y - cy) / cfg.aim_smooth;
    if (std::fabs(dx) < 1 && std::fabs(dy) < 1) return;

    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = (LONG)dx;
    in.mi.dy = (LONG)dy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(INPUT));
}

static void draw_menu() {
    ImGui::SetNextWindowSize({ 380, 460 }, ImGuiCond_FirstUseEver);
    ImGui::Begin("RobRunExt", nullptr, ImGuiWindowFlags_NoCollapse);

    ImGui::TextColored({ 0.4f, 0.9f, 0.4f, 1 }, g_ready ? "attached / sdk ok" : "waiting for match...");
    ImGui::Separator();

    ImGui::Checkbox("ESP", &cfg.esp);
    ImGui::Checkbox("Box", &cfg.esp_box);
    ImGui::Checkbox("Name", &cfg.esp_name);
    ImGui::Checkbox("Distance", &cfg.esp_dist);
    ImGui::Checkbox("Snapline", &cfg.esp_snapline);
    ImGui::Checkbox("Hide self", &cfg.skip_local);
    ImGui::SliderFloat("Max dist", &cfg.max_dist, 50, 800, "%.0f m");

    ImGui::Separator();
    ImGui::Checkbox("Aimbot", &cfg.aim);
    const char* bones[] = { "Head", "Chest" };
    ImGui::Combo("Bone", &cfg.aim_bone, bones, 2);
    ImGui::SliderFloat("FOV", &cfg.aim_fov, 10, 400, "%.0f px");
    ImGui::SliderFloat("Smooth", &cfg.aim_smooth, 1, 15, "%.1f");
    ImGui::Checkbox("Draw FOV", &cfg.aim_draw_fov);
    ImGui::Text("Aim key: right mouse (hold)");

    if (ImGui::Button("Save config")) save_cfg();
    ImGui::SameLine();
    if (ImGui::Button("Reload SDK")) { il2::init(); g_ready = game::init_sdk(); }

    ImGui::End();
}

static void worker() {
    while (overlay::running() && mem::alive()) {
        if (!g_ready) {
            if (!il2::init() || !game::init_sdk()) { Sleep(2000); continue; }
            g_ready = true;
        }
        if (!game::in_match()) { Sleep(500); continue; }

        game::Camera cam;
        std::vector<game::Player> list;
        if (game::get_camera(cam)) {
            game::collect(list, cam.pos);
            std::lock_guard lk(g_mtx);
            g_cam = cam;
            g_players.swap(list);
        }
        Sleep(8);
    }
}

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE, LPSTR, int) {
    load_cfg();

    for (;;) {
        if (mem::attach(L"ROB RUN.exe")) break;

        std::wstring msg = L"Could not attach.\n\nReason: ";
        msg += std::wstring(mem::last_error.begin(), mem::last_error.end());
        msg += L"\n\nRunning as admin: ";
        msg += mem::is_admin() ? L"yes" : L"NO";
        msg += L"\n\nProcesses matching \"rob\":\n";
        msg += mem::list_matching(L"rob");
        msg += L"\nMethod log:\n";
        msg += std::wstring(mem::last_error.begin(), mem::last_error.end());
        msg += L"\nModules seen:\n";
        msg += mem::list_modules();
        msg += mem::list_modules();

        if (MessageBoxW(nullptr, msg.c_str(), L"RobRunExt", MB_OKCANCEL) != IDOK) return 0;
    }

    g_game_hwnd = FindWindowW(nullptr, L"ROB RUN");
    // ... rest unchanged

    if (!overlay::create(hinst, 1920, 1080)) {
        MessageBoxW(nullptr, L"Overlay creation failed.", L"RobRunExt", MB_OK);
        return 1;
    }

    std::thread(worker).detach();

    bool menu = true, shown = true;
    while (overlay::running() && mem::alive()) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (GetAsyncKeyState(cfg.menu_key) & 1) { menu = !menu; overlay::set_click_through(!menu); }
        if (GetAsyncKeyState(cfg.hide_key) & 1) { shown = !shown; overlay::set_visible(shown); }
        if (GetAsyncKeyState(cfg.kill_key) & 1) break;

        overlay::begin_frame();

        if (cfg.esp) draw_esp();
        run_aimbot();
        if (menu && shown) draw_menu();

        overlay::end_frame();
    }

    overlay::destroy();
    return 0;
}