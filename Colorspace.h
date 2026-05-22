#pragma once
#include <opencv2/opencv.hpp>

enum class ColorSpaceMode {
    RGB = 0,
    BGR,
    HSV,
    GRAYSCALE,
    COUNT // Helper to cycle back to 0
};

class Colorspace {
public:
    Colorspace();
    ~Colorspace() = default;

    // Cycles to the next color space in the enum loop
    void cycle();

    // Resets the color space back to RGB
    void reset();

    // Applies the current color space transformation to the image matrix
    void apply(cv::Mat& mat) const;

    // Returns the current active mode
    ColorSpaceMode getMode() const { return current_mode; }


private:
    ColorSpaceMode current_mode;
};