cmake_minimum_required(VERSION 3.5)
project(riv)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(OpenCV REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)

# ============================================================================
# FILTER TO PREVENT dv-processing CMAKE CRASH ON OLD COMPILERS
# ============================================================================
set(ENABLE_EVENTS_SUPPORT ON)

# If using GCC and version is below 13.0, disable to prevent fatal error inside its config
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13.0")
    message(STATUS "GCC version is below 13.0 (${CMAKE_CXX_COMPILER_VERSION}). Disabling dv-processing to prevent CMake crash.")
    set(ENABLE_EVENTS_SUPPORT OFF)
endif()

# If using Clang and version is below 18.0, disable to prevent fatal error inside its config
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "18.0")
    message(STATUS "Clang version is below 18.0 (${CMAKE_CXX_COMPILER_VERSION}). Disabling dv-processing to prevent CMake crash.")
    set(ENABLE_EVENTS_SUPPORT OFF)
endif()

# Only search for the package if the current compiler meets the requirements
if(ENABLE_EVENTS_SUPPORT)
    find_package(dv-processing QUIET)
endif()

# SOLUCIÓN: Eigen3 se busca ÚNICAMENTE si se encontró dv-processing
if(dv-processing_FOUND)
    message(STATUS "dv-processing found - Event camera support enabled")
    add_definitions(-DHAS_EVENTS)
    
    find_package(Eigen3 REQUIRED QUIET)
    if(Eigen3_FOUND)
        message(STATUS "Eigen3 found and linked for dv-processing internal dependencies")
    endif()
else()
    message(STATUS "Compiling WITHOUT event camera support (.aedat4)")
endif()

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

# Base Source files (always compiled)
set(SOURCES
    main.cpp
    3DViewer.cpp
    Utils.cpp
    Tonemapper.cpp
    MouseControls.cpp 
    KeyboardControls.cpp
    Colormap.cpp
    Colorspace.cpp
    Polarized.cpp
    Video.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# Append Events source code ONLY if dv-processing was successfully found
if(dv-processing_FOUND)
    list(APPEND SOURCES Events.cpp)
endif()

# ============================================================================
# CREACIÓN DEL EJECUTABLE
# ============================================================================
add_executable(riv ${SOURCES})

target_include_directories(riv PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Link base libraries (Estilo Plain)
target_link_libraries(riv
    ${OpenCV_LIBS}
    OpenGL::GL
    glfw
)

# Link optional dv-processing and dependencies ONLY if event support is active
if(dv-processing_FOUND)
    target_link_libraries(riv ${dv-processing_LIBRARIES})
    target_include_directories(riv PRIVATE ${dv-processing_INCLUDE_DIRS})
    
    # SOLUCIÓN COMPLETA AL LINKER: Forzar la inclusión de ZSTD, LZ4 y FMT requeridos por dv-processing
    target_link_libraries(riv "-lzstd")
    target_link_libraries(riv "-llz4")
    target_link_libraries(riv "-lfmt")
    
    # Esto inyecta dinámicamente '/usr/include/eigen3'
    if(TARGET Eigen3::Eigen)
        target_link_libraries(riv Eigen3::Eigen)
    else()
        target_include_directories(riv PRIVATE ${EIGEN3_INCLUDE_DIR})
    endif()
endif()

# Link OpenEXR if found (Estilo Plain)
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

# ============================================================================
# INSTALLATION AND OS ICONS (USING ONLY logo.png)
# ============================================================================
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    install(TARGETS riv DESTINATION bin)
    # install(FILES misc/riv.desktop DESTINATION share/applications)
    install(FILES assets/logo.png DESTINATION share/icons/hicolor/48x48/apps RENAME riv.png)
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
    add_custom_command(TARGET riv POST_BUILD 
        COMMAND ${CMAKE_COMMAND} -E create_symlink riv.app/Contents/MacOS/riv ${CMAKE_BINARY_DIR}/bin/riv
    )
endif()