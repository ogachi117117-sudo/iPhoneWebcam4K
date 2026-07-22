#pragma once

#include <cstdint>
#include <string>

extern "C" {
#include "../third_party/obs-vcam-queue/shared-memory-queue.h"
}

// Writes decoded NV12 frames into OBS Studio's virtual camera shared-memory
// queue (the same protocol OBS's own virtualcam output and pyvirtualcam use).
// Requires OBS Studio to be installed (registers the "OBS Virtual Camera"
// DirectShow source) but does NOT require OBS itself to be running, and does
// NOT require OBS's own "Start Virtual Camera" button to be active (this
// class takes the writer role instead).
class VirtualCameraOutput {
public:
    VirtualCameraOutput() = default;
    ~VirtualCameraOutput();

    // fps is used to compute the frame interval OBS expects (100ns units).
    bool start(uint32_t width, uint32_t height, double fps, std::wstring* outError);
    void stop();

    // strideY: bytes per row of the Y plane; the UV plane is assumed to use
    // the same stride (standard for Media Foundation NV12 output).
    void writeFrame(const uint8_t* yPlane, const uint8_t* uvPlane, uint32_t strideY);

    bool isActive() const { return queue_ != nullptr; }

private:
    video_queue_t* queue_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    int64_t startTick_ = 0;
};
