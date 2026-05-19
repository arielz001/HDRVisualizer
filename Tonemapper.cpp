#include "Tonemapper.h"
#include <opencv2/photo.hpp>

cv::Mat applyTonemap(const cv::Mat& in,
                     int mode,
                     float r_gamma, float intensity, float light_adapt, float color_adapt,
                     float d_gamma, float d_saturation, float d_bias,
                     float m_gamma, float m_scale, float m_saturation)
{
    cv::Mat img;

    if (in.channels() == 1)
        cv::cvtColor(in, img, cv::COLOR_GRAY2BGR);
    else if (in.channels() == 4)
        cv::cvtColor(in, img, cv::COLOR_BGRA2BGR);
    else
        img = in.clone();

    cv::Mat f;
    img.convertTo(f, CV_32FC3);
    f /= 255.0f;

    cv::Mat out;

    if (mode == 1) {
        auto tm = cv::createTonemapReinhard(r_gamma, intensity, light_adapt, color_adapt);
        cv::Mat hdr;
        tm->process(f, hdr);
        hdr.convertTo(out, CV_8UC3, 255.0);
        return out;
    }
    if (mode == 2) {
        auto tm = cv::createTonemapDrago(d_gamma, d_saturation, d_bias);
        cv::Mat hdr;
        tm->process(f, hdr);
        hdr.convertTo(out, CV_8UC3, 255.0);
        return out;
    }
    if (mode == 3) {
        try {
            auto tm = cv::createTonemapMantiuk(m_gamma, m_scale, m_saturation);
            cv::Mat hdr;
            tm->process(f, hdr);
            hdr.convertTo(out, CV_8UC3, 255.0);
            return out;
        } catch (...) {
            auto tm = cv::createTonemapDrago(1.0f, 1.0f, 0.85f);
            cv::Mat hdr;
            tm->process(f, hdr);
            hdr.convertTo(out, CV_8UC3, 255.0);
            return out;
        }
    }

    return in;
}