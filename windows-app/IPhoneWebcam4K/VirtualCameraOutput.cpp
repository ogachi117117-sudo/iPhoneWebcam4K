#include "VirtualCameraOutput.h"

#include <windows.h>

VirtualCameraOutput::~VirtualCameraOutput() {
    stop();
}

bool VirtualCameraOutput::start(uint32_t width, uint32_t height, double fps, std::wstring* outError) {
    stop();

    uint64_t interval100ns = static_cast<uint64_t>(10'000'000.0 / fps);
    queue_ = video_queue_create(width, height, interval100ns);
    if (!queue_) {
        if (outError) {
            *outError = L"OBS仮想カメラの共有メモリを作成できません。"
                        L"OBS Studioがインストールされているか、既に他のアプリが仮想カメラを使用中でないか確認してください。";
        }
        return false;
    }

    width_ = width;
    height_ = height;

    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    startTick_ = li.QuadPart;
    return true;
}

void VirtualCameraOutput::stop() {
    if (queue_) {
        video_queue_close(queue_);
        queue_ = nullptr;
    }
}

void VirtualCameraOutput::writeFrame(const uint8_t* yPlane, const uint8_t* uvPlane, uint32_t strideY) {
    if (!queue_) return;

    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    uint64_t elapsedTicks = static_cast<uint64_t>(now.QuadPart - startTick_);
    uint64_t timestamp100ns = (elapsedTicks * 10'000'000ULL) / static_cast<uint64_t>(freq.QuadPart);

    uint8_t* data[2] = { const_cast<uint8_t*>(yPlane), const_cast<uint8_t*>(uvPlane) };
    uint32_t linesize[2] = { strideY, strideY };

    video_queue_write(queue_, data, linesize, timestamp100ns);
}
