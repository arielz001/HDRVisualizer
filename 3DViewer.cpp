#include "3DViewer.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstdint>

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

// --- GLSL SHADERS SOURCE ---
const char* vertex_shader_source = R"(
    #version 330 core
    layout (location = 0) in vec3 aPos;
    layout (location = 1) in vec3 aColor;
    layout (location = 2) in vec3 aNormal;

    out vec3 FragPos;
    out vec3 VertColor;
    out vec3 Normal;

    uniform mat4 model;
    uniform mat4 view;
    uniform mat4 projection;

    void main() {
        FragPos = vec3(model * vec4(aPos, 1.0));
        Normal = mat3(transpose(inverse(model))) * aNormal;
        VertColor = aColor;
        gl_Position = projection * view * vec4(FragPos, 1.0);
    }
)";

const char* fragment_shader_source = R"(
    #version 330 core
    out vec4 FragColor;

    in vec3 FragPos;
    in vec3 VertColor;
    in vec3 Normal;

    void main() {
        FragColor = vec4(VertColor, 1.0);
    }
)";

// --- BASIC MATH MATRIX HELPERS ---
void IdentityMat(float m[16]) {
    std::fill(m, m+16, 0.0f); m[0]=1.f; m[5]=1.f; m[10]=1.f; m[15]=1.f;
}

void OrthographicMat(float m[16], float left, float right, float bottom, float top, float nearZ, float farZ) {
    std::fill(m, m+16, 0.0f);
    m[0] = 2.0f / (right - left);
    m[5] = 2.0f / (top - bottom);
    m[10] = -2.0f / (farZ - nearZ);
    m[12] = -(right + left) / (right - left);
    m[13] = -(top + bottom) / (top - bottom);
    m[14] = -(farZ + nearZ) / (farZ - nearZ);
    m[15] = 1.0f;
}

void LookAtMat(float m[16], float eye[3], float target[3], float up[3]) {
    float f[3] = { target[0] - eye[0], target[1] - eye[1], target[2] - eye[2] };
    float f_len = std::sqrt(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
    if (f_len > 0.0f) { f[0] /= f_len; f[1] /= f_len; f[2] /= f_len; }

    float s[3] = { f[1]*up[2] - f[2]*up[1], f[2]*up[0] - f[0]*up[2], f[0]*up[1] - f[1]*up[0] };
    float s_len = std::sqrt(s[0]*s[0] + s[1]*s[1] + s[2]*s[2]);
    if (s_len > 0.0f) { s[0] /= s_len; s[1] /= s_len; s[2] /= s_len; }

    float u[3] = { s[1]*f[2] - s[2]*f[1], s[2]*f[0] - s[0]*f[2], s[0]*f[1] - s[1]*f[0] };

    m[0] = s[0];  m[4] = s[1];  m[8] = s[2];  m[12] = -(s[0]*eye[0] + s[1]*eye[1] + s[2]*eye[2]);
    m[1] = u[0];  m[5] = u[1];  m[9] = u[2];  m[13] = -(u[0]*eye[0] + u[1]*eye[1] + u[2]*eye[2]);
    m[2] = -f[0]; m[6] = -f[1]; m[10] = -f[2]; m[14] = (f[0]*eye[0] + f[1]*eye[1] + f[2]*eye[2]);
    m[3] = 0.0f;  m[7] = 0.0f;  m[11] = 0.0f; m[15] = 1.0f;
}

Viewer3D::Viewer3D() {}

Viewer3D::~Viewer3D() {
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (shader_program) glDeleteProgram(shader_program);
    if (fbo) glDeleteFramebuffers(1, &fbo);
    if (texture_id) glDeleteTextures(1, &texture_id);
    if (rbo) glDeleteRenderbuffers(1, &rbo);
}

void Viewer3D::InitGL() {
    if (is_initialized) return;

    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertex_shader_source, NULL);
    glCompileShader(vertexShader);

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragment_shader_source, NULL);
    glCompileShader(fragmentShader);

    shader_program = glCreateProgram();
    glAttachShader(shader_program, vertexShader);
    glAttachShader(shader_program, fragmentShader);
    glLinkProgram(shader_program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    is_initialized = true;
}

bool Viewer3D::LoadModel(const std::string& filepath) {
    vertices.clear();
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;

    std::string ext = filepath.substr(filepath.find_last_of(".") + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    model_min[0] = model_min[1] = model_min[2] = 1e10f;
    model_max[0] = model_max[1] = model_max[2] = -1e10f;

    double sum_x = 0.0, sum_y = 0.0, sum_z = 0.0;
    size_t total_points = 0;

    if (ext == "obj") {
        is_point_cloud = false;
        std::vector<float> temp_positions;
        std::vector<float> temp_colors;
        std::string line;
        
        while (std::getline(file, line)) {
            std::istringstream ss(line);
            std::string prefix;
            ss >> prefix;
            if (prefix == "v") {
                float x, y, z, r=1.0f, g=1.0f, b=1.0f;
                ss >> x >> y >> z >> r >> g >> b;
                temp_positions.push_back(x);
                temp_positions.push_back(y);
                temp_positions.push_back(z);
                temp_colors.push_back(r); temp_colors.push_back(g); temp_colors.push_back(b);
                
                sum_x += x; sum_y += y; sum_z += z;
                total_points++;

                model_min[0] = std::min(model_min[0], x); model_max[0] = std::max(model_max[0], x);
                model_min[1] = std::min(model_min[1], y); model_max[1] = std::max(model_max[1], y);
                model_min[2] = std::min(model_min[2], z); model_max[2] = std::max(model_max[2], z);
            } else if (prefix == "f") {
                std::string v1, v2, v3;
                ss >> v1 >> v2 >> v3;
                auto parse_idx = [](const std::string& t) {
                    return std::stoi(t.substr(0, t.find('/'))) - 1;
                };
                int idxs[3] = { parse_idx(v1), parse_idx(v2), parse_idx(v3) };
                
                float p1[3] = {temp_positions[idxs[0]*3], temp_positions[idxs[0]*3+1], temp_positions[idxs[0]*3+2]};
                float p2[3] = {temp_positions[idxs[1]*3], temp_positions[idxs[1]*3+1], temp_positions[idxs[1]*3+2]};
                float p3[3] = {temp_positions[idxs[2]*3], temp_positions[idxs[2]*3+1], temp_positions[idxs[2]*3+2]};
                
                float u[3] = {p2[0]-p1[0], p2[1]-p1[1], p2[2]-p1[2]};
                float v[3] = {p3[0]-p1[0], p3[1]-p1[1], p3[2]-p1[2]};
                float nx = u[1]*v[2] - u[2]*v[1];
                float ny = u[2]*v[0] - u[0]*v[2];
                float nz = u[0]*v[1] - u[1]*v[0];
                float len = std::sqrt(nx*nx + ny*ny + nz*nz);
                if(len > 0) { nx/=len; ny/=len; nz/=len; }

                for(int i=0; i<3; ++i) {
                    Vertex3D v;
                    v.position[0] = temp_positions[idxs[i]*3];
                    v.position[1] = temp_positions[idxs[i]*3+1];
                    v.position[2] = temp_positions[idxs[i]*3+2];
                    v.color[0] = temp_colors[idxs[i]*3];
                    v.color[1] = temp_colors[idxs[i]*3+1];
                    v.color[2] = temp_colors[idxs[i]*3+2];
                    v.normal[0] = nx; v.normal[1] = ny; v.normal[2] = nz;
                    vertices.push_back(v);
                }
            }
        }
    } 
    else if (ext == "ply") {
        struct PlyProperty {
            std::string name;
            std::string type;
            bool is_list = false;
            std::string count_type;
            std::string item_type;
        };

        auto readScalar = [&](const std::string& type) -> double {
            if (type == "char" || type == "int8") {
                int8_t v = 0; file.read(reinterpret_cast<char*>(&v), sizeof(v)); return static_cast<double>(v);
            }
            if (type == "uchar" || type == "uint8") {
                uint8_t v = 0; file.read(reinterpret_cast<char*>(&v), sizeof(v)); return static_cast<double>(v);
            }
            if (type == "short" || type == "int16") {
                int16_t v = 0; file.read(reinterpret_cast<char*>(&v), sizeof(v)); return static_cast<double>(v);
            }
            if (type == "ushort" || type == "uint16") {
                uint16_t v = 0; file.read(reinterpret_cast<char*>(&v), sizeof(v)); return static_cast<double>(v);
            }
            if (type == "int" || type == "int32") {
                int32_t v = 0; file.read(reinterpret_cast<char*>(&v), sizeof(v)); return static_cast<double>(v);
            }
            if (type == "uint" || type == "uint32") {
                uint32_t v = 0; file.read(reinterpret_cast<char*>(&v), sizeof(v)); return static_cast<double>(v);
            }
            if (type == "float" || type == "float32") {
                float v = 0.0f; file.read(reinterpret_cast<char*>(&v), sizeof(v)); return static_cast<double>(v);
            }
            if (type == "double" || type == "float64") {
                double v = 0.0; file.read(reinterpret_cast<char*>(&v), sizeof(v)); return v;
            }
            return 0.0;
        };

        auto appendTriangle = [&](int idx0, int idx1, int idx2,
                                  const std::vector<float>& temp_positions,
                                  const std::vector<float>& temp_colors) {
            float p1[3] = {temp_positions[idx0*3], temp_positions[idx0*3+1], temp_positions[idx0*3+2]};
            float p2[3] = {temp_positions[idx1*3], temp_positions[idx1*3+1], temp_positions[idx1*3+2]};
            float p3[3] = {temp_positions[idx2*3], temp_positions[idx2*3+1], temp_positions[idx2*3+2]};

            float u[3] = {p2[0]-p1[0], p2[1]-p1[1], p2[2]-p1[2]};
            float v[3] = {p3[0]-p1[0], p3[1]-p1[1], p3[2]-p1[2]};
            float nx = u[1]*v[2] - u[2]*v[1];
            float ny = u[2]*v[0] - u[0]*v[2];
            float nz = u[0]*v[1] - u[1]*v[0];
            float len = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (len > 0) { nx /= len; ny /= len; nz /= len; }

            int idxs[3] = {idx0, idx1, idx2};
            for (int i = 0; i < 3; ++i) {
                Vertex3D vtx;
                vtx.position[0] = temp_positions[idxs[i]*3];
                vtx.position[1] = temp_positions[idxs[i]*3+1];
                vtx.position[2] = temp_positions[idxs[i]*3+2];
                vtx.color[0] = temp_colors[idxs[i]*3];
                vtx.color[1] = temp_colors[idxs[i]*3+1];
                vtx.color[2] = temp_colors[idxs[i]*3+2];
                vtx.normal[0] = nx; vtx.normal[1] = ny; vtx.normal[2] = nz;
                vertices.push_back(vtx);
            }
        };

        std::string line;
        bool header = true;
        bool is_ascii = true;
        bool is_big_endian = false;
        int num_vertices = 0;
        int num_faces = 0;
        bool in_vertex = false;
        bool in_face = false;

        std::vector<PlyProperty> vertex_props;
        std::vector<PlyProperty> face_props;

        while (std::getline(file, line)) {
            if (line.rfind("format", 0) == 0) {
                if (line.find("binary_little_endian") != std::string::npos) is_ascii = false;
                if (line.find("binary_big_endian") != std::string::npos) { is_ascii = false; is_big_endian = true; }
            } else if (line.find("element vertex") != std::string::npos) {
                std::sscanf(line.c_str(), "element vertex %d", &num_vertices);
                in_vertex = true; in_face = false;
            } else if (line.find("element face") != std::string::npos) {
                std::sscanf(line.c_str(), "element face %d", &num_faces);
                in_vertex = false; in_face = true;
            } else if (line.rfind("property", 0) == 0) {
                std::istringstream ss(line);
                std::string prop, type, name;
                ss >> prop >> type;
                PlyProperty p;
                if (type == "list") {
                    std::string count_type, item_type;
                    ss >> count_type >> item_type >> name;
                    p.is_list = true;
                    p.count_type = count_type;
                    p.item_type = item_type;
                    p.name = name;
                } else {
                    ss >> name;
                    p.type = type;
                    p.name = name;
                }
                if (in_vertex) vertex_props.push_back(p);
                if (in_face) face_props.push_back(p);
            } else if (line == "end_header") {
                header = false;
                is_point_cloud = (num_faces == 0);
                break;
            }
        }

        if (header || is_big_endian) {
            return false;
        }

        std::vector<float> temp_positions;
        std::vector<float> temp_colors;
        temp_positions.reserve(static_cast<size_t>(num_vertices) * 3);
        temp_colors.reserve(static_cast<size_t>(num_vertices) * 3);

        auto applyColorNormalize = [](float& r, float& g, float& b) {
            if (r > 1.0f || g > 1.0f || b > 1.0f) {
                r /= 255.0f; g /= 255.0f; b /= 255.0f;
            }
        };

        if (is_ascii) {
            for (int i = 0; i < num_vertices; ++i) {
                if (!std::getline(file, line)) break;
                std::istringstream ss(line);
                float x = 0.0f, y = 0.0f, z = 0.0f;
                float r = 1.0f, g = 1.0f, b = 1.0f;
                for (const auto& p : vertex_props) {
                    if (!p.is_list) {
                        double v = 0.0; ss >> v;
                        if (p.name == "x") x = static_cast<float>(v);
                        else if (p.name == "y") y = static_cast<float>(v);
                        else if (p.name == "z") z = static_cast<float>(v);
                        else if (p.name == "red" || p.name == "r") r = static_cast<float>(v);
                        else if (p.name == "green" || p.name == "g") g = static_cast<float>(v);
                        else if (p.name == "blue" || p.name == "b") b = static_cast<float>(v);
                    } else {
                        int count = 0; ss >> count;
                        for (int k = 0; k < count; ++k) { double skip = 0.0; ss >> skip; }
                    }
                }

                applyColorNormalize(r, g, b);
                temp_positions.push_back(x); temp_positions.push_back(y); temp_positions.push_back(z);
                temp_colors.push_back(r); temp_colors.push_back(g); temp_colors.push_back(b);

                sum_x += x; sum_y += y; sum_z += z;
                total_points++;

                model_min[0] = std::min(model_min[0], x); model_max[0] = std::max(model_max[0], x);
                model_min[1] = std::min(model_min[1], y); model_max[1] = std::max(model_max[1], y);
                model_min[2] = std::min(model_min[2], z); model_max[2] = std::max(model_max[2], z);

                if (is_point_cloud) {
                    Vertex3D vtx;
                    vtx.position[0] = x; vtx.position[1] = y; vtx.position[2] = z;
                    vtx.color[0] = r; vtx.color[1] = g; vtx.color[2] = b;
                    vtx.normal[0] = 0; vtx.normal[1] = 1; vtx.normal[2] = 0;
                    vertices.push_back(vtx);
                }
            }

            for (int i = 0; i < num_faces; ++i) {
                if (!std::getline(file, line)) break;
                std::istringstream ss(line);
                std::vector<int> indices;

                for (const auto& p : face_props) {
                    if (p.is_list && (p.name == "vertex_indices" || p.name == "vertex_index")) {
                        int count = 0; ss >> count;
                        indices.resize(count);
                        for (int k = 0; k < count; ++k) ss >> indices[k];
                    } else if (p.is_list) {
                        int count = 0; ss >> count;
                        for (int k = 0; k < count; ++k) { double skip = 0.0; ss >> skip; }
                    } else {
                        double skip = 0.0; ss >> skip;
                    }
                }

                if (indices.size() >= 3) {
                    for (size_t k = 1; k + 1 < indices.size(); ++k) {
                        appendTriangle(indices[0], indices[k], indices[k + 1], temp_positions, temp_colors);
                    }
                }
            }
        } else {
            for (int i = 0; i < num_vertices; ++i) {
                float x = 0.0f, y = 0.0f, z = 0.0f;
                float r = 1.0f, g = 1.0f, b = 1.0f;
                for (const auto& p : vertex_props) {
                    if (!p.is_list) {
                        double v = readScalar(p.type);
                        if (p.name == "x") x = static_cast<float>(v);
                        else if (p.name == "y") y = static_cast<float>(v);
                        else if (p.name == "z") z = static_cast<float>(v);
                        else if (p.name == "red" || p.name == "r") r = static_cast<float>(v);
                        else if (p.name == "green" || p.name == "g") g = static_cast<float>(v);
                        else if (p.name == "blue" || p.name == "b") b = static_cast<float>(v);
                    } else {
                        int count = static_cast<int>(readScalar(p.count_type));
                        for (int k = 0; k < count; ++k) { readScalar(p.item_type); }
                    }
                }

                applyColorNormalize(r, g, b);
                temp_positions.push_back(x); temp_positions.push_back(y); temp_positions.push_back(z);
                temp_colors.push_back(r); temp_colors.push_back(g); temp_colors.push_back(b);

                sum_x += x; sum_y += y; sum_z += z;
                total_points++;

                model_min[0] = std::min(model_min[0], x); model_max[0] = std::max(model_max[0], x);
                model_min[1] = std::min(model_min[1], y); model_max[1] = std::max(model_max[1], y);
                model_min[2] = std::min(model_min[2], z); model_max[2] = std::max(model_max[2], z);

                if (is_point_cloud) {
                    Vertex3D vtx;
                    vtx.position[0] = x; vtx.position[1] = y; vtx.position[2] = z;
                    vtx.color[0] = r; vtx.color[1] = g; vtx.color[2] = b;
                    vtx.normal[0] = 0; vtx.normal[1] = 1; vtx.normal[2] = 0;
                    vertices.push_back(vtx);
                }
            }

            for (int i = 0; i < num_faces; ++i) {
                std::vector<int> indices;
                for (const auto& p : face_props) {
                    if (p.is_list && (p.name == "vertex_indices" || p.name == "vertex_index")) {
                        int count = static_cast<int>(readScalar(p.count_type));
                        indices.resize(count);
                        for (int k = 0; k < count; ++k) {
                            indices[k] = static_cast<int>(readScalar(p.item_type));
                        }
                    } else if (p.is_list) {
                        int count = static_cast<int>(readScalar(p.count_type));
                        for (int k = 0; k < count; ++k) { readScalar(p.item_type); }
                    } else {
                        readScalar(p.type);
                    }
                }

                if (indices.size() >= 3) {
                    for (size_t k = 1; k + 1 < indices.size(); ++k) {
                        appendTriangle(indices[0], indices[k], indices[k + 1], temp_positions, temp_colors);
                    }
                }
            }
        }
    }
    else if (ext == "pcd") {
        std::string line;
        bool header = true;
        is_point_cloud = true;

        while (std::getline(file, line)) {
            if (header) {
                if (line.find("DATA") == 0) header = false;
                continue;
            }
            std::istringstream ss(line);
            float x, y, z;
            if (ss >> x >> y >> z) {
                Vertex3D v;
                v.position[0] = x; v.position[1] = y; v.position[2] = z;
                float r_val = 255.0f, g_val = 255.0f, b_val = 255.0f;
                if (ss >> r_val) {
                    if (ss >> g_val >> b_val) {
                        // OK
                    } else if (r_val > 255.0f || r_val < -255.0f) {
                        uint32_t rgb = *reinterpret_cast<uint32_t*>(&r_val);
                        r_val = (rgb >> 16) & 0xff;
                        g_val = (rgb >> 8) & 0xff;
                        b_val = (rgb) & 0xff;
                    } else {
                        g_val = b_val = r_val;
                    }
                }

                v.color[0] = r_val / 255.0f; v.color[1] = g_val / 255.0f; v.color[2] = b_val / 255.0f;
                v.normal[0] = 0; v.normal[1] = 1; v.normal[2] = 0; 
                vertices.push_back(v);

                sum_x += x; sum_y += y; sum_z += z;
                total_points++;

                model_min[0] = std::min(model_min[0], x); model_max[0] = std::max(model_max[0], x);
                model_min[1] = std::min(model_min[1], y); model_max[1] = std::max(model_max[1], y);
                model_min[2] = std::min(model_min[2], z); model_max[2] = std::max(model_max[2], z);
            }
        }
    }

    if (total_points > 0) {
        camera.target[0] = static_cast<float>(sum_x / total_points);
        camera.target[1] = static_cast<float>(sum_y / total_points);
        camera.target[2] = static_cast<float>(sum_z / total_points);
    } else {
        camera.target[0] = camera.target[1] = camera.target[2] = 0.0f;
    }

    InitGL();
    SetupBuffers();
    ResetCamera();
    return !vertices.empty();
}

void Viewer3D::SetupBuffers() {
    if (vao == 0) glGenVertexArrays(1, &vao);
    if (vbo == 0) glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex3D), vertices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, position));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, color));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, normal));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

void Viewer3D::ResetCamera() {
    float dx = model_max[0] - model_min[0];
    float dy = model_max[1] - model_min[1];
    float dz = model_max[2] - model_min[2];
    
    float max_dim = std::max({dx, dy, dz});
    camera.radius = std::max(max_dim * 1.2f, 0.01f);
    
    camera.azimuth = 0.785f; 
    camera.polar = 1.2f;     
}

void Viewer3D::UpdateCamera(ImVec2 size) {
    ImGuiIO& io = ImGui::GetIO();
    bool is_view_hovered = ImGui::IsItemHovered();

    if (is_view_hovered) {
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            show_wireframe = !show_wireframe;
        }

        if (io.MouseWheel != 0.0f) {
            camera.radius -= io.MouseWheel * (camera.radius * 0.1f);
            camera.radius = std::clamp(camera.radius, 0.001f, 1000.0f);
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            camera.is_rotating = true;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            camera.is_panning = true;
        }
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) camera.is_rotating = false;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) camera.is_panning = false;

    if (camera.is_rotating) {
        camera.azimuth -= io.MouseDelta.x * 0.005f;
        camera.polar -= io.MouseDelta.y * 0.005f;
        camera.polar = std::clamp(camera.polar, 0.005f, 3.14159f - 0.005f);
    }

    if (camera.is_panning) {
        float factor = camera.radius * 0.002f;
        camera.target[0] -= io.MouseDelta.x * factor * std::cos(camera.azimuth);
        camera.target[2] += io.MouseDelta.x * factor * std::sin(camera.azimuth);
        camera.target[1] += io.MouseDelta.y * factor;
    }
}

void Viewer3D::RenderView(ImVec2 view_size) {
    if (view_size.x < 50 || view_size.y < 50) return;

    int w = static_cast<int>(view_size.x);
    int h = static_cast<int>(view_size.y);
    if (w != last_width || h != last_height) {
        last_width = w; last_height = h;

        if (fbo == 0) {
            glGenFramebuffers(1, &fbo);
            glGenTextures(1, &texture_id);
            glGenRenderbuffers(1, &rbo);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glBindTexture(GL_TEXTURE_2D, texture_id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_id, 0);

        glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    glClearColor(0.15f, 0.16f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    if (vao != 0 && !vertices.empty()) {
        glUseProgram(shader_program);

        float camX = camera.target[0] + camera.radius * std::sin(camera.polar) * std::sin(camera.azimuth);
        float camY = camera.target[1] + camera.radius * std::cos(camera.polar);
        float camZ = camera.target[2] + camera.radius * std::sin(camera.polar) * std::cos(camera.azimuth);

        float up[3] = { 0.0f, 1.0f, 0.0f };
        if (camera.polar < 0.01f || camera.polar > 3.13f) {
            up[0] = -std::sin(camera.azimuth);
            up[1] = 0.0f;
            up[2] = -std::cos(camera.azimuth);
        }

        float aspect = static_cast<float>(w) / static_cast<float>(h);
        float orthoSize = camera.radius;

        float proj[16];
        OrthographicMat(proj, -orthoSize * aspect, orthoSize * aspect, -orthoSize, orthoSize, -1000.0f, 1000.0f);
        
        int projLoc = glGetUniformLocation(shader_program, "projection");
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, proj);

        float model[16]; IdentityMat(model);
        int modelLoc = glGetUniformLocation(shader_program, "model");
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, model);

        float view[16];
        float eye[3] = { camX, camY, camZ };
        LookAtMat(view, eye, camera.target, up);
        
        int viewLoc = glGetUniformLocation(shader_program, "view");
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, view);

        glBindVertexArray(vao);
        
        if (is_point_cloud) {
            glPointSize(2.0f);
            glDrawArrays(GL_POINTS, 0, (int)vertices.size());
        } else {
            // rendering
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glDrawArrays(GL_TRIANGLES, 0, (int)vertices.size());

            // draw wireframe
            if (show_wireframe) {
                glEnable(GL_POLYGON_OFFSET_LINE);
                glPolygonOffset(-1.0f, -1.0f);

                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                glLineWidth(1.5f);
                glDisableVertexAttribArray(1); 
                glVertexAttrib3f(1, 0.05f, 0.05f, 0.05f);
                glDrawArrays(GL_TRIANGLES, 0, (int)vertices.size());
                glEnableVertexAttribArray(1);
                glDisable(GL_POLYGON_OFFSET_LINE);
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            }
        }
        glBindVertexArray(0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);

    ImGui::Image((void*)(intptr_t)texture_id, view_size, ImVec2(0, 1), ImVec2(1, 0));
    UpdateCamera(view_size);
}

void Viewer3D::RenderUI(const char* window_name, bool* p_open) {
    ImGui::Begin(window_name, p_open);
    RenderView(ImGui::GetContentRegionAvail());
    ImGui::End();
}