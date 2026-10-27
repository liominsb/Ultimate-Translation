cmake_minimum_required(VERSION 3.20)
project(TranslateBall CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -municode 让入口点走 wWinMain（Windows 下统一用宽字符）
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -municode -O2")

add_executable(TranslateBall WIN32
    src/main.cpp
    src/config.cpp
    src/capture.cpp
    src/popup.cpp
    src/langdetect.cpp
    src/engine.cpp
    src/logger.cpp
    src/trayicon.cpp
)

# 统一在命令行定义，避免每个源文件各写一遍；
# 0x0A00 是 Win10，GetDpiForWindow / SetProcessDpiAwarenessContext 都需要它
target_compile_definitions(TranslateBall PRIVATE
    UNICODE _UNICODE
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=0x0A000000
)

# 静态链接 gcc/stdc++，产物不依赖任何 MinGW 运行库 dll
target_link_options(TranslateBall PRIVATE
    -static -static-libgcc -static-libstdc++
)
# 只用系统自带库，从头到尾不引第三方依赖
target_link_libraries(TranslateBall PRIVATE
    gdi32 user32 shell32 dwmapi winhttp
)
