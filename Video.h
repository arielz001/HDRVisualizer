#pragma once
#include <opencv2/opencv.hpp>
#include <string>

#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    bool loadVideo(const std::string& filepath);
    void update(); 
    void renderUI(); 

private:
    void releaseResources();
    void updateTexture();

    cv::VideoCapture m_capture;
    cv::Mat m_frame;
    
    GLuint m_textureId; 

    bool m_isPlaying;
    bool m_isLoaded;
    double m_fps;
    int m_totalFrames;
    int m_currentFrameIdx;

    double m_lastFrameTime;
};