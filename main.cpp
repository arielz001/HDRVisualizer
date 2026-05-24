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
#include <vector>
#include <cmath>

#include "Polarized.h"
#include "Utils.h"
#include "Tonemapper.h"
#include "MouseControls.h"
#include "KeyboardControls.h"
#include "Colormap.h"
#include "Colorspace.h"

// ============================================================================
// 2. DATA STRUCTURES
// ============================================================================
struct AppContext 
{
    // Images & Graphics
    cv::Mat base_img_raw;    
    cv::Mat base_img_ldr;    
    cv::Mat current_img_ldr; 
    cv::Mat current_img_raw; 
    GLuint texture = 0;      

    // State Tracking
    std::vector<std::string> images;
    int current_idx = 0;
    bool is_polarized = false; 
    bool needs_tonemap = false; 
    bool needs_texture = false; 

    // Internal Modules
    ZoomState zoom_state;
    Colormap colormap;
    Colorspace colorspace;
    int mode = 1; // 1: Reinhard, 2: Drago, 3: Mantiuk
    
    // Tonemapping Parameters
    float r_gamma = 1.0f, intensity = 0.0f, light_adapt = 0.5f, color_adapt = 0.0f; 
    float d_gamma = 1.0f, d_saturation = 1.0f, d_bias = 0.85f;                     
    float m_gamma = 1.0f, m_scale = 0.7f, m_saturation = 1.0f;                     

    void resetToFactoryDefaults() {
        r_gamma = 1.0f; intensity = 0.0f; light_adapt = 0.5f; color_adapt = 0.0f;
        d_gamma = 1.0f; d_saturation = 1.0f; d_bias = 0.85f;
        m_gamma = 1.0f; m_scale = 0.7f; m_saturation = 1.0f;
        mode = 1; 
        is_polarized = false; 
        colorspace.reset();
    }
};

// ============================================================================
// 3. CORE CORE PIPELINE FUNCTIONS
// ============================================================================
void updateGlobalTonemapCache(AppContext& ctx)
{
    if (ctx.base_img_raw.empty()) return;

    cv::Mat full_canvas_ldr;
    if (ctx.colormap.is_active) {
        cv::Mat normalized = ctx.colormap.apply(ctx.base_img_raw);
        normalized.convertTo(full_canvas_ldr, CV_8UC3, 255.0);
    } 
    else {
        full_canvas_ldr = applyTonemap(ctx.base_img_raw, ctx.mode,
                                ctx.r_gamma, ctx.intensity, ctx.light_adapt, ctx.color_adapt,
                                ctx.d_gamma, ctx.d_saturation, ctx.d_bias,
                                ctx.m_gamma, ctx.m_scale, ctx.m_saturation);
    }

    ctx.colorspace.apply(full_canvas_ldr);
    ctx.base_img_ldr = full_canvas_ldr; 
}

void updateViewportImage(AppContext& ctx)
{
    if (ctx.base_img_ldr.empty()) return;

    ctx.zoom_state.current_roi &= cv::Rect(0, 0, ctx.base_img_raw.cols, ctx.base_img_raw.rows);
    if (ctx.zoom_state.current_roi.width <= 0 || ctx.zoom_state.current_roi.height <= 0) {
        ctx.zoom_state.current_roi = cv::Rect(0, 0, ctx.base_img_raw.cols, ctx.base_img_raw.rows);
    }

    ctx.current_img_ldr = ctx.base_img_ldr(ctx.zoom_state.current_roi).clone();
    ctx.current_img_raw = ctx.base_img_raw(ctx.zoom_state.current_roi).clone();

    ctx.needs_texture = true;
}

void updateTexture(AppContext& ctx)
{
    if (ctx.current_img_ldr.empty()) return;
    if (ctx.texture) glDeleteTextures(1, &ctx.texture);
    ctx.texture = matToTexture(ctx.current_img_ldr);
}


void loadRawImage(AppContext& ctx, int i)
{
    std::string file_path = ctx.images[i];
    std::string ext = std::filesystem::path(file_path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    ctx.is_polarized = (ext == ".raw" || ext == ".bin" || ext == ".dat");

    if (ctx.is_polarized) 
    {
        cv::Mat mosaic = dePolarize(file_path); 
        if (!mosaic.empty()) {
            mosaic.convertTo(mosaic, CV_32FC3, 1.0 / 255.0);
            
            int w = mosaic.cols / 2;
            int h = mosaic.rows / 2;

            // Extract clean channels for physical computation
            cv::Mat p0   = mosaic(cv::Rect(0, 0, w, h)).clone();
            cv::Mat p45  = mosaic(cv::Rect(w, 0, w, h)).clone();
            cv::Mat p90  = mosaic(cv::Rect(0, h, w, h)).clone();
            cv::Mat p135 = mosaic(cv::Rect(w, h, w, h)).clone();  

            // Compute Stokes vectors before modifying pixels
            std::vector<cv::Mat> bgr_channels = { p0, p45, p90, p135 };
            PolarizationResult polar = computePolarization(bgr_channels);

            auto addLabel = [](cv::Mat& img, const std::string& text) {
                cv::putText(img, text, cv::Point(10, 40), cv::FONT_HERSHEY_SIMPLEX, 1.5, cv::Scalar(0, 0, 0), 5, cv::LINE_AA);
                cv::putText(img, text, cv::Point(10, 40), cv::FONT_HERSHEY_SIMPLEX, 1.5, cv::Scalar(1, 1, 1), 2, cv::LINE_AA);
            };

            // Burn UI labels into intensity channels
            addLabel(p0,   "0");
            addLabel(p45,  "45");
            addLabel(p90,  "90");
            addLabel(p135, "135");

            // Assemble left mosaic panel
            cv::Mat labeled_mosaic;
            cv::Mat top_mosaic, bot_mosaic;
            cv::hconcat(p0, p45, top_mosaic);
            cv::hconcat(p90, p135, bot_mosaic);
            cv::vconcat(top_mosaic, bot_mosaic, labeled_mosaic);
            
            // Process DoLP
            cv::Mat dolp_8u, dolp_rgb, dolp_32f;
            polar.DoLP.convertTo(dolp_8u, CV_8UC1, 255.0);
            cv::applyColorMap(dolp_8u, dolp_rgb, cv::COLORMAP_JET);
            dolp_rgb.convertTo(dolp_32f, CV_32FC3, 1.0 / 255.0);
            cv::resize(dolp_32f, dolp_32f, cv::Size(w, h));
            addLabel(dolp_32f, "DoLP"); 

            // Process AoLP
            cv::Mat aolp_norm = (polar.AoLP + (M_PI / 2.0f)) / M_PI; 
            cv::Mat aolp_8u, aolp_rgb, aolp_32f;
            aolp_norm.convertTo(aolp_8u, CV_8UC1, 255.0);
            cv::applyColorMap(aolp_8u, aolp_rgb, cv::COLORMAP_HSV);
            aolp_rgb.convertTo(aolp_32f, CV_32FC3, 1.0 / 255.0);
            cv::resize(aolp_32f, aolp_32f, cv::Size(w, h));
            addLabel(aolp_32f, "AoLP"); 

            // Assemble right analysis panel
            cv::Mat right_block;
            cv::vconcat(aolp_32f, dolp_32f, right_block);

            // Generate final monolithic 6-panel matrix
            ctx.base_img_raw = cv::Mat();
            cv::hconcat(labeled_mosaic, right_block, ctx.base_img_raw);
        }
    } 
    else 
    {
        ctx.base_img_raw = cv::imread(file_path, cv::IMREAD_UNCHANGED);
        if (!ctx.base_img_raw.empty() && ctx.base_img_raw.depth() != CV_32F) {
            double max_val = (ctx.base_img_raw.depth() == CV_16U) ? 65535.0 : 255.0;
            ctx.base_img_raw.convertTo(ctx.base_img_raw, CV_32FC3, 1.0 / max_val);
        }
    }

    if (!ctx.base_img_raw.empty()) {
        ctx.zoom_state.Reset(ctx.base_img_raw.cols, ctx.base_img_raw.rows); 
        ctx.colormap.reset(); 
        updateGlobalTonemapCache(ctx);
    }
    ctx.needs_tonemap = false; 
    updateViewportImage(ctx); 
}
// ============================================================================
// 4. GUI RENDERING SUB-ROUTINES
// ============================================================================
void renderControlPanel(AppContext& ctx, GLFWwindow* window)
{
    ImGui::Text("Tonemapping");
    if (ImGui::Button("Reinhard")) { ctx.mode = 1; ctx.needs_tonemap = true; } ImGui::SameLine();
    if (ImGui::Button("Drago"))    { ctx.mode = 2; ctx.needs_tonemap = true; } ImGui::SameLine();
    if (ImGui::Button("Mantiuk"))  { ctx.mode = 3; ctx.needs_tonemap = true; }

    ImGui::Separator();

    if (ctx.mode == 1) {
        if (ImGui::SliderFloat("Gamma", &ctx.r_gamma, 0.1f, 3.0f))           ctx.needs_tonemap = true;
        if (ImGui::SliderFloat("Intensity", &ctx.intensity, -8.0f, 8.0f))    ctx.needs_tonemap = true;
        if (ImGui::SliderFloat("Light Adapt", &ctx.light_adapt, 0.0f, 1.0f))  ctx.needs_tonemap = true;
        if (ImGui::SliderFloat("Color Adapt", &ctx.color_adapt, 0.0f, 1.0f))  ctx.needs_tonemap = true;
    } else if (ctx.mode == 2) {
        if (ImGui::SliderFloat("Gamma", &ctx.d_gamma, 0.1f, 3.0f))           ctx.needs_tonemap = true;
        if (ImGui::SliderFloat("Saturation", &ctx.d_saturation, 0.0f, 2.0f)) ctx.needs_tonemap = true;
        if (ImGui::SliderFloat("Bias", &ctx.d_bias, 0.6f, 0.95f))            ctx.needs_tonemap = true;
    } else if (ctx.mode == 3) {
        if (ImGui::SliderFloat("Gamma", &ctx.m_gamma, 0.1f, 3.0f))           ctx.needs_tonemap = true;
        if (ImGui::SliderFloat("Scale", &ctx.m_scale, 0.1f, 2.0f))           ctx.needs_tonemap = true;
        if (ImGui::SliderFloat("Saturation", &ctx.m_saturation, 0.0f, 2.0f)) ctx.needs_tonemap = true;
    }

    if (ImGui::Button("Reset Zoom")) {
        ctx.zoom_state.Reset(ctx.base_img_raw.cols, ctx.base_img_raw.rows);
        updateViewportImage(ctx);
    }

    if (ctx.colormap.is_active) {
        ImGui::SameLine();
        if (ImGui::Button("Reset Range") || glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
            ctx.colormap.reset();
            ctx.needs_tonemap = true;
        }
    }
}

void renderViewportAndInteractions(AppContext& ctx)
{
    ImGui::BeginChild("img", ImVec2(0, 0), true);
    ImVec2 avail = ImGui::GetContentRegionAvail();

    if (!ctx.current_img_ldr.empty() && ctx.texture)
    {
        float ar = (float)ctx.current_img_ldr.cols / ctx.current_img_ldr.rows;
        ImVec2 size = avail;

        if (avail.x / avail.y > ar) size.x = avail.y * ar;
        else size.y = avail.x / ar;

        ImVec2 img_cursor_pos((avail.x - size.x) * 0.5f, (avail.y - size.y) * 0.5f);
        ImGui::SetCursorPos(img_cursor_pos);
        
        ImVec2 img_screen_pos = ImGui::GetCursorScreenPos();
        ImGui::Image((void*)(intptr_t)ctx.texture, size);

        // Process Interactive Inputs
        HandleZoomAndSelection(ctx.zoom_state, img_screen_pos, size, ctx.current_img_ldr, ctx.base_img_ldr, ctx.base_img_raw, ctx.colormap, ctx.needs_tonemap, ctx.needs_texture);
        if (ctx.needs_tonemap) {
            updateGlobalTonemapCache(ctx);
            updateViewportImage(ctx);
            ctx.needs_tonemap = false;
        }

        bool needs_roi_update = false;
        HandleScrollZoom(ctx.zoom_state, img_screen_pos, size, ctx.current_img_raw, ctx.base_img_raw, needs_roi_update);
        if (needs_roi_update) {
            updateViewportImage(ctx);
        }

        bool needs_pan_update = false;
        ImVec2 mouse_pos = ImGui::GetMousePos();
        bool mouse_is_over_image = (mouse_pos.x >= img_screen_pos.x && mouse_pos.x <= (img_screen_pos.x + size.x)) &&
                                    (mouse_pos.y >= img_screen_pos.y && mouse_pos.y <= (img_screen_pos.y + size.y));    
        if (mouse_is_over_image) {
            HandleMousePanning(ctx.zoom_state, size, ctx.current_img_raw, ctx.base_img_raw, needs_pan_update);
            if (needs_pan_update) {
                updateViewportImage(ctx); 
            }
        }

        RenderPixelValuesOverlay(ctx.zoom_state, img_screen_pos, size, ctx.current_img_raw);

        // Overlay Polar HUD
        if (ctx.is_polarized)
        {
            ImGui::SetNextWindowPos(ImVec2(img_screen_pos.x + 10, img_screen_pos.y + 10));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

            ImGui::PopStyleColor(2);
        }
    }
    ImGui::EndChild();
}

// ============================================================================
// 5. MAIN ENTRY POINT
// ============================================================================
int main(int argc, char** argv)
{
    if (!glfwInit()) return -1;

    if (argc < 2)
    {
        std::cout << "Usage: ./bin/rev <image_or_folder>\n";
        return -1;
    }

    AppContext ctx;
    std::string input = argv[1];

    if (std::filesystem::is_directory(input))
        ctx.images = getImages(input);
    else
        ctx.images.push_back(input);

    if (ctx.images.empty()) return -1;

    #ifdef __APPLE__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    #endif

    GLFWwindow* window = glfwCreateWindow(1280, 720, "REV Engine - HDR & Polarized Visualizer", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); 

    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    // Initial Loading Sequence
    loadRawImage(ctx, 0);
    updateTexture(ctx);

    // Main Render Loop
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Global Key Actions
        if (ImGui::IsKeyPressed(ImGuiKey_A, false))
        {
            ctx.resetToFactoryDefaults();
            loadRawImage(ctx, ctx.current_idx);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_S)) {
            ctx.colorspace.cycle();
            updateGlobalTonemapCache(ctx);
            updateViewportImage(ctx);
        }

        DrawCustomCursor();
        
        // Navigation Wrapper
        auto navigationCallback = [&](int new_idx) { loadRawImage(ctx, new_idx); };
        HandleKeyboardNavigation(ctx.current_idx, ctx.images.size(), navigationCallback);

        // Fullscreen Setup Window
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("HDR Viewer", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        // UI Modules
        renderControlPanel(ctx, window);
        ImGui::Separator();

        // Evaluators & Pipelines Updates
        if (ctx.needs_tonemap) {
            updateGlobalTonemapCache(ctx);
            updateViewportImage(ctx);
            ctx.needs_tonemap = false;
        }
        if (ctx.needs_texture) {
            updateTexture(ctx);
            ctx.needs_texture = false;
        }

        renderViewportAndInteractions(ctx);

        ImGui::End(); // End Main Window
        ImGui::Render();

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.1f, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // Cleanup Pipeline
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (ctx.texture) glDeleteTextures(1, &ctx.texture);
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}