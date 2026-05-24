// ============================================================================
// 1. LIBRARIES
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
// 2. STRUCTURES
// ============================================================================
struct AppContext 
{
    cv::Mat base_img_raw;    
    cv::Mat base_img_ldr;    
    cv::Mat current_img_ldr; 
    cv::Mat current_img_raw; 
    GLuint texture = 0;      

    std::vector<cv::Mat> polar_raw_channels; 
    std::vector<cv::Mat> polar_ldr_channels; 
    std::vector<cv::Mat> polar_current_ldr;  
    std::vector<cv::Mat> polar_current_raw;  
    std::vector<GLuint> polar_textures;      
    std::vector<std::string> polar_names = {"0 deg", "45 deg", "AoLP", "90 deg", "135 deg", "DoLP"};

    std::vector<std::string> images;
    int current_idx = 0;
    bool is_polarized = false; 
    bool needs_tonemap = false; 
    bool needs_texture = false; 

    ZoomState zoom_state;
    Colormap colormap;
    Colorspace colorspace;
    int mode = 1; 
    
    float r_gamma = 1.0f, intensity = 0.0f, light_adapt = 0.5f, color_adapt = 0.0f; 
    float d_gamma = 1.0f, d_saturation = 1.0f, d_bias = 0.85f;                     
    float m_gamma = 1.0f, m_scale = 0.7f, m_saturation = 1.0f;                     

    AppContext() {
        polar_raw_channels.resize(6);
        polar_ldr_channels.resize(6);
        polar_current_ldr.resize(6);
        polar_current_raw.resize(6);
        polar_textures.assign(6, 0);
    }

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
// 3. PIPELINE FUNCTIONS
// ============================================================================
void updateGlobalTonemapCache(AppContext& ctx)
{
    if (ctx.is_polarized) {
        for (int i = 0; i < 6; ++i) {
            if (ctx.polar_raw_channels[i].empty()) continue;
            
            cv::Mat ldr;
            if (i == 2 || i == 5) {
                ctx.polar_raw_channels[i].convertTo(ldr, CV_8UC3, 255.0);
            } 
            else {
                cv::Mat input_raw = ctx.polar_raw_channels[i];
                if (ctx.colormap.is_active) {
                    input_raw = ctx.colormap.apply(ctx.polar_raw_channels[i]); 
                }

                ldr = applyTonemap(input_raw, ctx.mode,
                                   ctx.r_gamma, ctx.intensity, ctx.light_adapt, ctx.color_adapt,
                                   ctx.d_gamma, ctx.d_saturation, ctx.d_bias,
                                   ctx.m_gamma, ctx.m_scale, ctx.m_saturation);
            }
            ctx.colorspace.apply(ldr);
            ctx.polar_ldr_channels[i] = ldr;
        }
    } else {
        if (ctx.base_img_raw.empty()) return;
        cv::Mat full_canvas_ldr;
        if (ctx.colormap.is_active) {
            cv::Mat normalized = ctx.colormap.apply(ctx.base_img_raw);
            full_canvas_ldr = applyTonemap(normalized, ctx.mode,
                                    ctx.r_gamma, ctx.intensity, ctx.light_adapt, ctx.color_adapt,
                                    ctx.d_gamma, ctx.d_saturation, ctx.d_bias,
                                    ctx.m_gamma, ctx.m_scale, ctx.m_saturation);
        } else {
            full_canvas_ldr = applyTonemap(ctx.base_img_raw, ctx.mode,
                                    ctx.r_gamma, ctx.intensity, ctx.light_adapt, ctx.color_adapt,
                                    ctx.d_gamma, ctx.d_saturation, ctx.d_bias,
                                    ctx.m_gamma, ctx.m_scale, ctx.m_saturation);
        }
        ctx.colorspace.apply(full_canvas_ldr);
        ctx.base_img_ldr = full_canvas_ldr; 
    }
}

void updateViewportImage(AppContext& ctx)
{
    if (ctx.is_polarized) {
        if (ctx.polar_ldr_channels[0].empty()) return;
        
        int w = ctx.polar_raw_channels[0].cols;
        int h = ctx.polar_raw_channels[0].rows;
        
        ctx.zoom_state.current_roi &= cv::Rect(0, 0, w, h);
        if (ctx.zoom_state.current_roi.width <= 0 || ctx.zoom_state.current_roi.height <= 0) {
            ctx.zoom_state.current_roi = cv::Rect(0, 0, w, h);
        }

        for (int i = 0; i < 6; ++i) {
            ctx.polar_current_ldr[i] = ctx.polar_ldr_channels[i](ctx.zoom_state.current_roi).clone();
            ctx.polar_current_raw[i] = ctx.polar_raw_channels[i](ctx.zoom_state.current_roi).clone();
        }
    } else {
        if (ctx.base_img_ldr.empty()) return;
        ctx.zoom_state.current_roi &= cv::Rect(0, 0, ctx.base_img_raw.cols, ctx.base_img_raw.rows);
        if (ctx.zoom_state.current_roi.width <= 0 || ctx.zoom_state.current_roi.height <= 0) {
            ctx.zoom_state.current_roi = cv::Rect(0, 0, ctx.base_img_raw.cols, ctx.base_img_raw.rows);
        }
        ctx.current_img_ldr = ctx.base_img_ldr(ctx.zoom_state.current_roi).clone();
        ctx.current_img_raw = ctx.base_img_raw(ctx.zoom_state.current_roi).clone();
    }
    ctx.needs_texture = true;
}

void updateTexture(AppContext& ctx)
{
    if (ctx.is_polarized) {
        for (int i = 0; i < 6; ++i) {
            if (ctx.polar_current_ldr[i].empty()) continue;
            if (ctx.polar_textures[i]) glDeleteTextures(1, &ctx.polar_textures[i]);
            ctx.polar_textures[i] = matToTexture(ctx.polar_current_ldr[i]);
        }
    } else {
        if (ctx.current_img_ldr.empty()) return;
        if (ctx.texture) glDeleteTextures(1, &ctx.texture);
        ctx.texture = matToTexture(ctx.current_img_ldr);
    }
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

            ctx.polar_raw_channels[0] = mosaic(cv::Rect(0, 0, w, h)).clone(); // 0 deg
            ctx.polar_raw_channels[1] = mosaic(cv::Rect(w, 0, w, h)).clone(); // 45 deg
            ctx.polar_raw_channels[3] = mosaic(cv::Rect(0, h, w, h)).clone(); // 90 deg
            ctx.polar_raw_channels[4] = mosaic(cv::Rect(w, h, w, h)).clone(); // 135 deg

            std::vector<cv::Mat> bgr_channels = { ctx.polar_raw_channels[0], ctx.polar_raw_channels[1], ctx.polar_raw_channels[3], ctx.polar_raw_channels[4] };
            PolarizationResult polar = computePolarization(bgr_channels);
            
            // AoLP channel
            cv::Mat aolp_norm = (polar.AoLP + (M_PI / 2.0f)) / M_PI; 
            cv::Mat aolp_8u, aolp_rgb;
            aolp_norm.convertTo(aolp_8u, CV_8UC1, 255.0);
            cv::applyColorMap(aolp_8u, aolp_rgb, cv::COLORMAP_HSV);
            aolp_rgb.convertTo(ctx.polar_raw_channels[2], CV_32FC3, 1.0 / 255.0);
            cv::resize(ctx.polar_raw_channels[2], ctx.polar_raw_channels[2], cv::Size(w, h));

            // DoLP channel
            cv::Mat dolp_8u, dolp_rgb;
            polar.DoLP.convertTo(dolp_8u, CV_8UC1, 255.0);
            cv::applyColorMap(dolp_8u, dolp_rgb, cv::COLORMAP_JET);
            dolp_rgb.convertTo(ctx.polar_raw_channels[5], CV_32FC3, 1.0 / 255.0);
            cv::resize(ctx.polar_raw_channels[5], ctx.polar_raw_channels[5], cv::Size(w, h));

            ctx.zoom_state.Reset(w, h); 
        }
    } 
    else 
    {
        ctx.base_img_raw = cv::imread(file_path, cv::IMREAD_UNCHANGED);
        if (!ctx.base_img_raw.empty() && ctx.base_img_raw.depth() != CV_32F) {
            double max_val = (ctx.base_img_raw.depth() == CV_16U) ? 65535.0 : 255.0;
            ctx.base_img_raw.convertTo(ctx.base_img_raw, CV_32FC3, 1.0 / max_val);
        }
        if (!ctx.base_img_raw.empty()) {
            ctx.zoom_state.Reset(ctx.base_img_raw.cols, ctx.base_img_raw.rows);
        }
    }

    ctx.colormap.reset(); 
    updateGlobalTonemapCache(ctx);
    ctx.needs_tonemap = false; 
    updateViewportImage(ctx); 
}

// ============================================================================
// 4. RENDERING SUB-ROUTINES
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
        int w = ctx.is_polarized ? ctx.polar_raw_channels[0].cols : ctx.base_img_raw.cols;
        int h = ctx.is_polarized ? ctx.polar_raw_channels[0].rows : ctx.base_img_raw.rows;
        ctx.zoom_state.Reset(w, h);
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

    if (!ctx.is_polarized) 
    {
        // --- Single Viewport Mode ---
        if (!ctx.current_img_ldr.empty() && ctx.texture)
        {
            float ar = (float)ctx.current_img_ldr.cols / ctx.current_img_ldr.rows;
            ImVec2 size = avail;
            if (avail.x / avail.y > ar) size.x = avail.y * ar; else size.y = avail.x / ar;

            ImVec2 img_cursor_pos((avail.x - size.x) * 0.5f, (avail.y - size.y) * 0.5f);
            ImGui::SetCursorPos(img_cursor_pos);
            ImVec2 img_screen_pos = ImGui::GetCursorScreenPos();
            
            ImGui::Image((void*)(intptr_t)ctx.texture, size);

            HandleZoomAndSelection(ctx.zoom_state, img_screen_pos, size, ctx.current_img_ldr, ctx.base_img_ldr, ctx.base_img_raw, ctx.colormap, ctx.needs_tonemap, ctx.needs_texture);
            
            bool needs_roi_update = false;
            HandleScrollZoom(ctx.zoom_state, img_screen_pos, size, ctx.current_img_raw, ctx.base_img_raw, needs_roi_update);
            
            bool needs_pan_update = false;
            ImVec2 mouse_pos = ImGui::GetMousePos();
            bool mouse_is_over = (mouse_pos.x >= img_screen_pos.x && mouse_pos.x <= (img_screen_pos.x + size.x)) && (mouse_pos.y >= img_screen_pos.y && mouse_pos.y <= (img_screen_pos.y + size.y));    
            if (mouse_is_over) {
                HandleMousePanning(ctx.zoom_state, size, ctx.current_img_raw, ctx.base_img_raw, needs_pan_update);
            }
            if (needs_roi_update || needs_pan_update) updateViewportImage(ctx);

            RenderPixelValuesOverlay(ctx.zoom_state, img_screen_pos, size, ctx.current_img_raw);
        }
    } 
    else 
    {
        // --- Split Grid Layout Mode ---
        if (!ctx.polar_current_ldr[0].empty() && ctx.polar_textures[0])
        {
            float left_pane_w = avail.x * 0.65f - 4.0f;
            float right_pane_w = avail.x * 0.35f - 4.0f;
            
            float deg_cell_w = left_pane_w / 2.0f - 4.0f;
            float deg_cell_h = avail.y / 2.0f - 4.0f;
            float analytic_cell_h = avail.y / 2.0f - 4.0f;

            float ar = (float)ctx.polar_current_ldr[0].cols / ctx.polar_current_ldr[0].rows;
            bool shared_roi_changed = false;
            ImVec2 mouse_pos = ImGui::GetMousePos();

            auto drawGridCell = [&](int idx, ImVec2 pos, ImVec2 cell_limits) {
                ImGui::SetCursorPos(pos);
                ImGui::BeginChild((std::string("cell_") + std::to_string(idx)).c_str(), cell_limits, true, ImGuiWindowFlags_NoScrollbar);
                
                ImVec2 view_size = cell_limits;
                if (cell_limits.x / cell_limits.y > ar) view_size.x = cell_limits.y * ar; else view_size.y = cell_limits.x / ar;
                
                ImVec2 child_avail = ImGui::GetContentRegionAvail();
                ImGui::SetCursorPos(ImVec2((child_avail.x - view_size.x) * 0.5f, (child_avail.y - view_size.y) * 0.5f));
                
                ImVec2 img_screen_pos = ImGui::GetCursorScreenPos();
                ImGui::Image((void*)(intptr_t)ctx.polar_textures[idx], view_size);

                ImVec2 text_pos(img_screen_pos.x + 12, img_screen_pos.y + 12);
                ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), 24.0f, ImVec2(text_pos.x - 1, text_pos.y), IM_COL32(0, 0, 0, 255), ctx.polar_names[idx].c_str());
                ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), 24.0f, ImVec2(text_pos.x + 1, text_pos.y), IM_COL32(0, 0, 0, 255), ctx.polar_names[idx].c_str());
                ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), 24.0f, text_pos, IM_COL32(0, 255, 0, 255), ctx.polar_names[idx].c_str());

                bool mouse_over_cell = (mouse_pos.x >= img_screen_pos.x && mouse_pos.x <= (img_screen_pos.x + view_size.x)) &&
                                       (mouse_pos.y >= img_screen_pos.y && mouse_pos.y <= (img_screen_pos.y + view_size.y));

                if (mouse_over_cell) {
                    bool internal_tonemap_flag = false;
                    bool internal_texture_flag = false;
                    
                    HandleZoomAndSelection(ctx.zoom_state, img_screen_pos, view_size, ctx.polar_current_ldr[idx], ctx.polar_ldr_channels[idx], ctx.polar_raw_channels[idx], ctx.colormap, internal_tonemap_flag, internal_texture_flag);
                    
                    // 🚀 FIXED: Only compute minMaxLoc when right click is active to boost performance
                    if ((internal_tonemap_flag || ctx.colormap.is_active) && (idx != 2 && idx != 5) && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                        double min_val = 0.0, max_val = 1.0;
                        
                        // Downsample area to 256x256 max using INTER_NEAREST (unbelievably fast execution)
                        cv::Mat small_roi;
                        int target_w = std::min(256, ctx.polar_current_raw[idx].cols);
                        int target_h = std::min(256, ctx.polar_current_raw[idx].rows);
                        
                        if (target_w > 0 && target_h > 0) {
                            cv::resize(ctx.polar_current_raw[idx], small_roi, cv::Size(target_w, target_h), 0, 0, cv::INTER_NEAREST);
                            cv::minMaxLoc(small_roi, &min_val, &max_val);
                        } else {
                            cv::minMaxLoc(ctx.polar_current_raw[idx], &min_val, &max_val);
                        }
                        
                        ctx.colormap.is_active = true;
                        ctx.needs_tonemap = true;
                        shared_roi_changed = true;
                    }

                    bool cell_roi_update = false;
                    HandleScrollZoom(ctx.zoom_state, img_screen_pos, view_size, ctx.polar_current_raw[idx], ctx.polar_raw_channels[idx], cell_roi_update);
                    
                    bool cell_pan_update = false;
                    HandleMousePanning(ctx.zoom_state, view_size, ctx.polar_current_raw[idx], ctx.polar_raw_channels[idx], cell_pan_update);
                    
                    if (internal_tonemap_flag || internal_texture_flag || cell_roi_update || cell_pan_update) {
                        shared_roi_changed = true;
                    }
                }

                RenderPixelValuesOverlay(ctx.zoom_state, img_screen_pos, view_size, ctx.polar_current_raw[idx]);
                ImGui::EndChild();
            };

            drawGridCell(0, ImVec2(4.0f, 4.0f), ImVec2(deg_cell_w, deg_cell_h));                                 
            drawGridCell(1, ImVec2(4.0f + left_pane_w / 2.0f, 4.0f), ImVec2(deg_cell_w, deg_cell_h));            
            drawGridCell(3, ImVec2(4.0f, 4.0f + avail.y / 2.0f), ImVec2(deg_cell_w, deg_cell_h));                
            drawGridCell(4, ImVec2(4.0f + left_pane_w / 2.0f, 4.0f + avail.y / 2.0f), ImVec2(deg_cell_w, deg_cell_h)); 

            drawGridCell(2, ImVec2(left_pane_w + 8.0f, 4.0f), ImVec2(right_pane_w, analytic_cell_h));                
            drawGridCell(5, ImVec2(left_pane_w + 8.0f, 4.0f + avail.y / 2.0f), ImVec2(right_pane_w, analytic_cell_h)); 

            if (shared_roi_changed || ctx.needs_tonemap) {
                if (ctx.needs_tonemap) {
                    updateGlobalTonemapCache(ctx);
                    ctx.needs_tonemap = false;
                }
                updateViewportImage(ctx);
            }
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

    GLFWwindow* window = glfwCreateWindow(1280, 720, "REV Engine", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); 

    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    loadRawImage(ctx, 0);
    updateTexture(ctx);

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

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
        
        auto navigationCallback = [&](int new_idx) { loadRawImage(ctx, new_idx); };
        HandleKeyboardNavigation(ctx.current_idx, ctx.images.size(), navigationCallback);

        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("HDR Viewer", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        renderControlPanel(ctx, window);
        ImGui::Separator();

        if (ctx.needs_texture) {
            updateTexture(ctx);
            ctx.needs_texture = false;
        }

        renderViewportAndInteractions(ctx);

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
    
    if (ctx.texture) glDeleteTextures(1, &ctx.texture);
    for(auto tex : ctx.polar_textures) if(tex) glDeleteTextures(1, &tex);

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}