#pragma once
#include "imgui.h"
#include <opencv2/opencv.hpp>

// Definimos la estructura aquí para que no dependa de ningún "Controls.h"
struct ZoomState {
    cv::Rect current_roi;
    bool is_selecting = false;
    bool has_selection = false;
    ImVec2 sel_start_uv = ImVec2(0,0);
    ImVec2 sel_end_uv = ImVec2(0,0);

    void Reset(int width, int height) {
        current_roi = cv::Rect(0, 0, width, height);
        is_selecting = false;
        has_selection = false;
    }
};

class Colormap;

void DrawCustomCursor();

void HandleScrollZoom(ZoomState& zoom_state, 
                     const ImVec2& img_screen_pos, 
                     const ImVec2& size, 
                     cv::Mat& current_img_raw, 
                     const cv::Mat& base_img_raw, 
                     bool& needs_update);

void HandleZoomAndSelection(ZoomState& zoom_state, 
                            const ImVec2& img_screen_pos, 
                            const ImVec2& size, 
                            cv::Mat& current_img_ldr, 
                            const cv::Mat& base_img_ldr, 
                            const cv::Mat& base_img_raw, 
                            Colormap& colormap, 
                            bool& needs_tonemap, 
                            bool& needs_texture);

void HandleMousePanning(ZoomState& zoom_state, 
                        const ImVec2& size, 
                        cv::Mat& current_img_raw, 
                        const cv::Mat& base_img_raw, 
                        bool& needs_update);


void RenderPixelValuesOverlay(const ZoomState& zoom_state, 
                              const ImVec2& img_screen_pos, 
                              const ImVec2& size, 
                              const cv::Mat& current_img_ldr);