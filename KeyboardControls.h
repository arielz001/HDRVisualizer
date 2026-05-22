#pragma once
#include "imgui.h"
#include <opencv2/opencv.hpp>
#include <functional>

// En lugar de incluir Controls.h, incluimos MouseControls.h para heredar la estructura ZoomState
#include "MouseControls.h" 

struct GLFWwindow;

void HandleKeyboardNavigation(int& current_idx, 
                              int total_images, 
                              const std::function<void(int)>& load_callback);

void DrawResetZoomButton(GLFWwindow* window, 
                         cv::Mat& current_img_raw, 
                         const cv::Mat& base_img_raw, 
                         ZoomState& zoom_state, 
                         bool& needs_update);