#include "Colormap.h"
#include <algorithm>

Colormap::Colormap() {
    radius = 0.5f;
    center = {0.5f, 0.5f, 0.5f};
    is_active = false;
}

void Colormap::autoCenterAndRadius(float min, float max) {
    if (min >= max) {
        radius = 0.5f;
    } else {
        radius = (max - min) / 2.0f;
    }
    for (int i = 0; i < 3; i++) {
        center[i] = (max + min) / 2.0f;
    }
    is_active = true;
}

void Colormap::reset() {
    radius = 0.5f;
    center = {0.5f, 0.5f, 0.5f};
    is_active = false;
}

std::array<float, 3> Colormap::getScale() const {
    std::array<float, 3> scale;
    float safe_radius = std::max(radius, 0.0001f);
    for (int i = 0; i < 3; i++) {
        scale[i] = 1.0f / (2.0f * safe_radius);
    }
    return scale;
}

std::array<float, 3> Colormap::getBias() const {
    std::array<float, 3> bias;
    std::array<float, 3> scale = getScale();
    for (int i = 0; i < 3; i++) {
        bias[i] = (radius - center[i]) * scale[i];
    }
    return bias;
}

cv::Mat Colormap::apply(const cv::Mat& input_hdr) const {
    if (input_hdr.empty() || !is_active) return input_hdr;

    cv::Mat output = input_hdr.clone();

    // Calculate window bounds from center and radius
    float min_val = center[0] - radius;
    float max_val = center[0] + radius;

    // Prevent division by zero on uniform regions
    if (max_val <= min_val) max_val = min_val + 0.001f;

    // Apply linear contrast stretching
    output = (output - min_val) / (max_val - min_val);

    // Clamp values strictly to [0.0, 1.0] range
    cv::threshold(output, output, 1.0, 1.0, cv::THRESH_TRUNC);
    cv::threshold(output, output, 0.0, 0.0, cv::THRESH_TOZERO);

    return output;
}