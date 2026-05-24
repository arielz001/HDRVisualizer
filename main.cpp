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
// 2. STRUCTURES AND GLOBAL STATE
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

bool is_dual_mode = false;
AppContext ctxA;
AppContext ctxB;

struct CrossSyncState {
    bool is_dragging = false;
    bool has_persistent_rect = false; 
    int active_source_idx = -1; 

    // Guardamos la posición en píxeles absolutos dentro de la imagen original (base_raw)
    cv::Point2f img_start_pixel = cv::Point2f(-1, -1);
    cv::Point2f img_end_pixel = cv::Point2f(-1, -1);
    
    ImVec2 normalized_cursor_pos = ImVec2(-1, -1); 

    void reset() {
        is_dragging = false;
        has_persistent_rect = false;
        img_start_pixel = cv::Point2f(-1, -1);
        img_end_pixel = cv::Point2f(-1, -1);
        normalized_cursor_pos = ImVec2(-1, -1);
        active_source_idx = -1;
    }
} g_sync;


// ============================================================================
// 3. PIPELINE FUNCTIONS
// ============================================================================
void updateGlobalTonemapCache(AppContext& ctx)
{
    if (ctx.base_img_raw.empty() && (!ctx.is_polarized || ctx.polar_raw_channels[0].empty())) return;

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
    if (ctx.images.empty() || i >= ctx.images.size()) return;
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
void renderControlPanel(AppContext& ctx, GLFWwindow* window)
{
    ImGui::Text("Tonemapping");
    if (ImGui::Button("Reinhard")) { ctx.mode = 1; ctx.needs_tonemap = true; if(is_dual_mode) { ctxB.mode = 1; ctxB.needs_tonemap = true; } } ImGui::SameLine();
    if (ImGui::Button("Drago"))    { ctx.mode = 2; ctx.needs_tonemap = true; if(is_dual_mode) { ctxB.mode = 2; ctxB.needs_tonemap = true; } } ImGui::SameLine();
    if (ImGui::Button("Mantiuk"))  { ctx.mode = 3; ctx.needs_tonemap = true; if(is_dual_mode) { ctxB.mode = 3; ctxB.needs_tonemap = true; } }

    ImGui::Separator();

    bool changed = false;
    if (ctx.mode == 1) {
        if (ImGui::SliderFloat("Gamma", &ctx.r_gamma, 0.1f, 3.0f))           changed = true;
        if (ImGui::SliderFloat("Intensity", &ctx.intensity, -8.0f, 8.0f))    changed = true;
        if (ImGui::SliderFloat("Light Adapt", &ctx.light_adapt, 0.0f, 1.0f))  changed = true;
        if (ImGui::SliderFloat("Color Adapt", &ctx.color_adapt, 0.0f, 1.0f))  changed = true;
    } else if (ctx.mode == 2) {
        if (ImGui::SliderFloat("Gamma", &ctx.d_gamma, 0.1f, 3.0f))           changed = true;
        if (ImGui::SliderFloat("Saturation", &ctx.d_saturation, 0.0f, 2.0f)) changed = true;
        if (ImGui::SliderFloat("Bias", &ctx.d_bias, 0.6f, 0.95f))            changed = true;
    } else if (ctx.mode == 3) {
        if (ImGui::SliderFloat("Gamma", &ctx.m_gamma, 0.1f, 3.0f))           changed = true;
        if (ImGui::SliderFloat("Scale", &ctx.m_scale, 0.1f, 2.0f))           changed = true;
        if (ImGui::SliderFloat("Saturation", &ctx.m_saturation, 0.0f, 2.0f)) changed = true;
    }

    if (changed) {
        ctx.needs_tonemap = true;
        if (is_dual_mode) {
            ctxB.r_gamma = ctx.r_gamma; ctxB.intensity = ctx.intensity;
            ctxB.light_adapt = ctx.light_adapt; ctxB.color_adapt = ctx.color_adapt;
            ctxB.d_gamma = ctx.d_gamma; ctxB.d_saturation = ctx.d_saturation; ctxB.d_bias = ctx.d_bias;
            ctxB.m_gamma = ctx.m_gamma; ctxB.m_scale = ctx.m_scale; ctxB.m_saturation = ctx.m_saturation;
            ctxB.needs_tonemap = true;
        }
    }

    if (ImGui::Button("Reset Zoom")) {
        int w = ctx.is_polarized ? ctx.polar_raw_channels[0].cols : ctx.base_img_raw.cols;
        int h = ctx.is_polarized ? ctx.polar_raw_channels[0].rows : ctx.base_img_raw.rows;
        ctx.zoom_state.Reset(w, h);
        g_sync.has_persistent_rect = false; 
        updateViewportImage(ctx);
        if (is_dual_mode) {
            ctxB.zoom_state.current_roi = ctx.zoom_state.current_roi;
            updateViewportImage(ctxB);
        }
    }

    if (ctx.colormap.is_active || (is_dual_mode && ctxB.colormap.is_active)) {
        ImGui::SameLine();
        if (ImGui::Button("Reset Range") || glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
            ctx.colormap.reset();
            g_sync.has_persistent_rect = false; 
            ctx.needs_tonemap = true;
            if (is_dual_mode) { 
                ctxB.colormap.reset(); 
                ctxB.needs_tonemap = true; 
            }
        }
    }
}


void drawSingleViewport(AppContext& ctx, ImVec2 size, int viewport_id, cv::Mat& current_raw, cv::Mat& base_raw, cv::Mat& current_ldr, cv::Mat& base_ldr, GLuint texture_id)
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

    // 1. CALCULAR ANTICIPADAMENTE LA POSICIÓN DE LOS BOTONES PARA DETECTAR HOVER REAL
    bool mouse_is_over_floating_buttons = false;
    ImVec2 dynamic_btn_pos(0, 0);
    ImVec2 btn_box_size(145.0f, 25.0f); // Tamaño estimado del grupo de botones

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

        // Comprobamos si el puntero está exactamente sobre la bounding box de los botones flotantes
        if (mouse_pos.x >= dynamic_btn_pos.x && mouse_pos.x <= (dynamic_btn_pos.x + btn_box_size.x) &&
            mouse_pos.y >= dynamic_btn_pos.y && mouse_pos.y <= (dynamic_btn_pos.y + btn_box_size.y)) {
            mouse_is_over_floating_buttons = true;
        }
    }

    // INTERACCIONES DE LA IMAGEN: Bloqueadas si el mouse está sobre los botones flotantes
    if (mouse_is_over && !over_buttons && !mouse_is_over_floating_buttons) {
        g_sync.normalized_cursor_pos = ImVec2((mouse_pos.x - img_screen_pos.x) / view_size.x, (mouse_pos.y - img_screen_pos.y) / view_size.y);
        
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            g_sync.has_persistent_rect = false;
            g_sync.img_start_pixel = cv::Point2f(-1, -1);
            g_sync.img_end_pixel = cv::Point2f(-1, -1);
            g_sync.active_source_idx = -1;
            ctxA.zoom_state.is_selecting = false;
            ctxB.zoom_state.is_selecting = false;
            ctxA.needs_tonemap = true;
            ctxB.needs_tonemap = true;
            updateViewportImage(ctxA);
            updateViewportImage(ctxB);
        }
    }

    if (mouse_is_over && !over_buttons && !mouse_is_over_floating_buttons && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        g_sync.is_dragging = true;
        g_sync.has_persistent_rect = false; 
        g_sync.active_source_idx = viewport_id;
        
        g_sync.img_start_pixel = cv::Point2f(-1, -1);
        g_sync.img_end_pixel = cv::Point2f(-1, -1);
        
        ctxA.zoom_state.is_selecting = false;
        ctxB.zoom_state.is_selecting = false;
        
        float norm_x = (mouse_pos.x - img_screen_pos.x) / view_size.x;
        float norm_y = (mouse_pos.y - img_screen_pos.y) / view_size.y;
        
        cv::Rect roi = ctx.zoom_state.current_roi;
        g_sync.img_start_pixel = cv::Point2f(roi.x + norm_x * roi.width, roi.y + norm_y * roi.height);
        g_sync.img_end_pixel = g_sync.img_start_pixel;

        ctxA.needs_tonemap = true;
        ctxB.needs_tonemap = true;
        updateViewportImage(ctxA);
        updateViewportImage(ctxB);
    }

    if (g_sync.is_dragging && g_sync.active_source_idx == viewport_id) {
        float norm_x = (mouse_pos.x - img_screen_pos.x) / view_size.x;
        float norm_y = (mouse_pos.y - img_screen_pos.y) / view_size.y;
        
        cv::Rect roi = ctx.zoom_state.current_roi;
        g_sync.img_end_pixel = cv::Point2f(roi.x + norm_x * roi.width, roi.y + norm_y * roi.height);
        
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            g_sync.is_dragging = false;
            g_sync.has_persistent_rect = true; 
            ctxA.needs_tonemap = true;
            ctxB.needs_tonemap = true;
            updateViewportImage(ctxA);
            updateViewportImage(ctxB);
        }
    }

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

    if (needs_roi_update || needs_pan_update || internal_texture || internal_tonemap) {
        if (internal_tonemap) ctx.needs_tonemap = true;
        updateViewportImage(ctx);
        
        if (is_dual_mode) {
            if (viewport_id == 0) {
                ctxB.zoom_state.current_roi = ctxA.zoom_state.current_roi;
                if (internal_tonemap) { ctxB.colormap = ctxA.colormap; ctxB.needs_tonemap = true; }
                updateViewportImage(ctxB);
            } else if (viewport_id == 1) {
                ctxA.zoom_state.current_roi = ctxB.zoom_state.current_roi;
                if (internal_tonemap) { ctxA.colormap = ctxB.colormap; ctxA.needs_tonemap = true; }
                updateViewportImage(ctxA);
            }
        }
    }
    
    // RENDERIZADO DEL RECUADRO VERDE Y LOS BOTONES
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

        if (g_sync.has_persistent_rect) 
        {
            // Posicionamos usando la variable calculada arriba de forma segura
            ImGui::SetCursorScreenPos(dynamic_btn_pos);
            
            ImGui::PushID(viewport_id); 
            
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
                    ctxA.zoom_state.current_roi = cv::Rect(roi_x, roi_y, roi_w, roi_h);
                    ctxB.zoom_state.current_roi = cv::Rect(roi_x, roi_y, roi_w, roi_h);
                    
                    g_sync.has_persistent_rect = false;
                    g_sync.img_start_pixel = cv::Point2f(-1, -1);
                    g_sync.img_end_pixel = cv::Point2f(-1, -1);
                    
                    ctxA.needs_tonemap = true;
                    ctxB.needs_tonemap = true;
                    updateViewportImage(ctxA);
                    updateViewportImage(ctxB);
                }
            }
            
            ImGui::SameLine();
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
                    
                    ctx.colormap.autoCenterAndRadius((float)global_min, (float)global_max);
                    
                    g_sync.has_persistent_rect = false;
                    g_sync.img_start_pixel = cv::Point2f(-1, -1);
                    g_sync.img_end_pixel = cv::Point2f(-1, -1);
                    ctx.needs_tonemap = true;
                    updateViewportImage(ctx);
                    if (is_dual_mode) {
                        if (viewport_id == 0) {
                            ctxB.colormap = ctx.colormap;
                            ctxB.needs_tonemap = true;
                            updateViewportImage(ctxB);
                        } else if (viewport_id == 1) {
                            ctxA.colormap = ctx.colormap;
                            ctxA.needs_tonemap = true;
                            updateViewportImage(ctxA);
                        }
                    }
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

    RenderPixelValuesOverlay(ctx.zoom_state, img_screen_pos, view_size, current_raw);
}

void renderViewportAndInteractions(AppContext& ctx)
{


    ImGui::BeginChild("img", ImVec2(0, 0), true);
    ImVec2 avail = ImGui::GetContentRegionAvail();

    if (is_dual_mode)
    {
        float half_w = avail.x * 0.5f - 4.0f;
        
        ImGui::BeginChild("left_pane", ImVec2(half_w, avail.y), true);
        drawSingleViewport(ctxA, ImVec2(half_w, avail.y), 0, ctxA.current_img_raw, ctxA.base_img_raw, ctxA.current_img_ldr, ctxA.base_img_ldr, ctxA.texture);
        ImGui::EndChild();
        
        ImGui::SameLine();
        
        ImGui::BeginChild("right_pane", ImVec2(half_w, avail.y), true);
        drawSingleViewport(ctxB, ImVec2(half_w, avail.y), 1, ctxB.current_img_raw, ctxB.base_img_raw, ctxB.current_img_ldr, ctxB.base_img_ldr, ctxB.texture);
        ImGui::EndChild();

        if (ctxA.needs_tonemap) { updateGlobalTonemapCache(ctxA); ctxA.needs_tonemap = false; updateViewportImage(ctxA); }
        if (ctxB.needs_tonemap) { updateGlobalTonemapCache(ctxB); ctxB.needs_tonemap = false; updateViewportImage(ctxB); }
    }
    else if (!ctx.is_polarized) 
    {
        drawSingleViewport(ctx, avail, 0, ctx.current_img_raw, ctx.base_img_raw, ctx.current_img_ldr, ctx.base_img_ldr, ctx.texture);
    } 
    else 
    {
        if (!ctx.polar_current_ldr[0].empty() && ctx.polar_textures[0])
        {
            float left_pane_w = avail.x * 0.65f - 4.0f;
            float right_pane_w = avail.x * 0.35f - 4.0f;
            
            float deg_cell_w = left_pane_w / 2.0f - 4.0f;
            float deg_cell_h = avail.y / 2.0f - 4.0f;
            float analytic_cell_h = avail.y / 2.0f - 4.0f;

            bool shared_roi_changed = g_sync.is_dragging;

            auto drawGridCell = [&](int idx, ImVec2 pos, ImVec2 cell_limits) {
                ImGui::SetCursorPos(pos);
                ImGui::BeginChild((std::string("cell_") + std::to_string(idx)).c_str(), cell_limits, true, ImGuiWindowFlags_NoScrollbar);
                
                drawSingleViewport(ctx, cell_limits, idx, ctx.polar_current_raw[idx], ctx.polar_raw_channels[idx], ctx.polar_current_ldr[idx], ctx.polar_ldr_channels[idx], ctx.polar_textures[idx]);

                ImVec2 img_screen_pos = ImGui::GetCursorScreenPos();
                ImVec2 text_pos(img_screen_pos.x + 12, img_screen_pos.y - cell_limits.y + 12); 
                ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), 24.0f, text_pos, IM_COL32(0, 255, 0, 255), ctx.polar_names[idx].c_str());

                ImGui::EndChild();
            };

            drawGridCell(0, ImVec2(4.0f, 4.0f), ImVec2(deg_cell_w, deg_cell_h));                                 
            drawGridCell(1, ImVec2(4.0f + left_pane_w / 2.0f, 4.0f), ImVec2(deg_cell_w, deg_cell_h));            
            drawGridCell(3, ImVec2(4.0f, 4.0f + avail.y / 2.0f), ImVec2(deg_cell_w, deg_cell_h));                
            drawGridCell(4, ImVec2(4.0f + left_pane_w / 2.0f, 4.0f + avail.y / 2.0f), ImVec2(deg_cell_w, deg_cell_h)); 

            drawGridCell(2, ImVec2(left_pane_w + 8.0f, 4.0f), ImVec2(right_pane_w, analytic_cell_h));                
            drawGridCell(5, ImVec2(left_pane_w + 8.0f, 4.0f + avail.y / 2.0f), ImVec2(right_pane_w, analytic_cell_h)); 

            if (!g_sync.is_dragging && g_sync.has_persistent_rect) {
                shared_roi_changed = true;
                if (g_sync.active_source_idx >= 0 && g_sync.active_source_idx < 6) {
                     ctx.needs_tonemap = true;
                }
            }

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

    if (argc < 2) {
        std::cout << "Usage (Single/Polarized Grid): ./bin/rev <image_or_folder>\n";
        std::cout << "Usage (Dual Comparison): ./bin/rev <image1> <image2>\n";
        return -1;
    }

    if (argc >= 3) {
        is_dual_mode = true;
        ctxA.images.push_back(argv[1]);
        ctxB.images.push_back(argv[2]);
    } else {
        is_dual_mode = false;
        std::string input = argv[1];
        if (std::filesystem::is_directory(input))
            ctxA.images = getImages(input);
        else
            ctxA.images.push_back(input);
    }

    if (ctxA.images.empty()) return -1;

    #ifdef __APPLE__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    #endif

    GLFWwindow* window = glfwCreateWindow(1280, 720, "REV Engine - Workspace", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); 

    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    loadRawImage(ctxA, 0);
    updateTexture(ctxA);

    if (is_dual_mode) {
        loadRawImage(ctxB, 0);
        updateTexture(ctxB);
        ctxB.zoom_state.current_roi = ctxA.zoom_state.current_roi;
    }

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // if (g_sync.has_persistent_rect && 
        //    (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
        //     g_sync.has_persistent_rect = false;
        // // g_sync.img_start_pixel = cv::Point2f(-1, -1);
        // // g_sync.img_end_pixel = cv::Point2f(-1, -1);
        // g_sync.reset();
        // }

        if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
            ctxA.resetToFactoryDefaults();
            loadRawImage(ctxA, ctxA.current_idx);
            if(is_dual_mode) {
                ctxB.resetToFactoryDefaults();
                loadRawImage(ctxB, ctxB.current_idx);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_S)) {
            ctxA.colorspace.cycle();
            updateGlobalTonemapCache(ctxA);
            updateViewportImage(ctxA);
            if (is_dual_mode) {
                ctxB.colorspace.cycle();
                updateGlobalTonemapCache(ctxB);
                updateViewportImage(ctxB);
            }
        }

        auto navigationCallback = [&](int new_idx) { loadRawImage(ctxA, new_idx); };
        HandleKeyboardNavigation(ctxA.current_idx, ctxA.images.size(), navigationCallback);

        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("HDR Viewer", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        renderControlPanel(ctxA, window);
        ImGui::Separator();

        if (ctxA.needs_texture) { updateTexture(ctxA); ctxA.needs_texture = false; }
        if (is_dual_mode && ctxB.needs_texture) { updateTexture(ctxB); ctxB.needs_texture = false; }

        renderViewportAndInteractions(ctxA);

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
    
    if (ctxA.texture) glDeleteTextures(1, &ctxA.texture);
    if (ctxB.texture) glDeleteTextures(1, &ctxB.texture);
    for(auto tex : ctxA.polar_textures) if(tex) glDeleteTextures(1, &tex);
    for(auto tex : ctxB.polar_textures) if(tex) glDeleteTextures(1, &tex);

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}