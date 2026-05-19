#pragma once
#include <opencv2/opencv.hpp>
#include <GLFW/glfw3.h>
#include <vector>
#include <string>

// Convierte un cv::Mat a una textura de OpenGL
GLuint matToTexture(const cv::Mat& img);

// Obtiene todas las imágenes válidas de una carpeta
std::vector<std::string> getImages(const std::string& folder);