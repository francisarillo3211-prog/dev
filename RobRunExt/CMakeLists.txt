cmake_minimum_required(VERSION 3.20)
project(RobRunExt CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(RobRunExt
    src/main.cpp src/memory.cpp src/il2cpp.cpp src/game.cpp src/overlay.cpp
    include/imgui/imgui.cpp include/imgui/imgui_draw.cpp
    include/imgui/imgui_tables.cpp include/imgui/imgui_widgets.cpp
    include/imgui/backends/imgui_impl_dx11.cpp include/imgui/backends/imgui_impl_win32.cpp)

target_include_directories(RobRunExt PRIVATE include include/imgui)
target_link_libraries(RobRunExt PRIVATE d3d11 dxgi dwmapi)
if (MSVC)
    target_compile_options(RobRunExt PRIVATE /utf-8 /EHsc /O2)
    target_link_options(RobRunExt PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:WinMainCRTStartup)
endif()