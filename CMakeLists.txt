cmake_minimum_required(VERSION 3.5)
project(riv)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(OpenCV REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)

# Optional: OpenEXR for HDR image support
find_package(OpenEXR QUIET)

if(OpenEXR_FOUND)
    message(STATUS "OpenEXR found - HDR image support enabled")
    add_definitions(-DHAS_OPENEXR)
else()
    message(STATUS "OpenEXR not found - HDR support limited to 8-bit images")
    message(STATUS "Install with: sudo apt install libopenexr-dev")
endif()

include_directories(${OpenCV_INCLUDE_DIRS})

# ImGui
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)

# Source files (AQUÍ AGREGAMOS LOS NUEVOS ARCHIVOS)
set(SOURCES
    main.cpp
    Utils.cpp
    Tonemapper.cpp
    # Controls.cpp
    MouseControls.cpp 
    KeyboardControls.cpp
    Colormap.cpp
    Colorspace.cpp
    Polarized.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

add_executable(riv ${SOURCES})

target_include_directories(riv PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Link libraries
target_link_libraries(riv
    ${OpenCV_LIBS}
    OpenGL::GL
    glfw
)

# Link OpenEXR if found
if(OpenEXR_FOUND)
    target_link_libraries(riv ${OPENEXR_LIBRARIES})
    target_include_directories(riv PRIVATE ${OPENEXR_INCLUDE_DIRS})
endif()

# Set output directory
set_target_properties(riv PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Compiler optimizations for release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(riv PRIVATE -O3 -march=native)
    endif()
endif()

# HDR SUPPORT
# sudo apt install libopenexr-dev

# IMGUI
# sudo apt install libglfw3-dev libopencv-dev

install(TARGETS riv DESTINATION bin)

# ============================================================================
# INSTALLATION AND OS ICONS (USING ONLY logo.png)
# ============================================================================
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    install(TARGETS riv DESTINATION bin)
    install(FILES misc/vpv.desktop DESTINATION share/applications)
    install(FILES assets/logo.png DESTINATION share/icons/hicolor/48x48/apps RENAME vpv.png)
    install(FILES assets/logo.png DESTINATION share/riv)
elseif(APPLE)
    set(ICON_DIR ${CMAKE_CURRENT_BINARY_DIR}/riv.iconset)
    set(MACOS_ICON ${CMAKE_CURRENT_BINARY_DIR}/icon.icns)
    add_custom_command(OUTPUT ${MACOS_ICON}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ICON_DIR}
        COMMAND sips -z 8 8     ${CMAKE_SOURCE_DIR}/assets/logo.png --out ${ICON_DIR}/icon_16x16.png
        COMMAND sips -z 16 16   ${CMAKE_SOURCE_DIR}/assets/logo.png --out ${ICON_DIR}/icon_16x16@2x.png
        COMMAND sips -z 16 16   ${CMAKE_SOURCE_DIR}/assets/logo.png --out ${ICON_DIR}/icon_32x32.png
        COMMAND sips -z 32 32   ${CMAKE_SOURCE_DIR}/assets/logo.png --out ${ICON_DIR}/icon_32x32@2x.png
        COMMAND sips -z 48 48   ${CMAKE_SOURCE_DIR}/assets/logo.png --out ${ICON_DIR}/icon_128x128.png
        COMMAND sips -z 64 64   ${CMAKE_SOURCE_DIR}/assets/logo.png --out ${ICON_DIR}/icon_128x128@2x.png
        COMMAND iconutil -c icns ${ICON_DIR} -o ${MACOS_ICON}
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${ICON_DIR}
        DEPENDS ${CMAKE_SOURCE_DIR}/assets/logo.png
    )
    set_source_files_properties(${MACOS_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    set_target_properties(riv PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_ICON_FILE "icon.icns"
        MACOSX_BUNDLE_BUNDLE_NAME "Research Image Viewer"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.arielizs.riv"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    )
    target_sources(riv PRIVATE ${MACOS_ICON})
    install(TARGETS riv BUNDLE DESTINATION .)
    add_custom_command(TARGET riv POST_BUILD 
        COMMAND touch ${CMAKE_BINARY_DIR}/bin/riv.app
        COMMAND /usr/bin/xattr -c ${CMAKE_BINARY_DIR}/bin/riv.app 2>/dev/null || true
    )
endif()

add_custom_command(TARGET riv POST_BUILD COMMAND ${CMAKE_COMMAND} -E create_symlink riv.app/Contents/MacOS/riv ${CMAKE_BINARY_DIR}/bin/riv)