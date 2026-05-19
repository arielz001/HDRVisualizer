#include "Controls.h"
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

void HandleKeyboardNavigation(int& current_idx, int total_images, const std::function<void(int)>& load_callback)
{
    if (total_images == 0) return;

    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
    {
        current_idx = (current_idx + 1) % total_images;
        load_callback(current_idx);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
    {
        current_idx = (current_idx - 1 + total_images) % total_images;
        load_callback(current_idx);
    }
}

void DrawResetZoomButton(cv::Mat& current_img_raw, const cv::Mat& base_img_raw, ZoomState& zoom_state, bool& needs_update)
{
    if (!base_img_raw.empty() && !current_img_raw.empty() && 
        (base_img_raw.cols != current_img_raw.cols || base_img_raw.rows != current_img_raw.rows))
    {
        ImGui::SameLine();
        if (ImGui::Button("Reset Zoom"))
        {
            current_img_raw = base_img_raw.clone();
            zoom_state.Reset(base_img_raw.cols, base_img_raw.rows);
            needs_update = true;
        }
    }
}

void HandleScrollZoom(ZoomState& zoom_state, const ImVec2& img_screen_pos, const ImVec2& size, cv::Mat& current_img_raw, const cv::Mat& base_img_raw, bool& needs_update)
{
    ImGuiIO& io = ImGui::GetIO();
    
    // Check if 'Z' is held down and scroll wheel is used
    if (ImGui::IsKeyDown(ImGuiKey_Z) && io.MouseWheel != 0.0f && !base_img_raw.empty())
    {
        ImVec2 mousePos = io.MousePos;
        
        // Calculate where the mouse is relative to the rendered image (0.0 to 1.0)
        float uv_x = (mousePos.x - img_screen_pos.x) / size.x;
        float uv_y = (mousePos.y - img_screen_pos.y) / size.y;

        // Only zoom if the mouse is actually hovering over the image
        if (uv_x >= 0.0f && uv_x <= 1.0f && uv_y >= 0.0f && uv_y <= 1.0f)
        {
            float zoom_factor = (io.MouseWheel > 0) ? 0.8f : 1.25f; 
            
            int new_w = zoom_state.current_roi.width * zoom_factor;
            int new_h = zoom_state.current_roi.height * zoom_factor;
            
            // Prevent zooming out beyond the original image bounds
            if (new_w > base_img_raw.cols || new_h > base_img_raw.rows) {
                new_w = base_img_raw.cols;
                new_h = base_img_raw.rows;
            }
            
            // Find the exact pixel in the base image the mouse is pointing at
            float focus_x = zoom_state.current_roi.x + (uv_x * zoom_state.current_roi.width);
            float focus_y = zoom_state.current_roi.y + (uv_y * zoom_state.current_roi.height);
            
            // Shift the new ROI so the focus pixel stays under the mouse UV
            int new_x = focus_x - (uv_x * new_w);
            int new_y = focus_y - (uv_y * new_h);
            
            // Clamp so the ROI doesn't go out of bounds (keeps it anchored to edges)
            new_x = std::clamp(new_x, 0, base_img_raw.cols - new_w);
            new_y = std::clamp(new_y, 0, base_img_raw.rows - new_h);
            
            cv::Rect new_roi(new_x, new_y, new_w, new_h);
            
            // Final safety clamp for OpenCV
            new_roi &= cv::Rect(0, 0, base_img_raw.cols, base_img_raw.rows);
            
            if (new_roi.width > 10 && new_roi.height > 10)
            {
                zoom_state.current_roi = new_roi;
                current_img_raw = base_img_raw(new_roi).clone();
                needs_update = true;
            }
        }
    }
}

void HandleZoomAndSelection(ZoomState& zoom_state, const ImVec2& img_screen_pos, const ImVec2& size, cv::Mat& current_img_raw, const cv::Mat& base_img_raw, bool& needs_update)
{
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    bool is_hovered = ImGui::IsItemHovered(); 

    // 1. Start selection with Right Click
    if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        zoom_state.is_selecting = true;
        zoom_state.has_selection = false;
        zoom_state.sel_start_uv = ImVec2((mousePos.x - img_screen_pos.x) / size.x, 
                                         (mousePos.y - img_screen_pos.y) / size.y);
        zoom_state.sel_end_uv = zoom_state.sel_start_uv;
    }

    // 2. Drag
    if (zoom_state.is_selecting && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        zoom_state.sel_end_uv = ImVec2((mousePos.x - img_screen_pos.x) / size.x, 
                                       (mousePos.y - img_screen_pos.y) / size.y);
        zoom_state.sel_end_uv.x = std::clamp(zoom_state.sel_end_uv.x, 0.0f, 1.0f);
        zoom_state.sel_end_uv.y = std::clamp(zoom_state.sel_end_uv.y, 0.0f, 1.0f);
    }

    // 3. Release Click
    if (zoom_state.is_selecting && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        zoom_state.is_selecting = false;
        zoom_state.has_selection = true;
    }

    // 4. Draw BBox and Zoom Button
    if (zoom_state.is_selecting || zoom_state.has_selection) {
        ImVec2 p_min(img_screen_pos.x + std::min(zoom_state.sel_start_uv.x, zoom_state.sel_end_uv.x) * size.x,
                     img_screen_pos.y + std::min(zoom_state.sel_start_uv.y, zoom_state.sel_end_uv.y) * size.y);
        ImVec2 p_max(img_screen_pos.x + std::max(zoom_state.sel_start_uv.x, zoom_state.sel_end_uv.x) * size.x,
                     img_screen_pos.y + std::max(zoom_state.sel_start_uv.y, zoom_state.sel_end_uv.y) * size.y);

        if (p_max.x - p_min.x > 10.0f && p_max.y - p_min.y > 10.0f) {
            ImGui::GetWindowDrawList()->AddRect(p_min, p_max, IM_COL32(0, 255, 0, 255), 0.0f, 0, 2.0f);

            if (zoom_state.has_selection) {
                ImGui::SetCursorScreenPos(ImVec2(p_max.x - 45.0f, p_max.y + 5.0f));
                ImGui::PushID("ZoomBtn");
                if (ImGui::Button("Zoom")) {
                    
                    // Map UVs to the CURRENT ROI bounds
                    int x = std::min(zoom_state.sel_start_uv.x, zoom_state.sel_end_uv.x) * zoom_state.current_roi.width;
                    int y = std::min(zoom_state.sel_start_uv.y, zoom_state.sel_end_uv.y) * zoom_state.current_roi.height;
                    int w = std::abs(zoom_state.sel_end_uv.x - zoom_state.sel_start_uv.x) * zoom_state.current_roi.width;
                    int h = std::abs(zoom_state.sel_end_uv.y - zoom_state.sel_start_uv.y) * zoom_state.current_roi.height;

                    // Offset by current ROI coordinates to keep track relative to base_img
                    x += zoom_state.current_roi.x;
                    y += zoom_state.current_roi.y;

                    cv::Rect new_roi(x, y, w, h);
                    new_roi &= cv::Rect(0, 0, base_img_raw.cols, base_img_raw.rows);

                    if (new_roi.width > 0 && new_roi.height > 0) {
                        zoom_state.current_roi = new_roi;
                        current_img_raw = base_img_raw(new_roi).clone();
                        needs_update = true;
                    }
                    zoom_state.has_selection = false;
                }
                ImGui::PopID();
            }
        }
        else {
            zoom_state.has_selection = false;
        }
    }
}