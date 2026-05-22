#include "KeyboardControls.h"

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

void DrawResetZoomButton(GLFWwindow* window, cv::Mat& current_img_raw, const cv::Mat& base_img_raw, 
                        ZoomState& zoom_state, bool& needs_update)
{
    if (!base_img_raw.empty() && !current_img_raw.empty() && 
        (base_img_raw.cols != current_img_raw.cols || base_img_raw.rows != current_img_raw.rows))
    {
        ImGui::SameLine();
        
        bool z_pressed = ImGui::IsKeyPressed(ImGuiKey_Z);

        if (ImGui::Button("Reset Zoom") || z_pressed)
        {
            current_img_raw = base_img_raw.clone();
            zoom_state.Reset(base_img_raw.cols, base_img_raw.rows);
            needs_update = true;
        }
    }
}