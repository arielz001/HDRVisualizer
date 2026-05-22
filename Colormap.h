#pragma once
#include <opencv2/opencv.hpp>
#include <array>

class Colormap {
public:
    std::array<float, 3> center;
    float radius;
    bool is_active;

    Colormap();

    void autoCenterAndRadius(float min, float max);
    void reset();
    cv::Mat apply(const cv::Mat& input_hdr) const;

private:
    std::array<float, 3> getScale() const;
    std::array<float, 3> getBias() const;
};