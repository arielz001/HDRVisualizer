#pragma once
#include <opencv2/opencv.hpp>

cv::Mat applyTonemap(const cv::Mat& in,
                     int mode,
                     float r_gamma, float intensity, float light_adapt, float color_adapt,
                     float d_gamma, float d_saturation, float d_bias,
                     float m_gamma, float m_scale, float m_saturation);