#include "Polarized.h"
#include <iostream>
#include <fstream>
#include <cmath>

cv::Mat dePolarize(const std::string& file_path)
{
    const int channels = 4;

    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return cv::Mat();
    
    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    int total_pixels = file_size / channels;
    int width = 0, height = 0;

    // Dynamic resolution search
    int start_h = static_cast<int>(std::sqrt(total_pixels / 1.8));
    int end_h = static_cast<int>(std::sqrt(total_pixels));
    for (int h = start_h; h <= end_h; ++h) {
        if (total_pixels % h == 0) {
            height = h;
            width = total_pixels / h;
            break; 
        }
    }
    if (width == 0 || height == 0) {
        height = static_cast<int>(std::sqrt(total_pixels));
        width = total_pixels / height;
    }

    std::vector<uchar> buffer(file_size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) return cv::Mat();
    file.close();

    cv::Mat raw_image(height, width, CV_8UC4, buffer.data());

    std::vector<cv::Mat> polarization_channels;
    cv::split(raw_image, polarization_channels);

    std::vector<cv::Mat> bgr_channels(4);
    for (int i = 0; i < channels; ++i) {
        cv::cvtColor(polarization_channels[i], bgr_channels[i], cv::COLOR_BayerRG2RGB);
    }

    // Create 2x2 Mosaic Grid
    // Top row: 0° | 45°
    cv::Mat top_row;
    cv::hconcat(bgr_channels[0], bgr_channels[1], top_row);

    // Bottom row: 90° | 135°
    cv::Mat bottom_row;
    cv::hconcat(bgr_channels[2], bgr_channels[3], bottom_row);

    // Final merge vertical
    cv::Mat output_mosaic;
    cv::vconcat(top_row, bottom_row, output_mosaic);

    return output_mosaic;
}

PolarizationResult computePolarization(const std::vector<cv::Mat>& bgr_channels)
{
    PolarizationResult res;
    std::vector<cv::Mat> gray(4);

    // Convert BGR channels to float32 grayscale for calculation mathematical safety
    for (int i = 0; i < 4; ++i) {
        cv::Mat g;
        cv::cvtColor(bgr_channels[i], g, cv::COLOR_RGB2GRAY);
        g.convertTo(gray[i], CV_32FC1, 1.0 / 255.0);
    }

    // Compute Stokes parameters
    res.S0 = gray[0] + gray[2];          // I_0 + I_90
    res.S1 = gray[0] - gray[2];          // I_0 - I_90
    res.S2 = gray[1] - gray[3];          // I_45 - I_135

    res.Intensity = res.S0 * 0.5f;

    // Compute AoLP (Angle of Linear Polarization): 0.5 * arctan2(S2, S1)
    res.AoLP = cv::Mat::zeros(res.S0.size(), CV_32FC1);
    for (int r = 0; r < res.S0.rows; ++r) {
        for (int c = 0; c < res.S0.cols; ++c) {
            res.AoLP.at<float>(r, c) = 0.5f * std::atan2(res.S2.at<float>(r, c), res.S1.at<float>(r, c));
        }
    }

    // Compute DoLP (Degree of Linear Polarization): sqrt(S1^2 + S2^2) / (S0 + epsilon)
    cv::Mat S1_sq, S2_sq, magnitude;
    cv::multiply(res.S1, res.S1, S1_sq);
    cv::multiply(res.S2, res.S2, S2_sq);
    cv::sqrt(S1_sq + S2_sq, magnitude);
    
    cv::Mat magnitude_clipped;
    cv::divide(magnitude, res.S0 + 1e-6f, magnitude_clipped);
    cv::threshold(magnitude_clipped, res.DoLP, 1.0, 1.0, cv::THRESH_TRUNC); // clip upper to 1.0
    res.DoLP = cv::max(0.0f, res.DoLP);                                     // clip lower to 0.0

    return res;
}

cv::Mat getPolarizationVisuals(const PolarizationResult& polar)
{
    // DoLP is already 0.0 to 1.0 float, map to a 3-channel image to display
    cv::Mat dolp_rgb;
    cv::cvtColor(polar.DoLP, dolp_rgb, cv::COLOR_GRAY2RGB);

    // Normalize AoLP [-pi/2, pi/2] to [0.0, 1.0] for standard rendering visualization
    cv::Mat aolp_norm = (polar.AoLP + (M_PI / 2.0f)) / M_PI;
    cv::Mat aolp_rgb;
    cv::cvtColor(aolp_norm, aolp_rgb, cv::COLOR_GRAY2RGB);

    // Combine them side by side (Left: DoLP, Right: AoLP)
    cv::Mat polarization_viewport;
    cv::hconcat(dolp_rgb, aolp_rgb, polarization_viewport);

    return polarization_viewport; 
}