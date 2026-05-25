#include "Utils.h"
#include <filesystem>
#include <algorithm>

GLuint matToTexture(const cv::Mat& img)
{
    cv::Mat rgba;
    if (img.channels() == 1)
        cv::cvtColor(img, rgba, cv::COLOR_GRAY2RGBA);
    else
        cv::cvtColor(img, rgba, cv::COLOR_BGR2RGBA);

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 rgba.cols, rgba.rows, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba.data);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    return tex;
}

std::vector<std::string> getImages(const std::string& folder)
{
    std::vector<std::string> imgs;
    for (auto& e : std::filesystem::directory_iterator(folder))
    {
        std::string p = e.path().string();
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
            ext == ".exr" || ext == ".hdr" || ext == ".raw" || 
            ext == ".avi" || ext == ".mp4" || ext == ".mkv" ||
             ext == ".obj" || ext == ".ply" || ext == ".pcd" ||
             ext == ".mov")
            imgs.push_back(p);
    }
    std::sort(imgs.begin(), imgs.end());
    return imgs;
}