#pragma once

#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Wraps a Media Foundation H.264 decoder MFT. Feed it Annex-B video samples
// (via submitVideoSample) and out-of-band SPS/PPS (via submitParameterSets,
// received from the iPhone before each keyframe); it converts the AVCC
// (length-prefixed) data coming over the network into Annex-B, prepends the
// current parameter sets, decodes, and reports each decoded NV12 frame via
// the frame callback.
class H264Decoder {
public:
    // yPlane/uvPlane point into the decoder's internal buffer and are only
    // valid for the duration of the callback. strideY applies to both planes.
    using FrameCallback = std::function<void(const uint8_t* yPlane, const uint8_t* uvPlane,
                                              uint32_t strideY, uint32_t width, uint32_t height)>;
    using ErrorCallback = std::function<void(const std::wstring& message)>;

    H264Decoder();
    ~H264Decoder();

    void setFrameCallback(FrameCallback cb) { frameCb_ = std::move(cb); }
    void setErrorCallback(ErrorCallback cb) { errorCb_ = std::move(cb); }

    // width/height are the encoder's nominal dimensions; used only to size
    // the initial input media type guess (the MFT adapts on the real stream).
    bool initialize(uint32_t width, uint32_t height);
    void shutdown();

    // avccData: AVCC-formatted sample as received from the network (one or
    // more NAL units, each with a 4-byte big-endian length prefix).
    void submitVideoSample(const std::vector<uint8_t>& avccData);
    void submitParameterSets(const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps);

private:
    bool createDecoderMFT();
    bool configureTypes(uint32_t width, uint32_t height);
    bool negotiateOutputType();
    void drainOutput();
    void reportError(const std::wstring& message);

    Microsoft::WRL::ComPtr<IMFTransform> transform_;
    bool mftProvidesOutputSamples_ = false;
    DWORD outputSampleSize_ = 0;
    uint32_t outputWidth_ = 0;
    uint32_t outputHeight_ = 0;

    std::vector<uint8_t> parameterSetsAnnexB_;
    bool inputTypeSet_ = false;
    bool outputTypeSet_ = false;
    LONGLONG sampleTimestamp_ = 0;

    FrameCallback frameCb_;
    ErrorCallback errorCb_;

    bool mfStarted_ = false;
};
