#pragma once
#include <windows.h>
#include <d3d11.h>

namespace overlay {
    bool  create(HINSTANCE hinst, int w, int h);
    void  begin_frame();
    void  end_frame();
    void  destroy();
    HWND  hwnd();
    bool  running();
    void  set_visible(bool v);
    void  set_click_through(bool v);
    extern int width, height;
}