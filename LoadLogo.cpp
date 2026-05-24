#include "LoadLogo.h"
#include <iostream>
#include <filesystem>
#include <opencv2/opencv.hpp>

// Incluir OpenGL de manera correcta dependiendo del sistema operativo
#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#include <mach-o/dyld.h>
#else
#include <GL/gl.h>
#endif

GLuint logo_texture = 0;
int logo_width = 0;
int logo_height = 0;

std::string get_logo_path() {
    std::filesystem::path exe_dir;

#if defined(__APPLE__)
    char path[1024];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        exe_dir = std::filesystem::path(path).parent_path();
    } else {
        exe_dir = std::filesystem::current_path();
    }
#else
    exe_dir = std::filesystem::read_symlink("/proc/self/exe").parent_path();
#endif

    // Buscar rutas en modo desarrollo (relativas al binario compilado)
    std::filesystem::path dev_path_1 = exe_dir / "assets" / "logo.png";
    std::filesystem::path dev_path_2 = exe_dir / ".." / ".." / "assets" / "logo.png";
    std::filesystem::path dev_path_3 = exe_dir / ".." / "assets" / "logo.png";

    if (std::filesystem::exists(dev_path_1)) return dev_path_1.string();
    if (std::filesystem::exists(dev_path_2)) return dev_path_2.string();
    if (std::filesystem::exists(dev_path_3)) return dev_path_3.string();

    // Buscar ruta en modo instalación global (ej: /usr/local/share/riv/)
    std::filesystem::path installed_path = exe_dir / ".." / "share" / "riv" / "logo.png";
    if (std::filesystem::exists(installed_path)) {
        return installed_path.string();
    }

#ifdef DATA_DIR
    std::filesystem::path cmake_path = std::filesystem::path(DATA_DIR) / "logo.png";
    if (std::filesystem::exists(cmake_path)) {
        return cmake_path.string();
    }
#endif

    if (std::filesystem::exists("assets/logo.png")) return "assets/logo.png";
    if (std::filesystem::exists("logo.png")) return "logo.png";

    return ""; 
}

bool load_logo_texture() {
    std::string path = get_logo_path();
    if (path.empty()) {
        std::cerr << "[Error] Logo file not found." << std::endl;
        return false;
    }

    cv::Mat img = cv::imread(path, cv::IMREAD_UNCHANGED); 
    if (img.empty()) {
        std::cerr << "[Error] Failed to decode logo at: " << path << std::endl;
        return false;
    }

    logo_width = img.cols;
    logo_height = img.rows;

    cv::Mat img_rgba;
    if (img.channels() == 4) {
        cv::cvtColor(img, img_rgba, cv::COLOR_BGRA2RGBA);
    } else {
        cv::cvtColor(img, img_rgba, cv::COLOR_BGR2RGBA);
    }

    glGenTextures(1, &logo_texture);
    glBindTexture(GL_TEXTURE_2D, logo_texture);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, logo_width, logo_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img_rgba.data);
    
    return true;
}

void set_window_icon(GLFWwindow* window) {
    std::string path = get_logo_path();
    if (path.empty()) return;

    static cv::Mat img_rgba;
    
    cv::Mat img = cv::imread(path, cv::IMREAD_UNCHANGED);
    if (img.empty()) return;

    if (img.channels() == 4) {
        cv::cvtColor(img, img_rgba, cv::COLOR_BGRA2RGBA);
    } else {
        cv::cvtColor(img, img_rgba, cv::COLOR_BGR2RGBA);
    }

    GLFWimage icon[1];
    icon[0].width = img_rgba.cols;
    icon[0].height = img_rgba.rows;
    icon[0].pixels = img_rgba.data; 

    glfwSetWindowIcon(window, 1, icon);
}