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
// 2. MAIN ENTRY POINT
// ============================================================================
int main(int argc, char** argv)
{
    if (!glfwInit()) return -1;

    if (argc < 2)
    {
        std::cout << "Usage: ./bin/HDRVisualizer <image_or_folder>\n";
        return -1;
    }

    std::string input = argv[1];
    std::vector<std::string> images;
    int idx = 0;

    if (std::filesystem::is_directory(input))
        images = getImages(input);
    else
        images.push_back(input);

    if (images.empty()) return -1;

    #ifdef __APPLE__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    #endif

    GLFWwindow* window = glfwCreateWindow(1280, 720, "HDR Viewer", NULL, NULL);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); 

    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    // ============================================================================
    // 3. APPLICATION STATE VARIABLES
    // ============================================================================
    cv::Mat base_img_raw;    
    cv::Mat base_img_ldr;    
    cv::Mat current_img_ldr; 
    cv::Mat current_img_raw; 
    GLuint texture = 0;      

    ZoomState zoom_state;
    Colormap colormap;
    Colorspace colorspace;
    int mode = 1;              
    bool is_polarized = false; 
    
    float r_gamma = 1.0f, intensity = 0.0f, light_adapt = 0.5f, color_adapt = 0.0f; 
    float d_gamma = 1.0f, d_saturation = 1.0f, d_bias = 0.85f;                     
    float m_gamma = 1.0f, m_scale = 0.7f, m_saturation = 1.0f;                     
    
    bool needs_tonemap = false; 
    bool needs_texture = false; 
        
    // ============================================================================
    // 4. CONTROL UTILITIES
    // ============================================================================
    auto updateGlobalTonemapCache = [&]()
    {
        if (base_img_raw.empty()) return;

        cv::Mat full_canvas_ldr;
        if (colormap.is_active) {
            cv::Mat normalized = colormap.apply(base_img_raw);
            normalized.convertTo(full_canvas_ldr, CV_8UC3, 255.0);
        } 
        else {
            full_canvas_ldr = applyTonemap(base_img_raw, mode,
                                    r_gamma, intensity, light_adapt, color_adapt,
                                    d_gamma, d_saturation, d_bias,
                                    m_gamma, m_scale, m_saturation);
        }

        colorspace.apply(full_canvas_ldr);
        base_img_ldr = full_canvas_ldr; 
    };

    auto updateViewportImage = [&]()
    {
        if (base_img_ldr.empty()) return;

        zoom_state.current_roi &= cv::Rect(0, 0, base_img_raw.cols, base_img_raw.rows);
        if (zoom_state.current_roi.width <= 0 || zoom_state.current_roi.height <= 0) {
            zoom_state.current_roi = cv::Rect(0, 0, base_img_raw.cols, base_img_raw.rows);
        }

        current_img_ldr = base_img_ldr(zoom_state.current_roi).clone();
        current_img_raw = base_img_raw(zoom_state.current_roi).clone();

        needs_texture = true;
    };

    auto updateTexture = [&]()
    {
        if (current_img_ldr.empty()) return;
        if (texture) glDeleteTextures(1, &texture);
        texture = matToTexture(current_img_ldr);
    };

    auto loadRawImage = [&](int i)
    {
        std::string file_path = images[i];
        std::string ext = std::filesystem::path(file_path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        is_polarized = false; 

        if (ext == ".raw" || ext == ".bin" || ext == ".dat") {
            is_polarized = true;
        }

        if (is_polarized) 
        {
            cv::Mat mosaic = dePolarize(file_path); 
            
            if (!mosaic.empty()) {
                mosaic.convertTo(mosaic, CV_32FC3, 1.0 / 255.0);
                
                int w = mosaic.cols / 2;
                int h = mosaic.rows / 2;
                std::vector<cv::Mat> bgr_channels = {
                    mosaic(cv::Rect(0, 0, w, h)).clone(), 
                    mosaic(cv::Rect(w, 0, w, h)).clone(), 
                    mosaic(cv::Rect(0, h, w, h)).clone(), 
                    mosaic(cv::Rect(w, h, w, h)).clone()  
                };

                PolarizationResult polar = computePolarization(bgr_channels);
                
                cv::Mat dolp_8u, dolp_rgb;
                polar.DoLP.convertTo(dolp_8u, CV_8UC1, 255.0);
                cv::applyColorMap(dolp_8u, dolp_rgb, cv::COLORMAP_JET);
                cv::Mat dolp_32f;
                dolp_rgb.convertTo(dolp_32f, CV_32FC3, 1.0 / 255.0);

                cv::Mat aolp_norm = (polar.AoLP + (M_PI / 2.0f)) / M_PI; 
                cv::Mat aolp_8u, aolp_rgb;
                aolp_norm.convertTo(aolp_8u, CV_8UC1, 255.0);
                cv::applyColorMap(aolp_8u, aolp_rgb, cv::COLORMAP_HSV);
                cv::Mat aolp_32f;
                aolp_rgb.convertTo(aolp_32f, CV_32FC3, 1.0 / 255.0);

                cv::resize(aolp_32f, aolp_32f, cv::Size(w, h));
                cv::resize(dolp_32f, dolp_32f, cv::Size(w, h));

                cv::Mat right_block;
                cv::vconcat(aolp_32f, dolp_32f, right_block);

                base_img_raw = cv::Mat();
                cv::hconcat(mosaic, right_block, base_img_raw);
            }
        } 
        else 
        {
            base_img_raw = cv::imread(file_path, cv::IMREAD_UNCHANGED);
            if (!base_img_raw.empty() && base_img_raw.depth() != CV_32F) {
                double max_val = (base_img_raw.depth() == CV_16U) ? 65535.0 : 255.0;
                base_img_raw.convertTo(base_img_raw, CV_32FC3, 1.0 / max_val);
            }
        }

        if(!base_img_raw.empty()) {
            zoom_state.Reset(base_img_raw.cols, base_img_raw.rows); 
            colormap.reset(); 
            updateGlobalTonemapCache();
        }
        needs_tonemap = false; 
        updateViewportImage(); 
    };

    loadRawImage(0);
    updateTexture();

    // ============================================================================
    // 5. MAIN RENDER LOOP
    // ============================================================================
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(window, true);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (ImGui::IsKeyPressed(ImGuiKey_A, false))
        {
            r_gamma = 1.0f; intensity = 0.0f; light_adapt = 0.5f; color_adapt = 0.0f;
            d_gamma = 1.0f; d_saturation = 1.0f; d_bias = 0.85f;
            m_gamma = 1.0f; m_scale = 0.7f; m_saturation = 1.0f;
            mode = 1; is_polarized = false; colorspace.reset();
            loadRawImage(idx);
        }

        DrawCustomCursor();
        HandleKeyboardNavigation(idx, images.size(), loadRawImage);

        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("HDR Viewer", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        ImGui::Text("Tonemapping");
        if (ImGui::Button("Reinhard")) { mode = 1; needs_tonemap = true; } ImGui::SameLine();
        if (ImGui::Button("Drago"))    { mode = 2; needs_tonemap = true; } ImGui::SameLine();
        if (ImGui::Button("Mantiuk"))  { mode = 3; needs_tonemap = true; }

        ImGui::Separator();

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

        if (ImGui::Button("Reset Zoom")) {
            zoom_state.Reset(base_img_raw.cols, base_img_raw.rows);
            updateViewportImage();
        }

        if (colormap.is_active) {
            ImGui::SameLine();
            if (ImGui::Button("Reset Range") || glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
                colormap.reset();
                needs_tonemap = true;
            }
        }

        if (needs_tonemap)
        {
            updateGlobalTonemapCache();
            updateViewportImage();
            needs_tonemap = false;
        }
        
        if (needs_texture)
        {
            updateTexture();
            needs_texture = false;
        }

        ImGui::Separator();
        
        ImGui::BeginChild("img", ImVec2(0, 0), true);
        ImVec2 avail = ImGui::GetContentRegionAvail();

        if (!current_img_ldr.empty() && texture)
        {
            float ar = (float)current_img_ldr.cols / current_img_ldr.rows;
            ImVec2 size = avail;

            if (avail.x / avail.y > ar) size.x = avail.y * ar;
            else size.y = avail.x / ar;

            ImVec2 img_cursor_pos((avail.x - size.x) * 0.5f, (avail.y - size.y) * 0.5f);
            ImGui::SetCursorPos(img_cursor_pos);
            
            ImVec2 img_screen_pos = ImGui::GetCursorScreenPos();
            ImGui::Image((void*)(intptr_t)texture, size);

            // ============================================================================
            // 5.6. INTERACTION EVENT HANDLERS
            // ============================================================================
            HandleZoomAndSelection(zoom_state, img_screen_pos, size, current_img_ldr, base_img_ldr, base_img_raw, colormap, needs_tonemap, needs_texture);
            if (needs_tonemap) {
                updateGlobalTonemapCache();
                updateViewportImage();
                needs_tonemap = false;
            }

            bool needs_roi_update = false;
            HandleScrollZoom(zoom_state, img_screen_pos, size, current_img_raw, base_img_raw, needs_roi_update);
            if (needs_roi_update) {
                updateViewportImage();
            }

            bool needs_pan_update = false;
            ImVec2 mouse_pos = ImGui::GetMousePos();
            bool mouse_is_over_image = (mouse_pos.x >= img_screen_pos.x && mouse_pos.x <= (img_screen_pos.x + size.x)) &&
                                        (mouse_pos.y >= img_screen_pos.y && mouse_pos.y <= (img_screen_pos.y + size.y));    
            if (mouse_is_over_image) {
                HandleMousePanning(zoom_state, size, current_img_raw, base_img_raw, needs_pan_update);
                if (needs_pan_update) {
                    updateViewportImage(); 
                }
            }

            RenderPixelValuesOverlay(zoom_state, img_screen_pos, size, current_img_raw);

            // ============================================================================
            // 5.7. OVERLAY HUD GENERATION
            // ============================================================================
            if (is_polarized)
            {
                ImGui::SetNextWindowPos(ImVec2(img_screen_pos.x + 10, img_screen_pos.y + 10));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

                ImGui::BeginChild("PolarHUD", ImVec2(400, 55), false, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "OPTIMIZED 6-PANEL MONOLITHIC RENDERING ENGINE");
                ImGui::Text("Selection box works natively over all unified grid spaces.");
                ImGui::EndChild();

                ImGui::PopStyleColor(2);
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_S)) {
            colorspace.cycle();
            updateGlobalTonemapCache();
            updateViewportImage();
        }

        ImGui::EndChild();
        ImGui::End();

        ImGui::Render();

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.1f, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}