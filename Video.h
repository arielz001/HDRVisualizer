#pragma once
#include <opencv2/opencv.hpp>
#include <string>

class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    bool loadVideo(const std::string& filepath);
    void release();

    bool getNextFrame(cv::Mat& frame);
    bool seekTo(float progress, cv::Mat& frame);
    
    bool isLoaded() const;
    bool isPlaying() const;
    void setPlaying(bool playing);
    
    float getProgress() const;
    
private:
    cv::VideoCapture m_capture;
    bool m_isPlaying;
    bool m_isLoaded;
};