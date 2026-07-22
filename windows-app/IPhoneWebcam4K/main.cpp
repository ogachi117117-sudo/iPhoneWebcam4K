#include <windows.h>
#include <windowsx.h>
#include <memory>
#include <string>

#include "NetworkClient.h"
#include "H264Decoder.h"
#include "VirtualCameraOutput.h"

namespace {
constexpr wchar_t kWindowClassName[] = L"IPhoneWebcam4KWindow";
constexpr wchar_t kDefaultHost[] = L"172.20.10.1";  // Fixed IP the iPhone takes on the USB Personal Hotspot link.
constexpr UINT WM_APP_STATUS = WM_APP + 1;

// WM_APP_STATUS carries a heap-allocated std::wstring* in lParam; the window
// procedure takes ownership and deletes it after use. Callbacks that post
// this message run on background threads (NetworkClient/H264Decoder), so all
// UI control updates must happen after crossing back to the UI thread here.
void PostStatus(HWND hwnd, const std::wstring& text) {
    auto* copy = new std::wstring(text);
    PostMessageW(hwnd, WM_APP_STATUS, 0, reinterpret_cast<LPARAM>(copy));
}
}  // namespace

class App {
public:
    explicit App(HWND hwnd) : hwnd_(hwnd) {}

    void start() {
        if (running_) return;
        running_ = true;

        decoder_ = std::make_unique<H264Decoder>();
        vcam_ = std::make_unique<VirtualCameraOutput>();

        decoder_->setErrorCallback([this](const std::wstring& msg) { PostStatus(hwnd_, L"デコードエラー: " + msg); });
        decoder_->setFrameCallback([this](const uint8_t* y, const uint8_t* uv, uint32_t strideY, uint32_t w, uint32_t h) {
            onDecodedFrame(y, uv, strideY, w, h);
        });

        if (!decoder_->initialize(3840, 2160)) {
            PostStatus(hwnd_, L"H.264デコーダの初期化に失敗しました");
            running_ = false;
            return;
        }

        client_ = std::make_unique<NetworkClient>(kDefaultHost, WireProtocolDefaultPort());
        client_->setStatusCallback([this](const std::wstring& s) { PostStatus(hwnd_, s); });
        client_->setParameterSetsCallback([this](const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps) {
            decoder_->submitParameterSets(sps, pps);
        });
        client_->setVideoSampleCallback([this](const std::vector<uint8_t>& sample) {
            decoder_->submitVideoSample(sample);
        });
        client_->start();

        SetWindowTextW(GetDlgItem(hwnd_, kButtonId), L"停止");
    }

    void stop() {
        if (!running_) return;
        running_ = false;

        if (client_) client_->stop();
        client_.reset();
        if (decoder_) decoder_->shutdown();
        decoder_.reset();
        if (vcam_) vcam_->stop();
        vcam_.reset();
        vcamWidth_ = vcamHeight_ = 0;

        SetWindowTextW(GetDlgItem(hwnd_, kButtonId), L"開始");
        PostStatus(hwnd_, L"停止中");
    }

    void toggle() { running_ ? stop() : start(); }

    static constexpr int kButtonId = 101;
    static constexpr int kStatusLabelId = 102;

private:
    static uint16_t WireProtocolDefaultPort() { return 5959; }

    // Called on the H264Decoder's internal thread (driven by NetworkClient's
    // worker thread). Lazily (re)creates the OBS virtual camera queue when
    // the decoded resolution is first known or changes.
    void onDecodedFrame(const uint8_t* y, const uint8_t* uv, uint32_t strideY, uint32_t w, uint32_t h) {
        if (!vcam_) return;
        if (w != vcamWidth_ || h != vcamHeight_) {
            std::wstring err;
            vcam_->stop();
            if (!vcam_->start(w, h, 30.0, &err)) {
                PostStatus(hwnd_, err);
                vcamWidth_ = vcamHeight_ = 0;
                return;
            }
            vcamWidth_ = w;
            vcamHeight_ = h;
            PostStatus(hwnd_, L"OBS仮想カメラへ出力中 (" + std::to_wstring(w) + L"x" + std::to_wstring(h) + L")");
        }
        vcam_->writeFrame(y, uv, strideY);
    }

    HWND hwnd_;
    bool running_ = false;
    std::unique_ptr<NetworkClient> client_;
    std::unique_ptr<H264Decoder> decoder_;
    std::unique_ptr<VirtualCameraOutput> vcam_;
    uint32_t vcamWidth_ = 0;
    uint32_t vcamHeight_ = 0;
};

namespace {
App* g_app = nullptr;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            CreateWindowW(L"STATIC", L"iPhone 4K Webcam (Windows受信側)\r\n開始を押すと iPhone からの接続を待ち受けます。",
                          WS_CHILD | WS_VISIBLE, 20, 20, 440, 40, hwnd, nullptr, nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"開始", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          20, 70, 120, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(App::kButtonId)),
                          nullptr, nullptr);
            CreateWindowW(L"STATIC", L"停止中", WS_CHILD | WS_VISIBLE | SS_LEFT,
                          20, 115, 440, 80, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(App::kStatusLabelId)),
                          nullptr, nullptr);
            g_app = new App(hwnd);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == App::kButtonId && g_app) {
                g_app->toggle();
            }
            return 0;
        case WM_APP_STATUS: {
            std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
            SetWindowTextW(GetDlgItem(hwnd, App::kStatusLabelId), text->c_str());
            return 0;
        }
        case WM_DESTROY:
            if (g_app) {
                g_app->stop();
                delete g_app;
                g_app = nullptr;
            }
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    // Needed on this (UI) thread because H264Decoder::createDecoderMFT() uses
    // CoCreateInstance directly to get the in-box synchronous H.264 decoder.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kWindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(kWindowClassName, L"iPhone 4K Webcam", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME,
                              CW_USEDEFAULT, CW_USEDEFAULT, 500, 240, nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}
