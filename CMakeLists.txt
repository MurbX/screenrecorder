cmake_minimum_required(VERSION 3.21)

# VERSION is the single source of truth for the app, the exe metadata and the installer.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SCREENREC_VERSION LIMIT_COUNT 1)
project(ScreenRec VERSION ${SCREENREC_VERSION} LANGUAGES CXX RC)

# GitHub "owner/name" used for update checks (Releases API). Empty = no update checks.
set(SCREENREC_GITHUB_REPO "MurbX/screenrecorder" CACHE STRING "GitHub repository (owner/name) that publishes releases")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: the exe needs no Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

configure_file(src/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h @ONLY)

file(GLOB SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
add_executable(ScreenRec WIN32 ${SOURCES} res/ScreenRec.rc)

target_include_directories(ScreenRec PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated src)
target_compile_definitions(ScreenRec PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(ScreenRec PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8 /W3 /permissive- /MP>)
target_link_options(ScreenRec PRIVATE /MANIFEST:NO)
target_link_libraries(ScreenRec PRIVATE
    d2d1 dwrite d3d11 dxgi dcomp windowscodecs dwmapi uxtheme shcore
    comctl32 ole32 oleaut32 shell32 shlwapi advapi32 winmm winhttp
    mfplat mfreadwrite mf mfuuid mfplay)

# Development convenience: copy ffmpeg.exe next to the exe when it is on this machine.
# (Release packages get a pinned FFmpeg build from packaging/package.ps1.)
find_program(FFMPEG_EXE ffmpeg)
if(FFMPEG_EXE)
    get_filename_component(FFMPEG_DIR ${FFMPEG_EXE} DIRECTORY)
    add_custom_command(TARGET ScreenRec POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_DIR}/ffmpeg.exe $<TARGET_FILE_DIR:ScreenRec>)
endif()
