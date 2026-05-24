#include "Video.h"
#include <imgui.h>

VideoPlayer::VideoPlayer()
    : m_textureId(0)
    , m_isPlaying(false)
    , m_isLoaded(false)
    , m_fps(0.0)
    , m_totalFrames(0)
    , m_currentFrameIdx(0)
    , m_lastFrameTime(0.0)
{
}

VideoPlayer::~VideoPlayer() {
    releaseResources();
}

void VideoPlayer::releaseResources() {
    m_capture.release();
    m_frame.release();
    if (m_textureId != 0) {
        // dynamic code depending on your GL loader (glDeleteTextures)
        // glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
    }
    m_isLoaded = false;
    m_isPlaying = false;
}

bool VideoPlayer::loadVideo(const std::string& filepath) {
    releaseResources();

    // m_capture.open(filepath);
    m_capture.open(filepath, cv::CAP_AVFOUNDATION);
    if (!m_capture.isOpened()) {
        return false;
    }

    m_fps = m_capture.get(cv::CAP_PROP_FPS);
    m_totalFrames = static_cast<int>(m_capture.get(cv::CAP_PROP_FRAME_COUNT));
    m_currentFrameIdx = 0;

    // Read the very first frame to initialize texture container
    if (m_capture.read(m_frame)) {
        cv::cvtColor(m_frame, m_frame, cv::COLOR_BGR2RGBA);
        updateTexture();
        m_isLoaded = true;
    }

    m_lastFrameTime = ImGui::GetTime();
    return m_isLoaded;
}

void VideoPlayer::update() {
    if (!m_isLoaded || !m_isPlaying) return;

    double currentTime = ImGui::GetTime();
    double timePerFrame = 1.0 / m_fps;

    // Check if enough time has passed to advance to the next frame
    if (currentTime - m_lastFrameTime >= timePerFrame) {
        if (m_capture.read(m_frame)) {
            // OpenCV reads BGR, ImGui needs RGBA (or RGB depending on platform)
            cv::cvtColor(m_frame, m_frame, cv::COLOR_BGR2RGBA);
            updateTexture();
            m_currentFrameIdx = static_cast<int>(m_capture.get(cv::CAP_PROP_POS_FRAMES));
            m_lastFrameTime = currentTime;
        } else {
            // Video ended -> loop or stop
            m_capture.set(cv::CAP_PROP_POS_FRAMES, 0);
            m_currentFrameIdx = 0;
            m_isPlaying = false; 
        }
    }
}

void VideoPlayer::updateTexture() {
    if (m_frame.empty()) return;

    // Lazy initialization of OpenGL texture ID
    if (m_textureId == 0) {
        // This relies on standard OpenGL symbols being loaded globally
        // glGenTextures(1, &m_textureId);
    }

    // Dynamic bindings matching your graphics backend template setup
    // glBindTexture(GL_TEXTURE_2D, m_textureId);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    // glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_frame.cols, m_frame.rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_frame.data);
}

void VideoPlayer::renderUI() {
    if (!m_isLoaded) {
        ImGui::Text("No video loaded.");
        return;
    }

    // Render viewport image dynamically sized
    float aspectRatio = static_cast<float>(m_frame.cols) / m_frame.rows;
    float viewWidth = ImGui::GetContentRegionAvail().x;
    float viewHeight = viewWidth / aspectRatio;
    
    if (m_textureId != 0) {
        ImGui::Image((void*)(intptr_t)m_textureId, ImVec2(viewWidth, viewHeight));
    }

    ImGui::Spacing();

    // Dynamic Play/Pause icon button morph
    const char* buttonLabel = m_isPlaying ? "|| Pause" : "> Play";
    if (ImGui::Button(buttonLabel, ImVec2(80, 25))) {
        m_isPlaying = !m_isPlaying;
        if (m_isPlaying) {
            m_lastFrameTime = ImGui::GetTime(); // Reset delta context
        }
    }

    ImGui::SameLine();
    
    // Timeline slider control
    int progress = m_currentFrameIdx;
    ImGui::PushItemWidth(viewWidth - 90.0f);
    if (ImGui::SliderInt("##timeline", &progress, 0, m_totalFrames - 1)) {
        m_capture.set(cv::CAP_PROP_POS_FRAMES, progress);
        m_currentFrameIdx = progress;
        if (m_capture.read(m_frame)) {
            cv::cvtColor(m_frame, m_frame, cv::COLOR_BGR2RGBA);
            updateTexture();
        }
    }
    ImGui::PopItemWidth();
}