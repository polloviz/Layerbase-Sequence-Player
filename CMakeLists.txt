cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
endif()

project(SequencePlayer VERSION 1.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: single self-contained exe, no DLL loading at startup.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

find_package(OpenEXR CONFIG REQUIRED)
find_package(OpenColorIO CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(TIFF REQUIRED)
find_package(GLEW REQUIRED)
find_path(STB_INCLUDE_DIRS "stb_image.h" REQUIRED)

configure_file(src/Version.h.in ${CMAKE_BINARY_DIR}/generated/Version.h @ONLY)

add_executable(SequencePlayer WIN32
    src/main.cpp
    src/App.cpp
    src/App.h
    src/AboutUI.cpp
    src/BatchUI.cpp
    src/UI.cpp
    src/ColorAgx.cpp
    src/CryptoUI.cpp
    src/ColorManager.cpp
    src/ColorManager.h
    src/Export.cpp
    src/Export.h
    src/ExportUI.cpp
    src/ExrLayers.cpp
    src/ExrLayers.h
    src/FrameCache.cpp
    src/FrameCache.h
    src/GLViewer.cpp
    src/GLViewer.h
    src/I18n.cpp
    src/I18n.h
    src/Image.h
    src/ImageIO.cpp
    src/ImageIO.h
    src/Platform.cpp
    src/Platform.h
    src/Sequence.cpp
    src/Sequence.h
    src/Settings.cpp
    src/Settings.h
    src/UpdateCheck.cpp
    src/UpdateCheck.h
    src/UpdateUI.cpp
    res/app.rc
    res/app.manifest
)

target_include_directories(SequencePlayer PRIVATE src ${CMAKE_BINARY_DIR}/generated ${STB_INCLUDE_DIRS})
target_compile_definitions(SequencePlayer PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(SequencePlayer PRIVATE /utf-8 /W3 /MP $<$<CONFIG:Release>:/O2 /GL>)
target_link_options(SequencePlayer PRIVATE $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>)
target_link_libraries(SequencePlayer PRIVATE
    OpenEXR::OpenEXR
    OpenColorIO::OpenColorIO
    imgui::imgui
    TIFF::TIFF
    GLEW::GLEW
    opengl32 dwmapi shlwapi shell32 ole32 uuid comctl32
)
