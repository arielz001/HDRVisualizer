#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <iostream>
#include <filesystem>
#include <algorithm>

// Include our modules
#include "Utils.h"
#include "Tonemapper.h"
#include "MouseControls.h"
#include "KeyboardControls.h"
#include "Colormap.h"
#include "Colorspace.h"


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

    GLFWwindow* window = glfwCreateWindow(1280, 720, "HDR Viewer", NULL, NULL);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    // --- LOGIC FOR IMAGES ---
    cv::Mat base_img_raw;    // original hdr image
    cv::Mat base_img_ldr;    // complete image with tonemapping (8-bits)
    cv::Mat current_img_ldr; // current crop (zoom) of the image tonemaped
    cv::Mat current_img_raw; // current crop (zoom) of the raw data
    GLuint texture = 0;

    ZoomState zoom_state;
    Colormap colormap;
    Colorspace colorspace;
    int mode = 1;
    
    // Default variables setup
    float r_gamma = 1.0f, intensity = 0.0f, light_adapt = 0.5f, color_adapt = 0.0f;
    float d_gamma = 1.0f, d_saturation = 1.0f, d_bias = 0.85f;
    float m_gamma = 1.0f, m_scale = 0.7f, m_saturation = 1.0f;
    
    // updates
    bool needs_tonemap = false; // ONLY IS ACTIVATED IF YOU CHANGE HDR PARAMETERS
    bool needs_texture = false; // IS ACTIVATED IF YOU DO A ZOOM (MORE FAST)
        
    auto doTonemap = [&]()
    {
        if (base_img_raw.empty()) return;

        if (colormap.is_active) {
            cv::Mat normalized = colormap.apply(base_img_raw);
            cv::Mat final_8bit;
            normalized.convertTo(final_8bit, CV_8UC3, 255.0);
            base_img_ldr = final_8bit;
        } 
        else {
            base_img_ldr = applyTonemap(base_img_raw, mode,
                                    r_gamma, intensity, light_adapt, color_adapt,
                                    d_gamma, d_saturation, d_bias,
                                    m_gamma, m_scale, m_saturation);
        }

        // Apply the color space mode transition here
        colorspace.apply(base_img_ldr);

        zoom_state.current_roi &= cv::Rect(0, 0, base_img_ldr.cols, base_img_ldr.rows);
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
        base_img_raw = cv::imread(images[i], cv::IMREAD_UNCHANGED);
        if(!base_img_raw.empty()) {
            zoom_state.Reset(base_img_raw.cols, base_img_raw.rows); 
            colormap.reset(); 
        }
        needs_tonemap = true;
    };

    loadRawImage(0);
    doTonemap();
    updateTexture();

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

        // --- GLOBAL HARD RESET CONTROLLER (Tecla A) ---
        if (ImGui::IsKeyPressed(ImGuiKey_A, false))
        {
            // 1. Reset values for Reinhard mode
            r_gamma = 1.0f; intensity = 0.0f; light_adapt = 0.5f; color_adapt = 0.0f;
            
            // 2. Reset values for Drago mode
            d_gamma = 1.0f; d_saturation = 1.0f; d_bias = 0.85f;
            
            // 3. Reset values for Mantiuk mode
            m_gamma = 1.0f; m_scale = 0.7f; m_saturation = 1.0f;
            
            // 4. Reset operator mode default to Reinhard
            mode = 1; 

            // 5. Reset Image Viewport, ROI and clear any applied ranges
            loadRawImage(idx);
        }

        // Cursor & Nav
        DrawCustomCursor();
        HandleKeyboardNavigation(idx, images.size(), loadRawImage);

        ImGui::SetNextWindowPos({0,0});
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

        ImGui::Begin("HDR Viewer", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        // ---------------- UI ----------------
        ImGui::Text("Tonemapping");

        if (ImGui::Button("Reinhard")) { mode = 1; needs_tonemap = true; } ImGui::SameLine();
        if (ImGui::Button("Drago")) { mode = 2; needs_tonemap = true; } ImGui::SameLine();
        if (ImGui::Button("Mantiuk")) { mode = 3; needs_tonemap = true; }

        ImGui::Separator();

        if (mode == 1) {
            if (ImGui::SliderFloat("Gamma", &r_gamma, 0.1f, 3.0f)) needs_tonemap = true;
            if (ImGui::SliderFloat("Intensity", &intensity, -8.0f, 8.0f)) needs_tonemap = true;
            if (ImGui::SliderFloat("Light Adapt", &light_adapt, 0.0f, 1.0f)) needs_tonemap = true;
            if (ImGui::SliderFloat("Color Adapt", &color_adapt, 0.0f, 1.0f)) needs_tonemap = true;
        } else if (mode == 2) {
            if (ImGui::SliderFloat("Gamma", &d_gamma, 0.1f, 3.0f)) needs_tonemap = true;
            if (ImGui::SliderFloat("Saturation", &d_saturation, 0.0f, 2.0f)) needs_tonemap = true;
            if (ImGui::SliderFloat("Bias", &d_bias, 0.6f, 0.95f)) needs_tonemap = true;
        } else if (mode == 3) {
            if (ImGui::SliderFloat("Gamma", &m_gamma, 0.1f, 3.0f)) needs_tonemap = true;
            if (ImGui::SliderFloat("Scale", &m_scale, 0.1f, 2.0f)) needs_tonemap = true;
            if (ImGui::SliderFloat("Saturation", &m_saturation, 0.0f, 2.0f)) needs_tonemap = true;
        }

        // ZOOM BUTTON
        DrawResetZoomButton(window, current_img_ldr, base_img_ldr, zoom_state, needs_texture);

        // BOTÓN PARA VOLVER AL RANGO DINÁMICO COMPLETO ORIGINAL SI EL AUTO RANGE ESTÁ ACTIVO
        if (colormap.is_active) {
            ImGui::SameLine();
            if (ImGui::Button("Reset Range") || glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
                colormap.reset();
                needs_tonemap = true;
            }
        }

        // PROCESS UPDATES IN ORDER
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
        
        // ---------------- IMAGE RENDER ----------------
        ImGui::BeginChild("img", ImVec2(0,0), true);
        ImVec2 avail = ImGui::GetContentRegionAvail();

        if (!current_img_ldr.empty() && texture)
        {
            float ar = (float)current_img_ldr.cols / current_img_ldr.rows;
            ImVec2 size = avail;

            if (avail.x / avail.y > ar) size.x = avail.y * ar;
            else size.y = avail.x / ar;

            ImVec2 img_cursor_pos((avail.x - size.x)*0.5f, (avail.y - size.y)*0.5f);
            ImGui::SetCursorPos(img_cursor_pos);
            
            ImVec2 img_screen_pos = ImGui::GetCursorScreenPos();
            
            ImGui::Image((void*)(intptr_t)texture, size);

            // BBox, Scroll Zooming & Panning
            HandleZoomAndSelection(zoom_state, img_screen_pos, size, current_img_ldr, base_img_ldr, base_img_raw, colormap, needs_tonemap, needs_texture);
            
            bool needs_roi_tonemap = false;
            HandleScrollZoom(zoom_state, img_screen_pos, size, current_img_raw, base_img_raw, needs_roi_tonemap);
            if (needs_roi_tonemap) {
                current_img_ldr = base_img_ldr(zoom_state.current_roi).clone();
                needs_texture = true;
            }

            bool needs_pan_update = false;
            HandleMousePanning(zoom_state, size, current_img_raw, base_img_raw, needs_pan_update);
            if (needs_pan_update) {
                current_img_ldr = base_img_ldr(zoom_state.current_roi).clone();
                needs_texture = true;
            }

            RenderPixelValuesOverlay(zoom_state, img_screen_pos, size, current_img_ldr);
        }

        if (ImGui::IsKeyPressed(ImGuiKey_S)) {
            colorspace.cycle();
            needs_tonemap = true;
        }

        ImGui::EndChild();
        ImGui::End();

        ImGui::Render();

        int w,h;
        glfwGetFramebufferSize(window,&w,&h);
        glViewport(0,0,w,h);
        glClearColor(0.1f,0.1f,0.1f,1);
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