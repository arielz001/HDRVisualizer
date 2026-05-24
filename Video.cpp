#include "Video.h"

VideoPlayer::VideoPlayer() : m_isPlaying(false), m_isLoaded(false) {}

VideoPlayer::~VideoPlayer() { 
    release(); 
}

bool VideoPlayer::loadVideo(const std::string& filepath) {
    release();
    m_capture.open(filepath);
    if (m_capture.isOpened()) {
        m_isLoaded = true;
        m_isPlaying = true;
        return true;
    }
    return false;
}

void VideoPlayer::release() {
    if (m_capture.isOpened()) {
        m_capture.release();
    }
    m_isLoaded = false;
    m_isPlaying = false;
}

bool VideoPlayer::getNextFrame(cv::Mat& frame) {
    if (!m_isLoaded || !m_isPlaying) return false;
    
    m_capture >> frame;
    if (frame.empty()) {
        m_capture.set(cv::CAP_PROP_POS_FRAMES, 0);
        m_capture >> frame;
    }
    return !frame.empty();
}

bool VideoPlayer::seekTo(float progress, cv::Mat& frame) {
    if (!m_isLoaded) return false;
    int total_frames = m_capture.get(cv::CAP_PROP_FRAME_COUNT);
    m_capture.set(cv::CAP_PROP_POS_FRAMES, (int)(progress * total_frames));
    m_capture >> frame;
    return !frame.empty();
}

bool VideoPlayer::isLoaded() const { return m_isLoaded; }
bool VideoPlayer::isPlaying() const { return m_isPlaying; }
void VideoPlayer::setPlaying(bool playing) { m_isPlaying = playing; }

float VideoPlayer::getProgress() const {
    if (!m_isLoaded) return 0.0f;
    float total = m_capture.get(cv::CAP_PROP_FRAME_COUNT);
    float current = m_capture.get(cv::CAP_PROP_POS_FRAMES);
    return total > 0 ? (current / total) : 0.0f;
}