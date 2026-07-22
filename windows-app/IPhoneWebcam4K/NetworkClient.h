#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

// TCP client that connects to the iPhone app (StreamServer.swift) and parses
// the wire protocol defined in ios-app/iPhoneWebcam4K/WireProtocol.swift:
//   [4 bytes BE length][1 byte type][payload]
//   type 0x01 = parameter sets: [2B BE spsLen][sps][2B BE ppsLen][pps]
//   type 0x02 = video sample: raw AVCC data (4-byte-length-prefixed NALs)
class NetworkClient {
public:
    using ParameterSetsCallback = std::function<void(const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps)>;
    using VideoSampleCallback = std::function<void(const std::vector<uint8_t>& avccData)>;
    using StatusCallback = std::function<void(const std::wstring& status)>;

    NetworkClient(std::wstring host, uint16_t port);
    ~NetworkClient();

    void setParameterSetsCallback(ParameterSetsCallback cb) { paramSetsCb_ = std::move(cb); }
    void setVideoSampleCallback(VideoSampleCallback cb) { videoSampleCb_ = std::move(cb); }
    void setStatusCallback(StatusCallback cb) { statusCb_ = std::move(cb); }

    void start();
    void stop();

private:
    void threadProc();
    bool connectOnce(SOCKET& outSock);
    bool readExact(SOCKET sock, uint8_t* buffer, size_t length);
    void reportStatus(const std::wstring& status);

    std::wstring host_;
    uint16_t port_;
    std::atomic<bool> running_{false};
    std::thread worker_;

    ParameterSetsCallback paramSetsCb_;
    VideoSampleCallback videoSampleCb_;
    StatusCallback statusCb_;
};
