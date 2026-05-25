#ifndef EVENTS_H
#define EVENTS_H

#include <string>
#include <memory>
#include <opencv2/opencv.hpp>
#include <dv-processing/io/mono_camera_recording.hpp>
#include <dv-processing/core/event.hpp>

class AedatReader {
public:
    AedatReader();
    AedatReader(const std::string& path);
    ~AedatReader();

    // Constructores de movimiento necesarios para evitar errores en AppContext y std::vector
    AedatReader(AedatReader&& other) noexcept;
    AedatReader& operator=(AedatReader&& other) noexcept;

    // Deshabilitar copia
    AedatReader(const AedatReader&) = delete;
    AedatReader& operator=(const AedatReader&) = delete;

    bool Open(const std::string& path);
    void Close();
    bool IsOpen() const { return is_open; }

    bool ReadNextFrame(cv::Mat& frame, int accumulation_mode, int slider_value);

    // Getters públicos para el Reset Zoom de main.cpp
    int GetWidth() const { return width; }
    int GetHeight() const { return height; }

private:
    std::unique_ptr<dv::io::MonoCameraRecording> reader;
    std::string file_path;
    bool is_open;
    int width;
    int height;

    // Búfer persistente de eventos para que el slider controle la ventana real
    dv::EventStore accumulated_events; 
};

#endif // EVENTS_H