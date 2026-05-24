#include "MouseControls.h"
#include "Colormap.h"
#include <algorithm>

void DrawCustomCursor()
{
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    float cursorSize = 3.0f; 
    float thickness = 1.0f;   

    ImGui::GetForegroundDrawList()->AddRect(
        ImVec2(mousePos.x - cursorSize, mousePos.y - cursorSize), 
        ImVec2(mousePos.x + cursorSize, mousePos.y + cursorSize), 
        IM_COL32(0, 255, 0, 255),                                 
        0.0f, 0, thickness                                                 
    );
}

void HandleScrollZoom(ZoomState& zoom_state, const ImVec2& img_screen_pos, const ImVec2& size, 
                     cv::Mat& current_img_raw, const cv::Mat& base_img_raw, bool& needs_update)
{
    if (base_img_raw.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mousePos = io.MousePos;
    
    float uv_x = (mousePos.x - img_screen_pos.x) / size.x;
    float uv_y = (mousePos.y - img_screen_pos.y) / size.y;

    if (uv_x >= 0.0f && uv_x <= 1.0f && uv_y >= 0.0f && uv_y <= 1.0f)
    {
        if (io.MouseWheel != 0.0f)
        {
            // --- Infinite Zoom Logic with Truncation Fix ---
            float zoom_factor = (io.MouseWheel > 0) ? 0.8f : 1.25f; 
            
            int new_w, new_h;

            if (zoom_factor < 1.0f) 
            {
                new_w = std::max(1, static_cast<int>(zoom_state.current_roi.width * zoom_factor));
                new_h = std::max(1, static_cast<int>(zoom_state.current_roi.height * zoom_factor));
            }
            else 
            {
                new_w = std::max(zoom_state.current_roi.width + 1, static_cast<int>(zoom_state.current_roi.width * zoom_factor));
                new_h = std::max(zoom_state.current_roi.height + 1, static_cast<int>(zoom_state.current_roi.height * zoom_factor));
            }
            
            if (new_w > base_img_raw.cols) new_w = base_img_raw.cols;
            if (new_h > base_img_raw.rows) new_h = base_img_raw.rows;
            
            float focus_x = zoom_state.current_roi.x + (uv_x * zoom_state.current_roi.width);
            float focus_y = zoom_state.current_roi.y + (uv_y * zoom_state.current_roi.height);
            
            int new_x = static_cast<int>(focus_x - (uv_x * new_w));
            int new_y = static_cast<int>(focus_y - (uv_y * new_h));
            
            new_x = std::clamp(new_x, 0, base_img_raw.cols - new_w);
            new_y = std::clamp(new_y, 0, base_img_raw.rows - new_h);
            
            cv::Rect new_roi(new_x, new_y, new_w, new_h);
            new_roi &= cv::Rect(0, 0, base_img_raw.cols, base_img_raw.rows);
            
            if (new_roi.width >= 1 && new_roi.height >= 1)
            {
                zoom_state.current_roi = new_roi;
                current_img_raw = base_img_raw(new_roi).clone();
                needs_update = true;
            }
        }
    }
}

void HandleZoomAndSelection(ZoomState& zoom_state, const ImVec2& img_screen_pos, const ImVec2& size, 
                            cv::Mat& current_img_ldr, const cv::Mat& base_img_ldr, const cv::Mat& base_img_raw, 
                            Colormap& colormap, bool& needs_tonemap, bool& needs_texture)
{
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    
    // Crucial: Use AllowWhenOverlapped so the bounding box selection works even with deep layers of text
    bool is_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlapped); 

    if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        zoom_state.is_selecting = true;
        zoom_state.has_selection = false;
        zoom_state.sel_start_uv = ImVec2((mousePos.x - img_screen_pos.x) / size.x, (mousePos.y - img_screen_pos.y) / size.y);
        zoom_state.sel_end_uv = zoom_state.sel_start_uv;
    }

    if (zoom_state.is_selecting && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        zoom_state.sel_end_uv = ImVec2((mousePos.x - img_screen_pos.x) / size.x, (mousePos.y - img_screen_pos.y) / size.y);
        zoom_state.sel_end_uv.x = std::clamp(zoom_state.sel_end_uv.x, 0.0f, 1.0f);
        zoom_state.sel_end_uv.y = std::clamp(zoom_state.sel_end_uv.y, 0.0f, 1.0f);
    }

    if (zoom_state.is_selecting && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        zoom_state.is_selecting = false;
        
        int x1 = std::min(zoom_state.sel_start_uv.x, zoom_state.sel_end_uv.x) * zoom_state.current_roi.width + zoom_state.current_roi.x;
        int y1 = std::min(zoom_state.sel_start_uv.y, zoom_state.sel_end_uv.y) * zoom_state.current_roi.height + zoom_state.current_roi.y;
        int x2 = std::max(zoom_state.sel_start_uv.x, zoom_state.sel_end_uv.x) * zoom_state.current_roi.width + zoom_state.current_roi.x;
        int y2 = std::max(zoom_state.sel_start_uv.y, zoom_state.sel_end_uv.y) * zoom_state.current_roi.height + zoom_state.current_roi.y;
        
        zoom_state.sel_start_uv.x = (float)x1 / base_img_raw.cols;
        zoom_state.sel_start_uv.y = (float)y1 / base_img_raw.rows;
        zoom_state.sel_end_uv.x   = (float)x2 / base_img_raw.cols;
        zoom_state.sel_end_uv.y   = (float)y2 / base_img_raw.rows;

        zoom_state.has_selection = true;
    }

    if (zoom_state.is_selecting || zoom_state.has_selection) {
        ImVec2 p_min, p_max;
        int img_x1, img_y1, img_x2, img_y2;

        ImVec2 img_bound_min = img_screen_pos;
        ImVec2 img_bound_max = ImVec2(img_screen_pos.x + size.x, img_screen_pos.y + size.y);

        if (zoom_state.is_selecting) {
            p_min = ImVec2(img_screen_pos.x + std::min(zoom_state.sel_start_uv.x, zoom_state.sel_end_uv.x) * size.x,
                           img_screen_pos.y + std::min(zoom_state.sel_start_uv.y, zoom_state.sel_end_uv.y) * size.y);
            p_max = ImVec2(img_screen_pos.x + std::max(zoom_state.sel_start_uv.x, zoom_state.sel_end_uv.x) * size.x,
                           img_screen_pos.y + std::max(zoom_state.sel_start_uv.y, zoom_state.sel_end_uv.y) * size.y);
            
            img_x1 = std::min(zoom_state.sel_start_uv.x, zoom_state.sel_end_uv.x) * zoom_state.current_roi.width + zoom_state.current_roi.x;
            img_y1 = std::min(zoom_state.sel_start_uv.y, zoom_state.sel_end_uv.y) * zoom_state.current_roi.height + zoom_state.current_roi.y;
            img_x2 = std::max(zoom_state.sel_start_uv.x, zoom_state.sel_end_uv.x) * zoom_state.current_roi.width + zoom_state.current_roi.x;
            img_y2 = std::max(zoom_state.sel_start_uv.y, zoom_state.sel_end_uv.y) * zoom_state.current_roi.height + zoom_state.current_roi.y;
        } 
        else {
            img_x1 = zoom_state.sel_start_uv.x * base_img_raw.cols;
            img_y1 = zoom_state.sel_start_uv.y * base_img_raw.rows;
            img_x2 = zoom_state.sel_end_uv.x * base_img_raw.cols;
            img_y2 = zoom_state.sel_end_uv.y * base_img_raw.rows;

            float u1 = (float)(img_x1 - zoom_state.current_roi.x) / zoom_state.current_roi.width;
            float v1 = (float)(img_y1 - zoom_state.current_roi.y) / zoom_state.current_roi.height;
            float u2 = (float)(img_x2 - zoom_state.current_roi.x) / zoom_state.current_roi.width;
            float v2 = (float)(img_y2 - zoom_state.current_roi.y) / zoom_state.current_roi.height;

            p_min = ImVec2(img_screen_pos.x + u1 * size.x, img_screen_pos.y + v1 * size.y);
            p_max = ImVec2(img_screen_pos.x + u2 * size.x, img_screen_pos.y + v2 * size.y);
        }

        p_min.x = std::clamp(p_min.x, img_bound_min.x, img_bound_max.x);
        p_min.y = std::clamp(p_min.y, img_bound_min.y, img_bound_max.y);
        p_max.x = std::clamp(p_max.x, img_bound_min.x, img_bound_max.x);
        p_max.y = std::clamp(p_max.y, img_bound_min.y, img_bound_max.y);

        if (p_max.x - p_min.x > 1.0f && p_max.y - p_min.y > 1.0f) {
            // Drawn over foreground to guarantee visibility over any grid text overlays
            ImGui::GetForegroundDrawList()->AddRect(p_min, p_max, IM_COL32(0, 255, 0, 255), 0.0f, 0, 2.0f);

            if (zoom_state.has_selection) {
                float btn_x = std::clamp(p_max.x - 145.0f, img_bound_min.x, img_bound_max.x - 145.0f);
                float btn_y = std::clamp(p_max.y + 5.0f, img_bound_min.y, img_bound_max.y - 35.0f);
                
                // Force buttons to render on top of everything inside the window stack
                ImGui::SetCursorScreenPos(ImVec2(btn_x, btn_y));
                
                ImGui::PushID("BBoxActions");
                
                if (ImGui::Button("Zoom")) {
                    int w = std::abs(img_x2 - img_x1);
                    int h = std::abs(img_y2 - img_y1);

                    cv::Rect new_roi(img_x1, img_y1, w, h);
                    new_roi &= cv::Rect(0, 0, base_img_raw.cols, base_img_raw.rows);

                    if (new_roi.width >= 1 && new_roi.height >= 1) {
                        zoom_state.current_roi = new_roi;
                        current_img_ldr = base_img_ldr(zoom_state.current_roi).clone();
                        needs_texture = true;
                        zoom_state.has_selection = false; // Reset selection window on execution
                    }
                }
                
                ImGui::SameLine();

                if (ImGui::Button("AutoRange")) {
                    cv::Rect selected_roi(img_x1, img_y1, std::abs(img_x2 - img_x1), std::abs(img_y2 - img_y1));
                    selected_roi &= cv::Rect(0, 0, base_img_raw.cols, base_img_raw.rows);

                    if (selected_roi.width > 5 && selected_roi.height > 5) {
                        cv::Mat roi_hdr = base_img_raw(selected_roi);
                        double min_val, max_val;
                        cv::minMaxIdx(roi_hdr, &min_val, &max_val);

                        colormap.autoCenterAndRadius(static_cast<float>(min_val), static_cast<float>(max_val));
                        needs_tonemap = true; 
                    }
                }
                
                ImGui::PopID();
            }
        }
    }
}

void HandleMousePanning(ZoomState& zoom_state, const ImVec2& size, cv::Mat& current_img_raw, const cv::Mat& base_img_raw, bool& needs_update)
{
    if (base_img_raw.empty()) return;

    ImGuiIO& io = ImGui::GetIO();

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        ImVec2 mouse_delta = io.MouseDelta;

        if (mouse_delta.x != 0.0f || mouse_delta.y != 0.0f)
        {
            float scale_x = static_cast<float>(zoom_state.current_roi.width) / size.x;
            float scale_y = static_cast<float>(zoom_state.current_roi.height) / size.y;

            int delta_img_x = static_cast<int>(mouse_delta.x * scale_x);
            int delta_img_y = static_cast<int>(mouse_delta.y * scale_y);

            int new_x = zoom_state.current_roi.x - delta_img_x;
            int new_y = zoom_state.current_roi.y - delta_img_y;

            new_x = std::clamp(new_x, 0, base_img_raw.cols - zoom_state.current_roi.width);
            new_y = std::clamp(new_y, 0, base_img_raw.rows - zoom_state.current_roi.height);

            if (new_x != zoom_state.current_roi.x || new_y != zoom_state.current_roi.y)
            {
                zoom_state.current_roi.x = new_x;
                zoom_state.current_roi.y = new_y;
                
                current_img_raw = base_img_raw(zoom_state.current_roi).clone();
                needs_update = true;
            }
        }
    }
}

// pixel values overlay
void RenderPixelValuesOverlay(const ZoomState& zoom_state, const ImVec2& img_screen_pos, const ImVec2& size, const cv::Mat& current_img_ldr, const cv::Mat& raw_hdr_img, const ImVec2& normalized_cursor_pos)
{
    // Check using the raw_hdr_img instead
    if (raw_hdr_img.empty()) return;

    int visible_cols = raw_hdr_img.cols;
    int visible_rows = raw_hdr_img.rows;

    float pixel_width_dst = size.x / static_cast<float>(visible_cols);
    float pixel_height_dst = size.y / static_cast<float>(visible_rows);

    if (pixel_width_dst < 15.0f || pixel_height_dst < 15.0f) return;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    float base_threshold = 85.0f; 
    float dynamic_font_scale = pixel_width_dst / base_threshold;
    dynamic_font_scale = std::clamp(dynamic_font_scale, 0.15f, 4.0f);

    ImGui::SetWindowFontScale(dynamic_font_scale);
    float scaled_font_height = ImGui::GetFontSize();

    int hover_c = -1;
    int hover_r = -1;
    if (normalized_cursor_pos.x >= 0.0f && normalized_cursor_pos.y >= 0.0f) {
        hover_c = static_cast<int>(normalized_cursor_pos.x * visible_cols);
        hover_r = static_cast<int>(normalized_cursor_pos.y * visible_rows);
    }

    int img_type = raw_hdr_img.type();
    int depth = raw_hdr_img.depth();
    bool is_float = (depth == CV_32F || depth == CV_64F);

    // Check actual HDR range values
    bool use_scientific = false;
    if (is_float) {
        double max_val = 0.0;
        cv::minMaxLoc(raw_hdr_img, nullptr, &max_val);
        if (max_val < 0.1) {
            use_scientific = true;
        }
    }

    for (int r = 0; r < visible_rows; ++r)
    {
        for (int c = 0; c < visible_cols; ++c)
        {
            float x0 = img_screen_pos.x + (c * pixel_width_dst);
            float y0 = img_screen_pos.y + (r * pixel_height_dst);
            float x1 = x0 + pixel_width_dst;
            float y1 = y0 + pixel_height_dst;

            bool is_hovered = (c == hover_c && r == hover_r);

            if (is_hovered)
            {
                ImGui::GetForegroundDrawList()->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(0, 255, 0, 255), 0.0f, 0, 2.0f);
            }
            else
            {
                draw_list->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(255, 255, 255, 30), 0.0f, 0, 1.0f);
            }

            float r_val = 0.0f, g_val = 0.0f, b_val = 0.0f;

            // Read from raw_hdr_img data directly
            if (img_type == CV_32FC3) { 
                cv::Vec3f pixel = raw_hdr_img.at<cv::Vec3f>(r, c);
                b_val = pixel[0]; g_val = pixel[1]; r_val = pixel[2];
            }
            else if (img_type == CV_32FC1) {
                b_val = g_val = r_val = raw_hdr_img.at<float>(r, c);
            }
            else if (img_type == CV_8UC3) {
                cv::Vec3b pixel = raw_hdr_img.at<cv::Vec3b>(r, c);
                b_val = pixel[0]; g_val = pixel[1]; r_val = pixel[2];
            } 
            else if (img_type == CV_8UC4) { 
                cv::Vec4b pixel = raw_hdr_img.at<cv::Vec4b>(r, c);
                b_val = pixel[0]; g_val = pixel[1]; r_val = pixel[2];
            }
            else if (img_type == CV_8UC1) { 
                b_val = g_val = r_val = raw_hdr_img.at<uchar>(r, c); 
            }
            else {
                b_val = g_val = r_val = 0.5f;
            }

            char text_r[32]; char text_g[32]; char text_b[32];

            if (is_float) {
                if (use_scientific) {
                    snprintf(text_r, sizeof(text_r), "R:%.3e", r_val);
                    snprintf(text_g, sizeof(text_g), "G:%.3e", g_val);
                    snprintf(text_b, sizeof(text_b), "B:%.3e", b_val);
                } else {
                    snprintf(text_r, sizeof(text_r), "R:%.3f", r_val);
                    snprintf(text_g, sizeof(text_g), "G:%.3f", g_val);
                    snprintf(text_b, sizeof(text_b), "B:%.3f", b_val);
                }
            } else {
                snprintf(text_r, sizeof(text_r), "R:%d", (int)r_val);
                snprintf(text_g, sizeof(text_g), "G:%d", (int)g_val);
                snprintf(text_b, sizeof(text_b), "B:%d", (int)b_val);
            }

            // Text contrast using current_img_ldr brightness context
            float luma = 0.0f;
            if (!current_img_ldr.empty()) {
                int ldr_type = current_img_ldr.type();
                if (ldr_type == CV_8UC3 || ldr_type == CV_8UC4) {
                    cv::Vec3b ldr_pix = current_img_ldr.at<cv::Vec3b>(r, c);
                    luma = (0.299f * ldr_pix[2] + 0.587f * ldr_pix[1] + 0.114f * ldr_pix[0]) / 255.0f;
                }
            }
            bool is_light_bg = (luma > 0.5f);

            ImU32 color_r = is_light_bg ? IM_COL32(180, 0, 0, 255)   : IM_COL32(255, 80, 80, 255);
            ImU32 color_g = is_light_bg ? IM_COL32(0, 130, 0, 255)   : IM_COL32(80, 255, 80, 255);
            ImU32 color_b = is_light_bg ? IM_COL32(0, 50, 220, 255)  : IM_COL32(100, 160, 255, 255);

            float start_y = y0 + (pixel_height_dst - (scaled_font_height * 3.0f)) * 0.5f;
            float center_x_offset = pixel_width_dst * 0.08f; 

            draw_list->AddText(ImVec2(x0 + center_x_offset, start_y), color_r, text_r);
            draw_list->AddText(ImVec2(x0 + center_x_offset, start_y + scaled_font_height), color_g, text_g);
            draw_list->AddText(ImVec2(x0 + center_x_offset, start_y + (scaled_font_height * 2.0f)), color_b, text_b);
        }
    }

    ImGui::SetWindowFontScale(1.0f);
}