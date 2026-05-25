#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <opencv2/opencv.hpp>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <memory>
#include <cmath>

#include "3DViewer.h"
#include "Polarized.h"
#include "Utils.h"
#include "Tonemapper.h"
#include "MouseControls.h"
#include "KeyboardControls.h"
#include "Colormap.h"
#include "Colorspace.h"
#include "Video.h" 

#ifdef HAS_EVENTS
#include "Events.h"
#include <dv-processing/core/core.hpp>
#endif
// Variables globales de sincronización para los sliders de eventos
int g_event_count_slider = 5000;
int g_event_time_slider = 30000;
// ============================================================================
// 2. STRUCTURES AND GLOBAL STATE
// ============================================================================
struct AppContext 
{
    cv::Mat base_img_raw;    
    cv::Mat base_img_ldr;    
    cv::Mat current_img_ldr; 
    cv::Mat current_img_raw; 
    GLuint texture = 0;      

    // C++20: Inicialización directa de tamaños para limpiar el constructor
    std::vector<cv::Mat> polar_raw_channels = std::vector<cv::Mat>(6); 
    std::vector<cv::Mat> polar_ldr_channels = std::vector<cv::Mat>(6); 
    std::vector<cv::Mat> polar_current_ldr  = std::vector<cv::Mat>(6);  
    std::vector<cv::Mat> polar_current_raw  = std::vector<cv::Mat>(6);  
    std::vector<GLuint> polar_textures      = std::vector<GLuint>(6, 0);      
    std::vector<std::string> polar_names = {"0 deg", "45 deg", "AoLP", "90 deg", "135 deg", "DoLP"};

    std::vector<std::string> images;
    int current_idx = 0;
    bool is_3d_model = false;
    bool is_polarized = false; 
    bool is_video = false;
    bool is_event_file = false;
    
    VideoPlayer video;

    // OPERADORES DE MOVIMIENTO: Siempre visibles para que std::vector no rompa la compilación
    AppContext(AppContext&&) noexcept = default;
    AppContext& operator=(AppContext&&) noexcept = default;

#ifdef HAS_EVENTS
    AedatReader event_reader;
#endif

    std::shared_ptr<Viewer3D> model_viewer;
    bool needs_tonemap = false; 
    bool needs_texture = false; 

    ZoomState zoom_state;
    Colormap colormap;
    Colorspace colorspace;
    int mode = 1; 
    
    float r_gamma = 1.0f, intensity = 0.0f, light_adapt = 0.5f, color_adapt = 0.0f; 
    float d_gamma = 1.0f, d_saturation = 1.0f, d_bias = 0.85f;                     
    float m_gamma = 1.0f, m_scale = 0.7f, m_saturation = 1.0f;                     

    // Constructor por defecto limpio gracias a las inicializaciones de arriba
    AppContext() = default;

    void resetToFactoryDefaults() {
        r_gamma = 1.0f; intensity = 0.0f; light_adapt = 0.5f; color_adapt = 0.0f;
        d_gamma = 1.0f; d_saturation = 1.0f; d_bias = 0.85f;
        m_gamma = 1.0f; m_scale = 0.7f; m_saturation = 1.0f;
        mode = 1; 
        is_3d_model = false;
        is_polarized = false; 
        is_video = false;
        is_event_file = false;
        video.release();

#ifdef HAS_EVENTS
        event_reader.Close();
#endif

        model_viewer.reset();
        colorspace.reset();
    }
};

std::vector<AppContext> contexts;


struct CrossSyncState {
    bool is_dragging = false;
    bool has_persistent_rect = false; 
    int active_context_idx = -1; 
    int active_viewport_id = -1;

    cv::Point2f img_start_pixel = cv::Point2f(-1, -1);
    cv::Point2f img_end_pixel = cv::Point2f(-1, -1);
    ImVec2 normalized_cursor_pos = ImVec2(-1, -1); 

    void reset() {
        is_dragging = false;
        has_persistent_rect = false;
        img_start_pixel = cv::Point2f(-1, -1);
        img_end_pixel = cv::Point2f(-1, -1);
        normalized_cursor_pos = ImVec2(-1, -1);
        active_context_idx = -1;
        active_viewport_id = -1;
    }
} g_sync;

bool is3DModelFile(const std::string& ext)
{
    return ext == ".obj" || ext == ".ply" || ext == ".pcd" || ext == ".stl";
}


// ============================================================================
// 3. PIPELINE FUNCTIONS
// ============================================================================
void updateGlobalTonemapCache(AppContext& ctx)
{
    if (ctx.is_3d_model || (ctx.base_img_raw.empty() && (!ctx.is_polarized || ctx.polar_raw_channels[0].empty()))) return;

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
    if (ctx.is_3d_model) return;

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
    if (ctx.is_3d_model) return;

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
    if (ctx.images.empty() || i >= ctx.images.size()) return;
    std::string file_path = ctx.images[i];
    std::string ext = std::filesystem::path(file_path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    ctx.is_3d_model = is3DModelFile(ext);
    ctx.is_polarized = (ext == ".raw" || ext == ".bin" || ext == ".dat");
    ctx.is_video = (ext == ".mp4" || ext == ".avi" || ext == ".mkv" || ext == ".mov");

    if (ctx.is_3d_model) 
    {
        ctx.is_polarized = false;
        ctx.is_video = false;
        ctx.base_img_raw.release();
        ctx.base_img_ldr.release();
        ctx.current_img_raw.release();
        ctx.current_img_ldr.release();

        if (ctx.texture) {
            glDeleteTextures(1, &ctx.texture);
            ctx.texture = 0;
        }

        for (auto& tex : ctx.polar_textures) {
            if (tex) {
                glDeleteTextures(1, &tex);
                tex = 0;
            }
        }

        ctx.model_viewer = ctx.model_viewer ? ctx.model_viewer : std::make_shared<Viewer3D>();
        if (ctx.model_viewer->LoadModel(file_path)) {
            ctx.model_viewer->ResetCamera();
        }
        ctx.needs_tonemap = false;
        ctx.needs_texture = false;
        return;
    }

    ctx.model_viewer.reset();

    if (ctx.is_video) 
    {
        if (ctx.video.loadVideo(file_path)) {
            cv::Mat frame;
            if (ctx.video.seekTo(0.0f, frame)) {
                frame.convertTo(ctx.base_img_raw, CV_32FC3, 1.0 / 255.0);
                ctx.zoom_state.Reset(ctx.base_img_raw.cols, ctx.base_img_raw.rows);
            }
        }
    }
    else if (ctx.is_polarized) 
    {
        cv::Mat mosaic = dePolarize(file_path); 
        if (!mosaic.empty()) {
            mosaic.convertTo(mosaic, CV_32FC3, 1.0 / 255.0);
            
            int w = mosaic.cols / 2;
            int h = mosaic.rows / 2;

            ctx.polar_raw_channels[0] = mosaic(cv::Rect(0, 0, w, h)).clone(); 
            ctx.polar_raw_channels[1] = mosaic(cv::Rect(w, 0, w, h)).clone(); 
            ctx.polar_raw_channels[3] = mosaic(cv::Rect(0, h, w, h)).clone(); 
            ctx.polar_raw_channels[4] = mosaic(cv::Rect(w, h, w, h)).clone(); 

            std::vector<cv::Mat> bgr_channels = { ctx.polar_raw_channels[0], ctx.polar_raw_channels[1], ctx.polar_raw_channels[3], ctx.polar_raw_channels[4] };
            PolarizationResult polar = computePolarization(bgr_channels);
            
            cv::Mat aolp_norm = (polar.AoLP + (M_PI / 2.0f)) / M_PI; 
            cv::Mat aolp_8u, aolp_rgb;
            aolp_norm.convertTo(aolp_8u, CV_8UC1, 255.0);
            cv::applyColorMap(aolp_8u, aolp_rgb, cv::COLORMAP_HSV);
            aolp_rgb.convertTo(ctx.polar_raw_channels[2], CV_32FC3, 1.0 / 255.0);
            cv::resize(ctx.polar_raw_channels[2], ctx.polar_raw_channels[2], cv::Size(w, h));

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
// 4. RENDERING SUB-ROUTINES (CONTROL PANEL & WORKSPACE)
// ============================================================================

void renderControlPanel(GLFWwindow* window)
{
    if (contexts.empty()) return;
    
    AppContext& ref_ctx = contexts[0];

    static int event_count_slider = 5000;
    static int event_time_slider = 30000;

    // ============================================================================
    // camera events file viewer (Solo por Ventana de Tiempo)
    // ============================================================================
    if (ref_ctx.is_event_file) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "ACUMULADOR DE EVENTOS (.aedat4)");
        ImGui::Separator();
        ImGui::Spacing();

        for (auto& c : contexts) {
            c.mode = 1; 
        }

        bool slider_changed = false; 

        if (ImGui::SliderInt("Tie (us)", &g_event_time_slider, 100, 100000, "%d us")) {
            slider_changed = true;
        }
        ImGui::TextDisabled("Temporal Window: Accumulates real microseconds.");

        if (slider_changed) {
            for (auto& c : contexts) {
                c.needs_tonemap = true;
                c.needs_texture = true;
            }
        }

        ImGui::Separator();
        ImGui::Spacing();
        
        if (ImGui::Button("Reset Zoom") || glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS) {
            for(auto& c : contexts) {
                c.zoom_state.Reset(c.base_img_raw.cols, c.base_img_raw.rows);
                updateViewportImage(c);
            }
            g_sync.has_persistent_rect = false; 
        }
        
        return; 
    }

    // ============================================================================
    // CASO 2: VISOR DE MODELOS 3D
    // ============================================================================
    if (ref_ctx.is_3d_model) {
        ImGui::Text("3D Model Viewer");
        if (ref_ctx.model_viewer && ImGui::Button("Reset Camera")) {
            for (auto& c : contexts) {
                if (c.is_3d_model && c.model_viewer) {
                    c.model_viewer->ResetCamera();
                }
            }
        }
        return;
    }

    // ============================================================================
    // CASO 3: INTERFAZ ESTÁNDAR (IMÁGENES, VIDEOS, RASTER, TONEMAPPING)
    // ============================================================================
    ImGui::Text("Tonemapping");
    if (ImGui::Button("Reinhard")) { for(auto& c : contexts) { c.mode = 1; c.needs_tonemap = true; } } ImGui::SameLine();
    if (ImGui::Button("Drago"))    { for(auto& c : contexts) { c.mode = 2; c.needs_tonemap = true; } } ImGui::SameLine();
    if (ImGui::Button("Mantiuk"))  { for(auto& c : contexts) { c.mode = 3; c.needs_tonemap = true; } }

    ImGui::Separator();

    bool changed = false;
    if (ref_ctx.mode == 1) {
        if (ImGui::SliderFloat("Gamma", &ref_ctx.r_gamma, 0.1f, 3.0f))           changed = true;
        if (ImGui::SliderFloat("Intensity", &ref_ctx.intensity, -8.0f, 8.0f))    changed = true;
        if (ImGui::SliderFloat("Light Adapt", &ref_ctx.light_adapt, 0.0f, 1.0f))  changed = true;
        if (ImGui::SliderFloat("Color Adapt", &ref_ctx.color_adapt, 0.0f, 1.0f))  changed = true;
    } else if (ref_ctx.mode == 2) {
        if (ImGui::SliderFloat("Gamma", &ref_ctx.d_gamma, 0.1f, 3.0f))           changed = true;
        if (ImGui::SliderFloat("Saturation", &ref_ctx.d_saturation, 0.0f, 2.0f)) changed = true;
        if (ImGui::SliderFloat("Bias", &ref_ctx.d_bias, 0.6f, 0.95f))            changed = true;
    } else if (ref_ctx.mode == 3) {
        if (ImGui::SliderFloat("Gamma", &ref_ctx.m_gamma, 0.1f, 3.0f))           changed = true;
        if (ImGui::SliderFloat("Scale", &ref_ctx.m_scale, 0.1f, 2.0f))           changed = true;
        if (ImGui::SliderFloat("Saturation", &ref_ctx.m_saturation, 0.0f, 2.0f)) changed = true;
    }

    if (changed) {
        for(auto& c : contexts) {
            c.r_gamma = ref_ctx.r_gamma; c.intensity = ref_ctx.intensity; c.light_adapt = ref_ctx.light_adapt; c.color_adapt = ref_ctx.color_adapt;
            c.d_gamma = ref_ctx.d_gamma; c.d_saturation = ref_ctx.d_saturation; c.d_bias = ref_ctx.d_bias;
            c.m_gamma = ref_ctx.m_gamma; c.m_scale = ref_ctx.m_scale; c.m_saturation = ref_ctx.m_saturation;
            c.needs_tonemap = true;
        }
    }

    if (ref_ctx.is_video && ref_ctx.video.isLoaded()) {
        if (ImGui::Button(ref_ctx.video.isPlaying() ? "Pause" : "Play") || ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            bool new_state = !ref_ctx.video.isPlaying();
            for (auto& c : contexts) if (c.is_video) c.video.setPlaying(new_state);
        }
        ImGui::SameLine();
        
        float progress = ref_ctx.video.getProgress();
        
        if (ImGui::SliderFloat("Progress", &progress, 0.0f, 1.0f, "%.3f")) {
            for (auto& c : contexts) {
                if (c.is_video && c.video.isLoaded()) {
                    cv::Mat frame;
                    if (c.video.seekTo(progress, frame)) {
                        frame.convertTo(c.base_img_raw, CV_32FC3, 1.0 / 255.0);
                        c.needs_tonemap = true;
                    }
                }
            }
        }
        ImGui::Separator();
    }

    if (ImGui::Button("Reset Zoom") || glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS) {
        for(auto& c : contexts) {
            int w = c.is_polarized ? c.polar_raw_channels[0].cols : c.base_img_raw.cols;
            int h = c.is_polarized ? c.polar_raw_channels[0].rows : c.base_img_raw.rows;
            c.zoom_state.Reset(w, h);
            updateViewportImage(c);
        }
        g_sync.has_persistent_rect = false; 
    }

    if (ref_ctx.colormap.is_active) {
        ImGui::SameLine();
        if (ImGui::Button("Reset Range") || glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
            for(auto& c : contexts) {
                c.colormap.reset();
                c.needs_tonemap = true;
            }
            g_sync.has_persistent_rect = false; 
        }
    }
}

void drawSingleViewport(int context_idx, AppContext& ctx, ImVec2 size, int viewport_id, cv::Mat& current_raw, cv::Mat& base_raw, cv::Mat& current_ldr, cv::Mat& base_ldr, GLuint texture_id)
{
    if (current_ldr.empty() || !texture_id) return;

    float ar = (float)current_ldr.cols / current_ldr.rows;
    ImVec2 view_size = size;
    if (size.x / size.y > ar) view_size.x = size.y * ar; else view_size.y = size.x / ar;

    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::SetCursorPos(ImVec2((avail.x - view_size.x) * 0.5f, (avail.y - view_size.y) * 0.5f));
    ImVec2 img_screen_pos = ImGui::GetCursorScreenPos();
    
    ImGui::Image((void*)(intptr_t)texture_id, view_size);

    ImVec2 mouse_pos = ImGui::GetMousePos();
    bool mouse_is_over = (mouse_pos.x >= img_screen_pos.x && mouse_pos.x <= (img_screen_pos.x + view_size.x)) && 
                         (mouse_pos.y >= img_screen_pos.y && mouse_pos.y <= (img_screen_pos.y + view_size.y));

    bool over_buttons = ImGui::IsAnyItemHovered();

    // Calculate floating buttons position
    bool mouse_is_over_floating_buttons = false;
    ImVec2 dynamic_btn_pos(0, 0);
    ImVec2 btn_box_size(145.0f, 25.0f); 

    if (g_sync.has_persistent_rect && g_sync.img_start_pixel.x >= 0.0f) {
        cv::Rect roi = ctx.zoom_state.current_roi;
        float norm_p1_x = (g_sync.img_start_pixel.x - roi.x) / (float)roi.width;
        float norm_p1_y = (g_sync.img_start_pixel.y - roi.y) / (float)roi.height;
        float norm_p2_x = (g_sync.img_end_pixel.x - roi.x) / (float)roi.width;
        float norm_p2_y = (g_sync.img_end_pixel.y - roi.y) / (float)roi.height;
        
        ImVec2 p1(img_screen_pos.x + norm_p1_x * view_size.x, img_screen_pos.y + norm_p1_y * view_size.y);
        ImVec2 p2(img_screen_pos.x + norm_p2_x * view_size.x, img_screen_pos.y + norm_p2_y * view_size.y);
        
        float rect_right = (p1.x > p2.x) ? p1.x : p2.x;
        float rect_bottom = (p1.y > p2.y) ? p1.y : p2.y;

        dynamic_btn_pos = ImVec2(rect_right - btn_box_size.x, rect_bottom + 4.0f); 
        if (dynamic_btn_pos.x < img_screen_pos.x) dynamic_btn_pos.x = img_screen_pos.x;
        if (dynamic_btn_pos.y > (img_screen_pos.y + view_size.y - btn_box_size.y)) dynamic_btn_pos.y = img_screen_pos.y + view_size.y - btn_box_size.y;

        if (mouse_pos.x >= dynamic_btn_pos.x && mouse_pos.x <= (dynamic_btn_pos.x + btn_box_size.x) &&
            mouse_pos.y >= dynamic_btn_pos.y && mouse_pos.y <= (dynamic_btn_pos.y + btn_box_size.y)) {
            mouse_is_over_floating_buttons = true;
        }
    }

    // Reset selection
    if (mouse_is_over && !over_buttons && !mouse_is_over_floating_buttons) {
        g_sync.normalized_cursor_pos = ImVec2((mouse_pos.x - img_screen_pos.x) / view_size.x, (mouse_pos.y - img_screen_pos.y) / view_size.y);
        
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            g_sync.reset();
            for(auto& c : contexts) {
                c.zoom_state.is_selecting = false;
                c.needs_tonemap = true;
                updateViewportImage(c);
            }
        }
    }

    // Right click interactions
    if (mouse_is_over && !over_buttons && !mouse_is_over_floating_buttons && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        g_sync.is_dragging = true;
        g_sync.has_persistent_rect = false; 
        g_sync.active_context_idx = context_idx;
        g_sync.active_viewport_id = viewport_id;
        
        for(auto& c : contexts) c.zoom_state.is_selecting = false;
        
        float norm_x = (mouse_pos.x - img_screen_pos.x) / view_size.x;
        float norm_y = (mouse_pos.y - img_screen_pos.y) / view_size.y;
        
        cv::Rect roi = ctx.zoom_state.current_roi;
        g_sync.img_start_pixel = cv::Point2f(roi.x + norm_x * roi.width, roi.y + norm_y * roi.height);
        g_sync.img_end_pixel = g_sync.img_start_pixel;

        for(auto& c : contexts) { c.needs_tonemap = true; updateViewportImage(c); }
    }

    // Dragging updates
    if (g_sync.is_dragging && g_sync.active_context_idx == context_idx && g_sync.active_viewport_id == viewport_id) {
        float norm_x = (mouse_pos.x - img_screen_pos.x) / view_size.x;
        float norm_y = (mouse_pos.y - img_screen_pos.y) / view_size.y;
        
        cv::Rect roi = ctx.zoom_state.current_roi;
        g_sync.img_end_pixel = cv::Point2f(roi.x + norm_x * roi.width, roi.y + norm_y * roi.height);
        
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            g_sync.is_dragging = false;
            g_sync.has_persistent_rect = true; 
            for(auto& c : contexts) { c.needs_tonemap = true; updateViewportImage(c); }
        }
    }

    // Zoom and panning calculations
    bool original_selecting = ctx.zoom_state.is_selecting;
    ctx.zoom_state.is_selecting = false;

    bool internal_tonemap = false;
    bool internal_texture = false;
    HandleZoomAndSelection(ctx.zoom_state, img_screen_pos, view_size, current_ldr, base_ldr, base_raw, ctx.colormap, internal_tonemap, internal_texture);
    ctx.zoom_state.is_selecting = original_selecting;

    bool needs_roi_update = false;
    HandleScrollZoom(ctx.zoom_state, img_screen_pos, view_size, current_raw, base_raw, needs_roi_update);
    
    bool needs_pan_update = false;
    if (mouse_is_over && !over_buttons && !mouse_is_over_floating_buttons) {
        HandleMousePanning(ctx.zoom_state, view_size, current_raw, base_raw, needs_pan_update);
    }

    // Propagate zoom updates to sync views
    if (needs_roi_update || needs_pan_update || internal_texture || internal_tonemap) {
        if (internal_tonemap) ctx.needs_tonemap = true;
        updateViewportImage(ctx);
        
        for (size_t i = 0; i < contexts.size(); ++i) {
            if ((int)i != context_idx) {
                contexts[i].zoom_state.current_roi = ctx.zoom_state.current_roi;
                if (internal_tonemap) { 
                    contexts[i].colormap = ctx.colormap; 
                    contexts[i].needs_tonemap = true; 
                }
                updateViewportImage(contexts[i]);
            }
        }
    }
    
    // Render green rectangle and metrics text overlay
    if ((g_sync.is_dragging || g_sync.has_persistent_rect) && g_sync.img_start_pixel.x >= 0.0f) 
    {
        cv::Rect roi = ctx.zoom_state.current_roi;
        
        float norm_p1_x = (g_sync.img_start_pixel.x - roi.x) / (float)roi.width;
        float norm_p1_y = (g_sync.img_start_pixel.y - roi.y) / (float)roi.height;
        float norm_p2_x = (g_sync.img_end_pixel.x - roi.x) / (float)roi.width;
        float norm_p2_y = (g_sync.img_end_pixel.y - roi.y) / (float)roi.height;
        
        ImVec2 p1(img_screen_pos.x + norm_p1_x * view_size.x, img_screen_pos.y + norm_p1_y * view_size.y);
        ImVec2 p2(img_screen_pos.x + norm_p2_x * view_size.x, img_screen_pos.y + norm_p2_y * view_size.y);
        
        ImGui::GetWindowDrawList()->PushClipRect(img_screen_pos, ImVec2(img_screen_pos.x + view_size.x, img_screen_pos.y + view_size.y), true);
        ImGui::GetWindowDrawList()->AddRect(p1, p2, IM_COL32(0, 255, 0, 255), 0.0f, 0, 2.0f);
        ImGui::GetWindowDrawList()->PopClipRect();

        // Bottom-left text overlay (X, Y, W, H)
        int start_x = (int)g_sync.img_start_pixel.x;
        int start_y = (int)g_sync.img_start_pixel.y;
        int end_x   = (int)std::abs(g_sync.img_end_pixel.x) ;
        int end_y  = (int)std::abs(g_sync.img_end_pixel.y);

        char overlay_text[128];
        snprintf(overlay_text, sizeof(overlay_text), "X1: %d  Y2: %d\nX2: %d  Y2: %d", start_x, start_y, end_x, end_y);

        ImVec2 text_size = ImGui::CalcTextSize(overlay_text);
        // Position offset calculated from bottom border
        ImVec2 text_pos = ImVec2(img_screen_pos.x + 10.0f, img_screen_pos.y + view_size.y - text_size.y - 10.0f);
        ImVec2 bg_p1 = ImVec2(text_pos.x - 5.0f, text_pos.y - 5.0f);
        ImVec2 bg_p2 = ImVec2(text_pos.x + text_size.x + 5.0f, text_pos.y + text_size.y + 5.0f);

        ImGui::GetWindowDrawList()->PushClipRect(img_screen_pos, ImVec2(img_screen_pos.x + view_size.x, img_screen_pos.y + view_size.y), true);
        ImGui::GetWindowDrawList()->AddRectFilled(bg_p1, bg_p2, IM_COL32(0, 0, 0, 180), 4.0f);
        ImGui::GetWindowDrawList()->AddText(text_pos, IM_COL32(0, 255, 0, 255), overlay_text);
        ImGui::GetWindowDrawList()->PopClipRect();

        if (g_sync.has_persistent_rect && g_sync.active_context_idx == context_idx && g_sync.active_viewport_id == viewport_id) 
        {
            ImGui::SetCursorScreenPos(dynamic_btn_pos);
            ImGui::PushID(context_idx * 100 + viewport_id); 
            
            // Global Zoom
            if (ImGui::Button("Zoom")) {
                float x1 = std::min(g_sync.img_start_pixel.x, g_sync.img_end_pixel.x);
                float y1 = std::min(g_sync.img_start_pixel.y, g_sync.img_end_pixel.y);
                float x2 = std::max(g_sync.img_start_pixel.x, g_sync.img_end_pixel.x);
                float y2 = std::max(g_sync.img_start_pixel.y, g_sync.img_end_pixel.y);

                int roi_x = std::max(0, (int)x1);
                int roi_y = std::max(0, (int)y1);
                int roi_w = std::min(base_raw.cols - roi_x, (int)(x2 - x1));
                int roi_h = std::min(base_raw.rows - roi_y, (int)(y2 - y1));

                if (roi_w > 5 && roi_h > 5) {
                    for(auto& c : contexts) {
                        c.zoom_state.current_roi = cv::Rect(roi_x, roi_y, roi_w, roi_h);
                        c.needs_tonemap = true;
                        updateViewportImage(c);
                    }
                    g_sync.reset();
                }
            }
            
            ImGui::SameLine();
            // Global AutoRange
            if (ImGui::Button("AutoRange")) {
                float x1 = std::min(g_sync.img_start_pixel.x, g_sync.img_end_pixel.x);
                float y1 = std::min(g_sync.img_start_pixel.y, g_sync.img_end_pixel.y);
                float x2 = std::max(g_sync.img_start_pixel.x, g_sync.img_end_pixel.x);
                float y2 = std::max(g_sync.img_start_pixel.y, g_sync.img_end_pixel.y);

                int roi_x = std::max(0, (int)x1);
                int roi_y = std::max(0, (int)y1);
                int roi_w = std::min(base_raw.cols - roi_x, (int)(x2 - x1));
                int roi_h = std::min(base_raw.rows - roi_y, (int)(y2 - y1));

                if (roi_w > 5 && roi_h > 5) {
                    cv::Rect selection(roi_x, roi_y, roi_w, roi_h);
                    
                    cv::Mat roi_mat = base_raw(selection);
                    std::vector<cv::Mat> channels;
                    cv::split(roi_mat, channels);
                    
                    double global_min = std::numeric_limits<double>::max();
                    double global_max = std::numeric_limits<double>::lowest();
                    
                    for(const auto& ch : channels) {
                        double ch_min, ch_max;
                        cv::minMaxLoc(ch, &ch_min, &ch_max);
                        if(ch_min < global_min) global_min = ch_min;
                        if(ch_max > global_max) global_max = ch_max;
                    }
                    
                    for(auto& c : contexts) {
                        c.colormap.autoCenterAndRadius((float)global_min, (float)global_max);
                        c.needs_tonemap = true;
                        updateViewportImage(c);
                    }
                    g_sync.reset();
                }
            }
            ImGui::PopID();
        }
    }

    if (g_sync.normalized_cursor_pos.x >= 0.0f && g_sync.normalized_cursor_pos.y >= 0.0f && !over_buttons && !mouse_is_over_floating_buttons) {
        ImVec2 target_pixel_pos(img_screen_pos.x + g_sync.normalized_cursor_pos.x * view_size.x, img_screen_pos.y + g_sync.normalized_cursor_pos.y * view_size.y);
        
        float box_size = 4.0f; 
        ImVec2 p1(target_pixel_pos.x - box_size, target_pixel_pos.y - box_size);
        ImVec2 p2(target_pixel_pos.x + box_size, target_pixel_pos.y + box_size);
        
        ImGui::GetWindowDrawList()->PushClipRect(img_screen_pos, ImVec2(img_screen_pos.x + view_size.x, img_screen_pos.y + view_size.y), true);
        ImGui::GetWindowDrawList()->AddRect(p1, p2, IM_COL32(0, 255, 0, 255), 0.0f, 0, 1.5f);
        ImGui::GetWindowDrawList()->PopClipRect();
    }

    RenderPixelValuesOverlay(ctx.zoom_state, img_screen_pos, view_size, current_ldr, current_raw, g_sync.normalized_cursor_pos);
}

void renderViewportAndInteractions()
{
    ImGui::BeginChild("workspace", ImVec2(0, 0), true);
    ImVec2 avail = ImGui::GetContentRegionAvail();

    if (!contexts.empty()) 
    {
        // 1. GRID DINÁMICO: Calcular columnas y filas óptimas
        int n = contexts.size();
        int cols = std::ceil(std::sqrt(n));
        int rows = std::ceil((float)n / cols);

        float cell_w = (avail.x / cols) - 8.0f;
        float cell_h = (avail.y / rows) - 8.0f;

        // 2. Iterar sobre todos los contextos para dibujarlos
        for (int i = 0; i < n; ++i) 
        {
            if (contexts[i].needs_tonemap) { 
                updateGlobalTonemapCache(contexts[i]); 
                contexts[i].needs_tonemap = false; 
                updateViewportImage(contexts[i]); 
            }

            ImGui::PushID(i);
            ImGui::BeginChild("viewport_pane", ImVec2(cell_w, cell_h), true);

            if (contexts[i].is_3d_model) {
                if (contexts[i].model_viewer) {
                    contexts[i].model_viewer->RenderView(ImGui::GetContentRegionAvail());
                } else {
                    ImGui::TextUnformatted("No 3D model loaded");
                }

                ImGui::EndChild();
                ImGui::PopID();

                if ((i + 1) % cols != 0 && (i + 1) < n) {
                    ImGui::SameLine();
                }
                continue;
            }
            
            if (!contexts[i].is_polarized) 
            {
                // Imagen Normal
                drawSingleViewport(i, contexts[i], ImVec2(cell_w, cell_h), 0, 
                                   contexts[i].current_img_raw, contexts[i].base_img_raw, 
                                   contexts[i].current_img_ldr, contexts[i].base_img_ldr, 
                                   contexts[i].texture);
            } 
            else 
            {
                // Imagen Polarizada (Grid Interno)
                if (!contexts[i].polar_current_ldr[0].empty() && contexts[i].polar_textures[0])
                {
                    float left_pane_w = cell_w * 0.65f - 4.0f;
                    float right_pane_w = cell_w * 0.35f - 4.0f;
                    float deg_cell_w = left_pane_w / 2.0f - 4.0f;
                    float deg_cell_h = cell_h / 2.0f - 4.0f;
                    float analytic_cell_h = cell_h / 2.0f - 4.0f;

                    auto drawGridCell = [&](int idx, ImVec2 pos, ImVec2 cell_limits) {
                        ImGui::SetCursorPos(pos);
                        ImGui::BeginChild((std::string("cell_") + std::to_string(idx)).c_str(), cell_limits, true, ImGuiWindowFlags_NoScrollbar);
                        
                        drawSingleViewport(i, contexts[i], cell_limits, idx, 
                                           contexts[i].polar_current_raw[idx], contexts[i].polar_raw_channels[idx], 
                                           contexts[i].polar_current_ldr[idx], contexts[i].polar_ldr_channels[idx], 
                                           contexts[i].polar_textures[idx]);

                        ImVec2 img_screen_pos = ImGui::GetCursorScreenPos();
                        ImVec2 text_pos(img_screen_pos.x + 12, img_screen_pos.y - cell_limits.y + 12); 
                        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), 24.0f, text_pos, IM_COL32(0, 255, 0, 255), contexts[i].polar_names[idx].c_str());

                        ImGui::EndChild();
                    };

                    drawGridCell(0, ImVec2(4.0f, 4.0f), ImVec2(deg_cell_w, deg_cell_h));                                 
                    drawGridCell(1, ImVec2(4.0f + left_pane_w / 2.0f, 4.0f), ImVec2(deg_cell_w, deg_cell_h));            
                    drawGridCell(3, ImVec2(4.0f, 4.0f + cell_h / 2.0f), ImVec2(deg_cell_w, deg_cell_h));                
                    drawGridCell(4, ImVec2(4.0f + left_pane_w / 2.0f, 4.0f + cell_h / 2.0f), ImVec2(deg_cell_w, deg_cell_h)); 
                    drawGridCell(2, ImVec2(left_pane_w + 8.0f, 4.0f), ImVec2(right_pane_w, analytic_cell_h));                
                    drawGridCell(5, ImVec2(left_pane_w + 8.0f, 4.0f + cell_h / 2.0f), ImVec2(right_pane_w, analytic_cell_h)); 
                }
            }

            ImGui::EndChild();
            ImGui::PopID();

            // Salto de línea dinámico para cuadrar las columnas
            if ((i + 1) % cols != 0 && (i + 1) < n) {
                ImGui::SameLine();
            }
        }
    }
    ImGui::EndChild();
}

// ============================================================================
// 5. MAIN ENTRY POINT
// ============================================================================
void updateContextType(AppContext& c, const std::string& file_path) 
{
#ifdef HAS_EVENTS
    if (c.is_event_file) {
        c.event_reader.Close();
    }
#endif
    c.is_event_file = false;
    c.is_video = false;
    c.is_3d_model = false;

    std::string ext = std::filesystem::path(file_path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".aedat" || ext == ".aedat4") {
#ifdef HAS_EVENTS
        c.is_event_file = true;
        c.event_reader.Open(file_path);
        c.mode = 1;
#else
        std::cout << "[WARNING] Soporte de eventos deshabilitado para: " << file_path << "\n";
#endif
    }
    else if (ext == ".mp4" || ext == ".avi" || ext == ".mkv" || ext == ".mov") {
        c.is_video = true;
    }
    else if (ext == ".obj" || ext == ".ply" || ext == ".pcd") {
        c.is_3d_model = true;
        if (!c.model_viewer) {
            using UnderlyingType = std::remove_reference_t<decltype(*c.model_viewer)>;
            c.model_viewer = std::make_unique<UnderlyingType>();
        }
    }
}

void processInputArguments(int argc, char** argv, AppContext& ctx) 
{
    for (int i = 1; i < argc; ++i) {
        std::string input = argv[i];
        if (std::filesystem::is_directory(input)) {
            std::vector<std::string> dir_files = getImages(input);
            ctx.images.insert(ctx.images.end(), dir_files.begin(), dir_files.end());
        } else {
            ctx.images.push_back(input);
        }
    }
}

GLFWwindow* initGraphicsWindow(int width, int height, const char* title) 
{
#ifdef __APPLE__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!window) return nullptr;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    return window;
}

void initImGui(GLFWwindow* window) 
{
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void loadInitialContextData() 
{
    for (auto& c : contexts) {
        if (!c.is_event_file && !c.is_3d_model) {
            loadRawImage(c, 0);
            updateTexture(c);
        } 
#ifdef HAS_EVENTS
        else if (c.is_event_file && c.event_reader.IsOpen()) {
            cv::Mat first_ev_frame;
            if (c.event_reader.ReadNextFrame(first_ev_frame, 1, g_event_time_slider)) {
                first_ev_frame.convertTo(c.base_img_raw, CV_32FC3, 1.0 / 255.0);
                c.needs_tonemap = true;
                c.needs_texture = true;
            }
        }
#endif
        if (c.is_3d_model && c.model_viewer) {
            c.model_viewer->LoadModel(c.images[0]);
        }
    }
}

// ============================================================================
// PROCESAMIENTO CONTINUO DE CONTENIDOS (FRAME A FRAME)
// ============================================================================

void processContinuousMedia() 
{
    for (auto& c : contexts) {
        if (c.is_video) {
            cv::Mat frame;
            if (c.video.getNextFrame(frame)) {
                frame.convertTo(c.base_img_raw, CV_32FC3, 1.0 / 255.0);
                c.needs_tonemap = true;
            }
        } 
#ifdef HAS_EVENTS
        else if (c.is_event_file && c.event_reader.IsOpen()) {
            cv::Mat ev_frame;
            if (!c.event_reader.ReadNextFrame(ev_frame, 1, g_event_time_slider)) {
                c.event_reader.Close();
                c.event_reader.Open(c.images[c.current_idx]);
                if (c.event_reader.ReadNextFrame(ev_frame, 1, g_event_time_slider)) {
                    ev_frame.convertTo(c.base_img_raw, CV_32FC3, 1.0 / 255.0);
                    c.needs_tonemap = true;
                    c.needs_texture = true;
                }
            } else {
                ev_frame.convertTo(c.base_img_raw, CV_32FC3, 1.0 / 255.0);
                c.needs_tonemap = true;
                c.needs_texture = true; 
            }
        }
#endif
    }
}

// ============================================================================
// MANEJO DE ENTRADAS DEL USUARIO (TECLADO / INTERFAZ)
// ============================================================================

void handleKeyboardInputs(GLFWwindow* window) 
{
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        for (auto& c : contexts) {
            c.resetToFactoryDefaults();
            if (!c.is_event_file && !c.is_3d_model) {
                loadRawImage(c, c.current_idx);
            }
            if (c.is_3d_model && c.model_viewer) {
                c.model_viewer->ResetCamera();
            }
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_S)) {
        for (auto& c : contexts) {
            if (c.is_3d_model || c.is_event_file) continue;
            c.colorspace.cycle();
            updateGlobalTonemapCache(c);
            updateViewportImage(c);
        }
    }

    auto navigationCallback = [](int new_idx) {
        for (auto& c : contexts) {
            c.current_idx = new_idx;
            updateContextType(c, c.images[new_idx]);
            
            if (!c.is_event_file && !c.is_3d_model) {
                loadRawImage(c, new_idx);
            }
            else if (c.is_3d_model && c.model_viewer) {
                c.model_viewer->LoadModel(c.images[new_idx]);
                c.model_viewer->ResetCamera();
            }
#ifdef HAS_EVENTS
            else if (c.is_event_file && c.event_reader.IsOpen()) {
                cv::Mat first_ev_frame;
                if (c.event_reader.ReadNextFrame(first_ev_frame, 1, g_event_time_slider)) {
                    first_ev_frame.convertTo(c.base_img_raw, CV_32FC3, 1.0 / 255.0);
                    c.needs_tonemap = true;
                    c.needs_texture = true;
                }
            }
#endif
        }
    };

    if (!contexts.empty()) {
        HandleKeyboardNavigation(
            contexts[0].current_idx,
            contexts[0].images.size(),
            navigationCallback
        );
    }
}

void renderImGuiInterface(GLFWwindow* window) 
{
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

    ImGui::Begin(
        "HDR Viewer",
        nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
    );

    renderControlPanel(window);
    ImGui::Separator();

    for (auto& c : contexts) {
        if (c.needs_texture) {
            updateTexture(c);
            c.needs_texture = false;
        }
    }

    renderViewportAndInteractions();

    if (!contexts.empty() && !contexts[0].images.empty()) {
        const std::string& current_path = contexts[0].images[contexts[0].current_idx];
        float bar_h = 28.0f;

        ImDrawList* draw = ImGui::GetForegroundDrawList();
        ImVec2 p0 = ImGui::GetWindowPos();
        ImVec2 p1(p0.x + ImGui::GetWindowWidth(), p0.y + bar_h);

        draw->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 230));
        draw->AddText(ImVec2(p0.x + 10, p0.y + 6), IM_COL32(0, 255, 120, 255), current_path.c_str());
    }

    ImGui::End();
}

void cleanupResources() 
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    for (auto& c : contexts) {
        if (c.texture) glDeleteTextures(1, &c.texture);
        for (auto tex : c.polar_textures) {
            if (tex) glDeleteTextures(1, &tex);
        }
#ifdef HAS_EVENTS
        if (c.is_event_file) c.event_reader.Close();
#endif
    }
    glfwTerminate();
}

// ============================================================================
// 5. MAIN ENTRY POINT
// ============================================================================
int main(int argc, char** argv)
{
    if (!glfwInit()) return -1;

    if (argc < 2) {
        std::cout << "Usage: ./bin/riv <image1> [image2] [image3] ...\n";
        return -1;
    }

    AppContext ctx;
    processInputArguments(argc, argv, ctx);

    if (ctx.images.empty()) return -1;
    ctx.current_idx = 0;

    updateContextType(ctx, ctx.images[ctx.current_idx]);
    contexts.push_back(std::move(ctx));

    GLFWwindow* window = initGraphicsWindow(1280, 720, "Research Image Viewer");
    if (!window) {
        glfwTerminate();
        return -1;
    }

    initImGui(window);
    loadInitialContextData();

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        handleKeyboardInputs(window);
        processContinuousMedia();
        renderImGuiInterface(window);

        ImGui::Render();

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    cleanupResources();
    return 0;
}