// ============================================================================
// 1. LIBRARIES AND DEPENDENCIES
// ============================================================================
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <iostream>
#include <filesystem>
#include <algorithm>

// Project modules
#include "Utils.h"
#include "Tonemapper.h"
#include "MouseControls.h"
#include "KeyboardControls.h"
#include "Colormap.h"
#include "Colorspace.h"

// ============================================================================
// 2. MAIN ENTRY POINT
// ============================================================================
int main(int argc, char** argv)
{
    // ------------------------------------------------------------------------
    // 2.1. Argument Validation and Image File Parsing
    // ------------------------------------------------------------------------
    if (!glfwInit()) return -1;

    if (argc < 2)
    {
        std::cout << "Usage: ./bin/HDRVisualizer <image_or_folder>\n";
        return -1;
    }

    std::string input = argv[1];
    std::vector<std::string> images;
    int idx = 0;

    // Detect if input path is a folder directory or a standalone image file
    if (std::filesystem::is_directory(input))
        images = getImages(input);
    else
        images.push_back(input);

    if (images.empty()) return -1;

    // ------------------------------------------------------------------------
    // 2.2. Graphics Context Setup (GLFW & OpenGL Core Profile)
    // ------------------------------------------------------------------------
    #ifdef __APPLE__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    #endif

    GLFWwindow* window = glfwCreateWindow(1280, 720, "HDR Viewer", NULL, NULL);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable V-Sync

    // ------------------------------------------------------------------------
    // 2.3. Graphical User Interface Context Initialization (Dear ImGui)
    // ------------------------------------------------------------------------
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    // Hide native OS cursor to draw the Custom Precision Crosshair instead
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    // ============================================================================
    // 3. APPLICATION STATE VARIABLES (OpenCV Matrices & Framework Objects)
    // ============================================================================
    cv::Mat base_img_raw;    // Full-resolution original float HDR image matrix
    cv::Mat base_img_ldr;    // Full-resolution mapped and processed 8-bit image matrix
    cv::Mat current_img_ldr; // Active cropped region (Zoom window) in 8-bit format
    cv::Mat current_img_raw; // Active cropped region (Zoom window) in raw float format
    GLuint texture = 0;      // OpenGL hardware texture identifier handle

    // Core architectural instances
    ZoomState zoom_state;
    Colormap colormap;
    Colorspace colorspace;
    int mode = 1; // 1: Reinhard, 2: Drago, 3: Mantiuk
    
    // Factory default parameters setup for Tonemapping operator sliders
    float r_gamma = 1.0f, intensity = 0.0f, light_adapt = 0.5f, color_adapt = 0.0f; // Reinhard
    float d_gamma = 1.0f, d_saturation = 1.0f, d_bias = 0.85f;                     // Drago
    float m_gamma = 1.0f, m_scale = 0.7f, m_saturation = 1.0f;                     // Mantiuk
    
    // Performance flag modifiers to optimize backend pipelines
    bool needs_tonemap = false; // Triggers full re-calculation if HDR global settings shift
    bool needs_texture = false; // Triggers rapid GPU texture buffer updates during Zoom/Pan
        
    // ============================================================================
    // 4. CONTROL UTILITIES (Data Pipeline Framework)
    // ============================================================================
    
    // 4.1: Compute dynamic mapping or localized AutoRange operations
    auto doTonemap = [&]()
    {
        if (base_img_raw.empty()) return;

        // Apply localized normalization scale if a custom Colormap range bounds are active
        if (colormap.is_active) {
            cv::Mat normalized = colormap.apply(base_img_raw);
            cv::Mat final_8bit;
            normalized.convertTo(final_8bit, CV_8UC3, 255.0);
            base_img_ldr = final_8bit;
        } 
        // Otherwise, process global high dynamic range conversion via selected operator
        else {
            base_img_ldr = applyTonemap(base_img_raw, mode,
                                    r_gamma, intensity, light_adapt, color_adapt,
                                    d_gamma, d_saturation, d_bias,
                                    m_gamma, m_scale, m_saturation);
        }

        // Run space profile rendering transitions
        colorspace.apply(base_img_ldr);

        // Keep current region safely bounded and isolate sub-matrix selections
        zoom_state.current_roi &= cv::Rect(0, 0, base_img_ldr.cols, base_img_ldr.rows);
        current_img_ldr = base_img_ldr(zoom_state.current_roi).clone();
        current_img_raw = base_img_raw(zoom_state.current_roi).clone();
        needs_texture = true; 
    };

    // 4.2: Upload modified OpenCV matrices directly to GL GPU texture spaces
    auto updateTexture = [&]()
    {
        if (current_img_ldr.empty()) return;
        if (texture) glDeleteTextures(1, &texture);
        texture = matToTexture(current_img_ldr);
    };

    // 4.3: Low-level file reader allocation and viewport canvas reset
    auto loadRawImage = [&](int i)
    {
        base_img_raw = cv::imread(images[i], cv::IMREAD_UNCHANGED);
        if(!base_img_raw.empty()) {
            zoom_state.Reset(base_img_raw.cols, base_img_raw.rows); 
            colormap.reset(); 
        }
        needs_tonemap = true;
    };

    // Bootstrapping the initial presentation canvas frame states
    loadRawImage(0);
    doTonemap();
    updateTexture();

    // ============================================================================
    // 5. MAIN RENDER LOOP
    // ============================================================================
    while (!glfwWindowShouldClose(window))
    {
        // ------------------------------------------------------------------------
        // 5.1. Window Messages and Polling Events
        // ------------------------------------------------------------------------
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(window, true);
        }

        // Initialize display frame context structures for Dear ImGui backend builders
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // ------------------------------------------------------------------------
        // 5.2. Global Keyboard Input Controllers
        // ------------------------------------------------------------------------
        
        // GLOBAL HARD RESET SHORTCUT (Key: A)
        // Destroys and clears all parameters, zooms, colormaps and sets defaults
        if (ImGui::IsKeyPressed(ImGuiKey_A, false))
        {
            // Reset numerical bounds back to factory default states
            r_gamma = 1.0f; intensity = 0.0f; light_adapt = 0.5f; color_adapt = 0.0f;
            d_gamma = 1.0f; d_saturation = 1.0f; d_bias = 0.85f;
            m_gamma = 1.0f; m_scale = 0.7f; m_saturation = 1.0f;
            
            // Revert fallback active operator to Reinhard and reset color transformations
            mode = 1; 
            colorspace.reset();

            // Refresh raw dimensions data, reset BBox crops and clear colormaps
            loadRawImage(idx);
        }

        // Paint custom crosshair position tracker and compute directory slide indexes
        DrawCustomCursor();
        HandleKeyboardNavigation(idx, images.size(), loadRawImage);

        // ------------------------------------------------------------------------
        // 5.3. Graphical User Interface Layout Presentation
        // ------------------------------------------------------------------------
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

        ImGui::Begin("HDR Viewer", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        ImGui::Text("Tonemapping");

        // Operator mode buttons
        if (ImGui::Button("Reinhard")) { mode = 1; needs_tonemap = true; } ImGui::SameLine();
        if (ImGui::Button("Drago"))    { mode = 2; needs_tonemap = true; } ImGui::SameLine();
        if (ImGui::Button("Mantiuk"))  { mode = 3; needs_tonemap = true; }

        ImGui::Separator();

        // Render dynamic sliders based on current mathematical operation rules
        if (mode == 1) {
            if (ImGui::SliderFloat("Gamma", &r_gamma, 0.1f, 3.0f))           needs_tonemap = true;
            if (ImGui::SliderFloat("Intensity", &intensity, -8.0f, 8.0f))    needs_tonemap = true;
            if (ImGui::SliderFloat("Light Adapt", &light_adapt, 0.0f, 1.0f))  needs_tonemap = true;
            if (ImGui::SliderFloat("Color Adapt", &color_adapt, 0.0f, 1.0f))  needs_tonemap = true;
        } else if (mode == 2) {
            if (ImGui::SliderFloat("Gamma", &d_gamma, 0.1f, 3.0f))           needs_tonemap = true;
            if (ImGui::SliderFloat("Saturation", &d_saturation, 0.0f, 2.0f)) needs_tonemap = true;
            if (ImGui::SliderFloat("Bias", &d_bias, 0.6f, 0.95f))            needs_tonemap = true;
        } else if (mode == 3) {
            if (ImGui::SliderFloat("Gamma", &m_gamma, 0.1f, 3.0f))           needs_tonemap = true;
            if (ImGui::SliderFloat("Scale", &m_scale, 0.1f, 2.0f))           needs_tonemap = true;
            if (ImGui::SliderFloat("Saturation", &m_saturation, 0.0f, 2.0f)) needs_tonemap = true;
        }

        // Display basic zoom recovery trigger
        DrawResetZoomButton(window, current_img_ldr, base_img_ldr, zoom_state, needs_texture);

        // Render dynamic colormap cancellation button if AutoRange scaling is working
        if (colormap.is_active) {
            ImGui::SameLine();
            if (ImGui::Button("Reset Range") || glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
                colormap.reset();
                needs_tonemap = true;
            }
        }

        // ------------------------------------------------------------------------
        // 5.4. State Sequence Resolver
        // ------------------------------------------------------------------------
        if (needs_tonemap)
        {
            doTonemap();
            needs_tonemap = false;
        }
        
        if (needs_texture)
        {
            updateTexture();
            needs_texture = false;
        }

        ImGui::Separator();
        
        // ------------------------------------------------------------------------
        // 5.5. Viewport Canvas Rendering Space
        // ------------------------------------------------------------------------
        ImGui::BeginChild("img", ImVec2(0, 0), true);
        ImVec2 avail = ImGui::GetContentRegionAvail();

        if (!current_img_ldr.empty() && texture)
        {
            // Secure pixel ratio matching setups on fluid panel resizing
            float ar = (float)current_img_ldr.cols / current_img_ldr.rows;
            ImVec2 size = avail;

            if (avail.x / avail.y > ar) size.x = avail.y * ar;
            else size.y = avail.x / ar;

            // Center image layout vector tracking updates
            ImVec2 img_cursor_pos((avail.x - size.x) * 0.5f, (avail.y - size.y) * 0.5f);
            ImGui::SetCursorPos(img_cursor_pos);
            
            ImVec2 img_screen_pos = ImGui::GetCursorScreenPos();
            
            // Render finalized GL texture resource directly on ImGui window context
            ImGui::Image((void*)(intptr_t)texture, size);

            // ------------------------------------------------------------------------
            // 5.6. High-Precision Mouse Interaction Event Handlers
            // ------------------------------------------------------------------------
            
            // 1. Right-Click Drag: Selection BBox + Floating Context Actions Menu
            HandleZoomAndSelection(zoom_state, img_screen_pos, size, current_img_ldr, base_img_ldr, base_img_raw, colormap, needs_tonemap, needs_texture);
            
            // 2. Mouse Scroll Wheel: Dynamic Focus-Preserved Cursor Magnification
            bool needs_roi_tonemap = false;
            HandleScrollZoom(zoom_state, img_screen_pos, size, current_img_raw, base_img_raw, needs_roi_tonemap);
            if (needs_roi_tonemap) {
                // Instantly sync 8-bit slice coordinates without re-triggering full CPU tone computation
                current_img_ldr = base_img_ldr(zoom_state.current_roi).clone();
                needs_texture = true;
            }

            // 3. Left-Click Drag: Viewport Coordinate Canvas Panning Control
            bool needs_pan_update = false;
            HandleMousePanning(zoom_state, size, current_img_raw, base_img_raw, needs_pan_update);
            if (needs_pan_update) {
                current_img_ldr = base_img_ldr(zoom_state.current_roi).clone();
                needs_texture = true;
            }

            // 4. Pixel Sub-Grid Matrix: Draw custom readable color arrays over closeups
            RenderPixelValuesOverlay(zoom_state, img_screen_pos, size, current_img_ldr);
        }

        // Sequential profile key toggle handling shortcuts
        if (ImGui::IsKeyPressed(ImGuiKey_S)) {
            colorspace.cycle();
            needs_tonemap = true;
        }

        ImGui::EndChild();
        ImGui::End();

        // ------------------------------------------------------------------------
        // 5.7. Hardware Buffer Swap Execution
        // ------------------------------------------------------------------------
        ImGui::Render();

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.1f, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // ============================================================================
    // 6. PIPELINE DE-ALLOCATION AND TERMINATION
    // ============================================================================
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}