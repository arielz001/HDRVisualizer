#ifndef POLARIZED_H
#define POLARIZED_H

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

// Struct to store Stokes parameters and Polarization results
struct PolarizationResult {
    cv::Mat S0, S1, S2;
    cv::Mat Intensity;
    cv::Mat AoLP; // Angle of Linear Polarization
    cv::Mat DoLP; // Degree of Linear Polarization
};

// Generates the 2x2 grid mosaic from RAW file
cv::Mat dePolarize(const std::string& file_path);

// Computes Stokes vector, AoLP, and DoLP from the 4 extracted BGR/Grayscale channels
PolarizationResult computePolarization(const std::vector<cv::Mat>& bgr_channels);

// Helper to visualize AoLP and DoLP as RGB maps for your HDR Viewer
cv::Mat getPolarizationVisuals(const PolarizationResult& polar);

#endif // POLARIZED_H