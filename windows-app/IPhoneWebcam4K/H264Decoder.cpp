#include "H264Decoder.h"

#include <mferror.h>
#include <mfobjects.h>
#include <codecapi.h>
#include <cstring>

#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "Mf.lib")
#pragma comment(lib, "Mfuuid.lib")
#pragma comment(lib, "Ole32.lib")

using Microsoft::WRL::ComPtr;

namespace {
constexpr LONGLONG kFrameDuration100ns = 10'000'000 / 30;

std::vector<uint8_t> avccToAnnexB(const std::vector<uint8_t>& avcc) {
    std::vector<uint8_t> out;
    out.reserve(avcc.size() + 16);
    static const uint8_t kStartCode[4] = {0, 0, 0, 1};
    size_t offset = 0;
    while (offset + 4 <= avcc.size()) {
        uint32_t nalLen = (static_cast<uint32_t>(avcc[offset]) << 24) |
                           (static_cast<uint32_t>(avcc[offset + 1]) << 16) |
                           (static_cast<uint32_t>(avcc[offset + 2]) << 8) |
                           static_cast<uint32_t>(avcc[offset + 3]);
        offset += 4;
        if (offset + nalLen > avcc.size()) break;
        out.insert(out.end(), kStartCode, kStartCode + 4);
        out.insert(out.end(), avcc.begin() + offset, avcc.begin() + offset + nalLen);
        offset += nalLen;
    }
    return out;
}
}  // namespace

H264Decoder::H264Decoder() = default;

H264Decoder::~H264Decoder() {
    shutdown();
}

void H264Decoder::reportError(const std::wstring& message) {
    if (errorCb_) errorCb_(message);
}

bool H264Decoder::initialize(uint32_t width, uint32_t height) {
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL))) {
        reportError(L"Media Foundationの初期化に失敗しました");
        return false;
    }
    mfStarted_ = true;

    if (!createDecoderMFT()) return false;
    if (!configureTypes(width, height)) return false;

    transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    return true;
}

void H264Decoder::shutdown() {
    if (transform_) {
        transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        transform_.Reset();
    }
    if (mfStarted_) {
        MFShutdown();
        mfStarted_ = false;
    }
    inputTypeSet_ = false;
    outputTypeSet_ = false;
}

namespace {
// CLSID of the in-box "Microsoft H264 Video Decoder MFT" — a synchronous
// software decoder that ships with Windows 10/11 and is always registered,
// regardless of what hardware decoders MFTEnumEx happens to surface on a
// given machine. Hardware MFTs are always asynchronous (they must be driven
// via IMFMediaEventGenerator, not direct ProcessInput/ProcessOutput calls as
// this class does), and enumeration order/results for synchronous decoders
// varies by system, so this CLSID is instantiated directly instead.
const GUID kMSH264DecoderMFT = {
    0x62CE7E72, 0x4C71, 0x4D20, {0xB1, 0x5D, 0x45, 0x28, 0x31, 0xA8, 0x7D, 0x9D}};
}  // namespace

bool H264Decoder::createDecoderMFT() {
    HRESULT hr = CoCreateInstance(kMSH264DecoderMFT, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(transform_.GetAddressOf()));
    if (FAILED(hr)) {
        wchar_t buf[32];
        swprintf_s(buf, L"0x%08X", static_cast<unsigned int>(hr));
        reportError(std::wstring(L"H.264デコーダの作成に失敗しました (HRESULT=") + buf + L")");
        return false;
    }
    return true;
}

bool H264Decoder::configureTypes(uint32_t width, uint32_t height) {
    ComPtr<IMFMediaType> inputType;
    MFCreateMediaType(inputType.GetAddressOf());
    inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    MFSetAttributeSize(inputType.Get(), MF_MT_FRAME_SIZE, width, height);
    inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);

    if (FAILED(transform_->SetInputType(0, inputType.Get(), 0))) {
        reportError(L"H.264入力タイプの設定に失敗しました");
        return false;
    }
    inputTypeSet_ = true;

    return negotiateOutputType();
}

bool H264Decoder::negotiateOutputType() {
    for (DWORD i = 0;; i++) {
        ComPtr<IMFMediaType> outType;
        HRESULT hr = transform_->GetOutputAvailableType(0, i, outType.GetAddressOf());
        if (hr == MF_E_NO_MORE_TYPES || FAILED(hr)) break;

        GUID subtype = {};
        outType->GetGUID(MF_MT_SUBTYPE, &subtype);
        if (subtype != MFVideoFormat_NV12) continue;

        if (FAILED(transform_->SetOutputType(0, outType.Get(), 0))) continue;

        MFGetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE, &outputWidth_, &outputHeight_);

        MFT_OUTPUT_STREAM_INFO info = {};
        transform_->GetOutputStreamInfo(0, &info);
        mftProvidesOutputSamples_ =
            (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
        outputSampleSize_ = info.cbSize;
        outputTypeSet_ = true;
        return true;
    }
    reportError(L"NV12出力タイプの設定に失敗しました");
    return false;
}

void H264Decoder::submitParameterSets(const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps) {
    static const uint8_t kStartCode[4] = {0, 0, 0, 1};
    parameterSetsAnnexB_.clear();
    parameterSetsAnnexB_.insert(parameterSetsAnnexB_.end(), kStartCode, kStartCode + 4);
    parameterSetsAnnexB_.insert(parameterSetsAnnexB_.end(), sps.begin(), sps.end());
    parameterSetsAnnexB_.insert(parameterSetsAnnexB_.end(), kStartCode, kStartCode + 4);
    parameterSetsAnnexB_.insert(parameterSetsAnnexB_.end(), pps.begin(), pps.end());
}

void H264Decoder::submitVideoSample(const std::vector<uint8_t>& avccData) {
    if (!transform_ || !inputTypeSet_ || !outputTypeSet_) return;

    std::vector<uint8_t> annexB = parameterSetsAnnexB_;
    std::vector<uint8_t> nals = avccToAnnexB(avccData);
    if (nals.empty()) return;
    annexB.insert(annexB.end(), nals.begin(), nals.end());

    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(annexB.size()), buffer.GetAddressOf()))) return;

    BYTE* dst = nullptr;
    if (FAILED(buffer->Lock(&dst, nullptr, nullptr))) return;
    memcpy(dst, annexB.data(), annexB.size());
    buffer->Unlock();
    buffer->SetCurrentLength(static_cast<DWORD>(annexB.size()));

    ComPtr<IMFSample> sample;
    MFCreateSample(sample.GetAddressOf());
    sample->AddBuffer(buffer.Get());
    sampleTimestamp_ += kFrameDuration100ns;
    sample->SetSampleTime(sampleTimestamp_);
    sample->SetSampleDuration(kFrameDuration100ns);

    HRESULT hr = transform_->ProcessInput(0, sample.Get(), 0);
    if (hr == MF_E_NOTACCEPTING) {
        drainOutput();
        hr = transform_->ProcessInput(0, sample.Get(), 0);
    }
    if (FAILED(hr)) {
        wchar_t buf[32];
        swprintf_s(buf, L"0x%08X", static_cast<unsigned int>(hr));
        reportError(std::wstring(L"ProcessInputに失敗しました (HRESULT=") + buf + L")");
        return;
    }

    drainOutput();
}

void H264Decoder::drainOutput() {
    while (true) {
        ComPtr<IMFSample> outSample;
        MFT_OUTPUT_DATA_BUFFER outputBuffer = {};

        if (!mftProvidesOutputSamples_) {
            MFCreateSample(outSample.GetAddressOf());
            ComPtr<IMFMediaBuffer> outBuf;
            MFCreateMemoryBuffer(outputSampleSize_, outBuf.GetAddressOf());
            outSample->AddBuffer(outBuf.Get());
            outputBuffer.pSample = outSample.Get();
        }
        outputBuffer.dwStreamID = 0;

        DWORD status = 0;
        HRESULT hr = transform_->ProcessOutput(0, 1, &outputBuffer, &status);

        if (mftProvidesOutputSamples_ && outputBuffer.pSample) {
            outSample.Attach(outputBuffer.pSample);
        }

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) break;
        if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
            if (!negotiateOutputType()) break;
            continue;
        }
        if (FAILED(hr)) break;
        if (!outSample) continue;

        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(outSample->GetBufferByIndex(0, buffer.GetAddressOf()))) continue;

        ComPtr<IMF2DBuffer2> buffer2D;
        BYTE* scanline0 = nullptr;
        LONG pitch = 0;
        BYTE* bufferStart = nullptr;
        DWORD bufferLength = 0;
        bool locked2D = false;

        if (SUCCEEDED(buffer.As(&buffer2D)) &&
            SUCCEEDED(buffer2D->Lock2DSize(MF2DBuffer_LockFlags_Read, &scanline0, &pitch, &bufferStart, &bufferLength))) {
            locked2D = true;
        } else if (FAILED(buffer->Lock(&scanline0, nullptr, nullptr))) {
            continue;
        }

        // Fall back to a tightly-packed stride (no row padding) when the MFT
        // hands back a plain (non-2D) buffer, which is the software-decoder case.
        uint32_t strideY = locked2D ? static_cast<uint32_t>(pitch) : outputWidth_;
        const uint8_t* yPlane = scanline0;
        const uint8_t* uvPlane = scanline0 + static_cast<size_t>(strideY) * outputHeight_;
        if (frameCb_) frameCb_(yPlane, uvPlane, strideY, outputWidth_, outputHeight_);

        if (locked2D) {
            buffer2D->Unlock2D();
        } else {
            buffer->Unlock();
        }
    }
}
