#ifndef LOAD_LOGO_H
#define LOAD_LOGO_H

#include <string>
#include <GLFW/glfw3.h>

extern GLuint logo_texture;
extern int logo_width;
extern int logo_height;

std::string get_logo_path();
bool load_logo_texture();
// NUEVA FUNCIÓN:
void set_window_icon(GLFWwindow* window); 

#endif // LOAD_LOGO_H