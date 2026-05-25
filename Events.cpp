#include "Events.h"

AedatReader::AedatReader() : file_path(""), is_open(false), width(1280), height(720) {}

AedatReader::AedatReader(const std::string& path) : is_open(false), width(1280), height(720) {
    Open(path);
}

AedatReader::~AedatReader() {
    Close();
}

AedatReader::AedatReader(AedatReader&& other) noexcept {
    reader = std::move(other.reader);
    file_path = std::move(other.file_path);
    is_open = other.is_open;
    width = other.width;
    height = other.height;
    accumulated_events = std::move(other.accumulated_events);
    other.is_open = false;
}

AedatReader& AedatReader::operator=(AedatReader&& other) noexcept {
    if (this != &other) {
        Close();
        reader = std::move(other.reader);
        file_path = std::move(other.file_path);
        is_open = other.is_open;
        width = other.width;
        height = other.height;
        accumulated_events = std::move(other.accumulated_events);
        other.is_open = false;
    }
    return *this;
}

bool AedatReader::Open(const std::string& path) {
    try {
        file_path = path;
        reader = std::make_unique<dv::io::MonoCameraRecording>(path);
        if (reader && reader->isRunning()) {
            auto sensorResolution = reader->getEventResolution();
            if (sensorResolution.has_value()) {
                width = sensorResolution->width;
                height = sensorResolution->height;
            } else {
                width = 1280;
                height = 720;
            }
            is_open = true;
            accumulated_events = dv::EventStore();
            return true;
        }
    } catch (...) {
        is_open = false;
    }
    return false;
}

void AedatReader::Close() {
    is_open = false;
    reader.reset();
    accumulated_events = dv::EventStore();
}

bool AedatReader::ReadNextFrame(cv::Mat& frame, int accumulation_mode, int slider_value) {
    if (!is_open || !reader || !reader->isRunning()) return false;

    while (accumulated_events.size() < static_cast<size_t>(slider_value) && reader->isRunning()) {
        auto batch = reader->getNextEventBatch();
        if (!batch || batch->isEmpty()) {
            break; 
        }
        accumulated_events.add(*batch);
    }

    if (accumulated_events.isEmpty()) return false;

    // Limpiar el frame reteniendo memoria
    if (frame.empty() || frame.cols != width || frame.rows != height) {
        frame = cv::Mat::zeros(height, width, CV_8UC3);
    } else {
        frame = cv::Scalar(0, 0, 0);
    }

    size_t events_to_consume = 0;

    if (accumulation_mode == 0) {
        events_to_consume = std::min(accumulated_events.size(), static_cast<size_t>(slider_value));

        for (size_t i = 0; i < events_to_consume; ++i) {
            const auto& ev = accumulated_events[i];
            if (ev.x() < width && ev.y() < height) {
                frame.at<cv::Vec3b>(ev.y(), ev.x()) = ev.polarity() ? cv::Vec3b(255, 0, 0) : cv::Vec3b(0, 0, 255);
            }
        }
    } 
    else {
        int64_t start_t = accumulated_events.getLowestTime();
        
        for (size_t i = 0; i < accumulated_events.size(); ++i) {
            const auto& ev = accumulated_events[i];
            if ((ev.timestamp() - start_t) > slider_value) {
                break;
            }
            events_to_consume++;

            if (ev.x() < width && ev.y() < height) {
                frame.at<cv::Vec3b>(ev.y(), ev.x()) = ev.polarity() ? cv::Vec3b(255, 0, 0) : cv::Vec3b(0, 0, 255);
            }
        }
        
        if (events_to_consume == 0 && accumulated_events.size() > 0) {
            events_to_consume = 1;
        }
    }

    if (events_to_consume > 0) {
        accumulated_events = accumulated_events.slice(static_cast<int64_t>(events_to_consume));
    }

    return true;
}