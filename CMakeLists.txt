cmake_minimum_required(VERSION 3.5)
project(HDRVisualizer)

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
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

add_executable(HDRVisualizer ${SOURCES})

target_include_directories(HDRVisualizer PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Link libraries
target_link_libraries(HDRVisualizer
    ${OpenCV_LIBS}
    OpenGL::GL
    glfw
)

# Link OpenEXR if found
if(OpenEXR_FOUND)
    target_link_libraries(HDRVisualizer ${OPENEXR_LIBRARIES})
    target_include_directories(HDRVisualizer PRIVATE ${OPENEXR_INCLUDE_DIRS})
endif()

# Set output directory
set_target_properties(HDRVisualizer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Compiler optimizations for release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(HDRVisualizer PRIVATE -O3 -march=native)
    endif()
endif()

# HDR SUPPORT
# sudo apt install libopenexr-dev

# IMGUI
# sudo apt install libglfw3-dev libopencv-dev