#pragma once
#include "imgui.h"
#include <opencv2/opencv.hpp>
#include <functional>

// Struct to hold the state of our zoom selection and current view
struct ZoomState {
    bool is_selecting = false;
    bool has_selection = false;
    ImVec2 sel_start_uv = {0.0f, 0.0f};
    ImVec2 sel_end_uv = {0.0f, 0.0f};
    
    // Track the current crop area relative to the base image
    cv::Rect current_roi; 
    
    // Resets the selection state and full view
    void Reset(int width = 0, int height = 0) {
        is_selecting = false;
        has_selection = false;
        if (width > 0 && height > 0) {
            current_roi = cv::Rect(0, 0, width, height);
        }
    }
};

// Draws the custom green square cursor
void DrawCustomCursor();

// Handles left/right arrow keys to navigate images
void HandleKeyboardNavigation(int& current_idx, int total_images, const std::function<void(int)>& load_callback);

// Shows the "Reset Zoom" button if the image is currently zoomed
void DrawResetZoomButton(cv::Mat& current_img_raw, const cv::Mat& base_img_raw, ZoomState& zoom_state, bool& needs_update);

// Handles BBox right-click selection box and the "Zoom" button logic over the image
void HandleZoomAndSelection(ZoomState& zoom_state, const ImVec2& img_screen_pos, const ImVec2& img_size, cv::Mat& current_img_raw, const cv::Mat& base_img_raw, bool& needs_update);


// NEW: Handles holding 'Z' and scrolling to zoom in/out relative to mouse cursor
void HandleScrollZoom(ZoomState& zoom_state, const ImVec2& img_screen_pos, const ImVec2& img_size, cv::Mat& current_img_raw, const cv::Mat& base_img_raw, bool& needs_update);
