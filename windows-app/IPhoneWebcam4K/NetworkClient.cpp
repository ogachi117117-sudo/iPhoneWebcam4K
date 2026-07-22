#include "NetworkClient.h"

#include <chrono>

#pragma comment(lib, "Ws2_32.lib")

namespace {
constexpr int kConnectRetryDelayMs = 2000;
constexpr int kRecvTimeoutMs = 5000;
}

NetworkClient::NetworkClient(std::wstring host, uint16_t port)
    : host_(std::move(host)), port_(port) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

NetworkClient::~NetworkClient() {
    stop();
    WSACleanup();
}

void NetworkClient::start() {
    if (running_.exchange(true)) return;
    worker_ = std::thread(&NetworkClient::threadProc, this);
}

void NetworkClient::stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
}

void NetworkClient::reportStatus(const std::wstring& status) {
    if (statusCb_) statusCb_(status);
}

bool NetworkClient::connectOnce(SOCKET& outSock) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return false;

    DWORD timeout = kRecvTimeoutMs;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    std::string hostUtf8(host_.begin(), host_.end());
    if (InetPtonA(AF_INET, hostUtf8.c_str(), &addr.sin_addr) != 1) {
        closesocket(sock);
        return false;
    }

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return false;
    }

    outSock = sock;
    return true;
}

bool NetworkClient::readExact(SOCKET sock, uint8_t* buffer, size_t length) {
    size_t received = 0;
    while (received < length) {
        int n = recv(sock, reinterpret_cast<char*>(buffer + received), static_cast<int>(length - received), 0);
        if (n <= 0) return false;
        received += static_cast<size_t>(n);
    }
    return true;
}

void NetworkClient::threadProc() {
    while (running_) {
        reportStatus(L"接続中... (" + host_ + L")");
        SOCKET sock;
        if (!connectOnce(sock)) {
            reportStatus(L"接続失敗。iPhoneのインターネット共有がONか確認してください。再試行します。");
            std::this_thread::sleep_for(std::chrono::milliseconds(kConnectRetryDelayMs));
            continue;
        }

        reportStatus(L"接続済み・受信中");

        std::vector<uint8_t> payload;
        bool connectionAlive = true;
        while (running_ && connectionAlive) {
            uint8_t lengthBytes[4];
            if (!readExact(sock, lengthBytes, 4)) { connectionAlive = false; break; }
            uint32_t payloadLength =
                (static_cast<uint32_t>(lengthBytes[0]) << 24) |
                (static_cast<uint32_t>(lengthBytes[1]) << 16) |
                (static_cast<uint32_t>(lengthBytes[2]) << 8) |
                static_cast<uint32_t>(lengthBytes[3]);
            if (payloadLength == 0 || payloadLength > 64 * 1024 * 1024) { connectionAlive = false; break; }

            payload.resize(payloadLength);
            if (!readExact(sock, payload.data(), payloadLength)) { connectionAlive = false; break; }

            uint8_t type = payload[0];
            const uint8_t* body = payload.data() + 1;
            size_t bodyLen = payload.size() - 1;

            if (type == 0x01) { // parameter sets
                if (bodyLen < 2) continue;
                uint16_t spsLen = (static_cast<uint16_t>(body[0]) << 8) | body[1];
                if (bodyLen < static_cast<size_t>(2) + spsLen + 2) continue;
                std::vector<uint8_t> sps(body + 2, body + 2 + spsLen);
                size_t ppsLenOffset = 2 + spsLen;
                uint16_t ppsLen = (static_cast<uint16_t>(body[ppsLenOffset]) << 8) | body[ppsLenOffset + 1];
                if (bodyLen < ppsLenOffset + 2 + ppsLen) continue;
                std::vector<uint8_t> pps(body + ppsLenOffset + 2, body + ppsLenOffset + 2 + ppsLen);
                if (paramSetsCb_) paramSetsCb_(sps, pps);
            } else if (type == 0x02) { // video sample (AVCC)
                if (videoSampleCb_) videoSampleCb_(std::vector<uint8_t>(body, body + bodyLen));
            }
        }

        closesocket(sock);
        if (running_) reportStatus(L"切断されました。再接続します。");
    }
    reportStatus(L"停止");
}
