
#include "Colorspace.h"

Colorspace::Colorspace() {
    current_mode = ColorSpaceMode::RGB;
}

void Colorspace::cycle() {
    int next_mode = static_cast<int>(current_mode) + 1;
    current_mode = static_cast<ColorSpaceMode>(next_mode % static_cast<int>(ColorSpaceMode::COUNT));
}

void Colorspace::reset() {
    current_mode = ColorSpaceMode::RGB;
}

void Colorspace::apply(cv::Mat& mat) const {
    if (mat.empty()) return;

    switch (current_mode) {
        case ColorSpaceMode::BGR:
            cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);
            break;
        case ColorSpaceMode::HSV:
            cv::cvtColor(mat, mat, cv::COLOR_RGB2HSV);
            break;
        case ColorSpaceMode::GRAYSCALE:
            cv::cvtColor(mat, mat, cv::COLOR_RGB2GRAY);
            // Convert back to 3 channels to maintain OpenGL texture format consistency
            cv::cvtColor(mat, mat, cv::COLOR_GRAY2RGB);
            break;
        case ColorSpaceMode::RGB:
        default:
            break;
    }
}