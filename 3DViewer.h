#pragma once
#include <string>
#include <vector>
#include "imgui.h"

// Vertex structure for GPU buffers
struct Vertex3D {
    float position[3]; // X, Y, Z
    float color[3];    // R, G, B
    float normal[3];   // NX, NY, NZ
    float texcoord[2]; // U, V
};

// Camera state for Orbit controls
struct Camera3D {
    float radius = 5.0f;
    float azimuth = 0.0f;   // Horizontal rotation
    float polar = 0.785f;    // Vertical rotation
    float target[3] = {0.0f, 0.0f, 0.0f};
    
    bool is_rotating = false;
    bool is_panning = false;
    float pan_x = 0.0f;
    float pan_y = 0.0f;
};

class Viewer3D {
public:
    Viewer3D();
    ~Viewer3D();

    // Load .obj, .ply, or .pcd file
    bool LoadModel(const std::string& filepath);
    
    // Render the model inside the current ImGui region
    void RenderView(ImVec2 view_size);

    // Render ImGui viewport and window
    void RenderUI(const char* window_name, bool* p_open = nullptr);

    // Center camera on loaded model
    void ResetCamera();

private:
    void InitGL();
    void SetupBuffers();
    void UpdateCamera(ImVec2 size);
    void RenderScene();

    // OpenGL handles
    unsigned int vao = 0, vbo = 0;
    unsigned int shader_program = 0;
    unsigned int diffuse_texture = 0;
    unsigned int fbo = 0, rbo = 0, texture_id = 0;
    
    int last_width = 0, last_height = 0;
    bool is_initialized = false;

    // Model data
    std::vector<Vertex3D> vertices;
    bool is_point_cloud = false; 
    bool show_wireframe = true;
    bool has_texture = false;
    float model_min[3] = {0,0,0};
    float model_max[3] = {0,0,0};

    Camera3D camera;
};