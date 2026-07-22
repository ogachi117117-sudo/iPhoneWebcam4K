import ReplayKit
import CoreMedia
import VideoToolbox

/// ReplayKit broadcast upload extension: receives the whole-device screen as
/// CMSampleBuffers (any app, the home screen, etc.), encodes it with the
/// hardware H.264 encoder, and streams it to the Windows receiver using the
/// same wire protocol as the camera path (WireProtocol.swift/StreamServer.swift,
/// shared with the main app target). Runs as its own process while a
/// broadcast is active; the main app is not involved in the video path.
final class SampleHandler: RPBroadcastSampleHandler {
    private let server = StreamServer()
    private var compressionSession: VTCompressionSession?
    private var formatDescriptionSent = false
    private var outputWidth: Int32 = 0
    private var outputHeight: Int32 = 0

    override func broadcastStarted(withSetupInfo setupInfo: [String: NSObject]?) {
        server.start()
    }

    override func broadcastFinished() {
        if let compressionSession {
            VTCompressionSessionInvalidate(compressionSession)
        }
        compressionSession = nil
        formatDescriptionSent = false
        server.stop()
    }

    override func processSampleBuffer(_ sampleBuffer: CMSampleBuffer, with sampleBufferType: RPSampleBufferType) {
        guard sampleBufferType == .video, server.hasClient else { return }
        guard let imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }

        let width = Int32(CVPixelBufferGetWidth(imageBuffer))
        let height = Int32(CVPixelBufferGetHeight(imageBuffer))
        if compressionSession == nil || width != outputWidth || height != outputHeight {
            setUpCompressionSession(width: width, height: height)
        }
        guard let compressionSession else { return }

        let pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer)
        let duration = CMSampleBufferGetDuration(sampleBuffer)
        VTCompressionSessionEncodeFrame(
            compressionSession, imageBuffer: imageBuffer, presentationTimeStamp: pts, duration: duration,
            frameProperties: nil, sourceFrameRefcon: nil, infoFlagsOut: nil
        )
    }

    // MARK: - VideoToolbox compression

    private func setUpCompressionSession(width: Int32, height: Int32) {
        if let compressionSession {
            VTCompressionSessionInvalidate(compressionSession)
        }
        compressionSession = nil
        formatDescriptionSent = false
        outputWidth = width
        outputHeight = height

        var session: VTCompressionSession?
        let status = VTCompressionSessionCreate(
            allocator: kCFAllocatorDefault,
            width: width,
            height: height,
            codecType: kCMVideoCodecType_H264,
            encoderSpecification: nil,
            imageBufferAttributes: nil,
            compressedDataAllocator: kCFAllocatorDefault,
            outputCallback: broadcastCompressionOutputCallback,
            refcon: Unmanaged.passUnretained(self).toOpaque(),
            compressionSessionOut: &session
        )
        guard status == noErr, let compressionSession = session else { return }

        // Screen content (text, UI edges) needs a higher bitrate than camera
        // video for the same resolution to stay clean; 60fps is requested
        // up front so the encoder doesn't have to renegotiate mid-stream.
        VTSessionSetProperty(compressionSession, key: kVTCompressionPropertyKey_RealTime, value: kCFBooleanTrue)
        VTSessionSetProperty(compressionSession, key: kVTCompressionPropertyKey_ProfileLevel, value: kVTProfileLevel_H264_High_AutoLevel)
        VTSessionSetProperty(compressionSession, key: kVTCompressionPropertyKey_AllowFrameReordering, value: kCFBooleanFalse)
        VTSessionSetProperty(compressionSession, key: kVTCompressionPropertyKey_AverageBitRate, value: NSNumber(value: 25_000_000))
        VTSessionSetProperty(compressionSession, key: kVTCompressionPropertyKey_ExpectedFrameRate, value: NSNumber(value: 60))
        VTSessionSetProperty(compressionSession, key: kVTCompressionPropertyKey_MaxKeyFrameInterval, value: NSNumber(value: 120))
        VTCompressionSessionPrepareToEncodeFrames(compressionSession)

        self.compressionSession = compressionSession
    }

    fileprivate func handleEncodedFrame(sampleBuffer: CMSampleBuffer) {
        guard CMSampleBufferDataIsReady(sampleBuffer) else { return }

        if isKeyFrame(sampleBuffer) || !formatDescriptionSent {
            if let (sps, pps) = extractParameterSets(from: sampleBuffer) {
                let payload = WireProtocol.makeParameterSetsPayload(sps: sps, pps: pps)
                server.send(type: .parameterSets, payload: payload)
                formatDescriptionSent = true
            }
        }

        guard let blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer) else { return }
        var length = 0
        var dataPointer: UnsafeMutablePointer<Int8>?
        guard CMBlockBufferGetDataPointer(blockBuffer, atOffset: 0, lengthAtOffsetOut: nil, totalLengthOut: &length, dataPointerOut: &dataPointer) == kCMBlockBufferNoErr,
              let dataPointer else { return }

        server.send(type: .videoSample, payload: Data(bytes: dataPointer, count: length))
    }

    private func isKeyFrame(_ sampleBuffer: CMSampleBuffer) -> Bool {
        guard let attachmentsArray = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, createIfNecessary: false) as? [[CFString: Any]],
              let attachments = attachmentsArray.first else {
            return true
        }
        let notSync = attachments[kCMSampleAttachmentKey_NotSync] as? Bool ?? false
        return !notSync
    }

    private func extractParameterSets(from sampleBuffer: CMSampleBuffer) -> (sps: Data, pps: Data)? {
        guard let formatDescription = CMSampleBufferGetFormatDescription(sampleBuffer) else { return nil }

        var spsPointer: UnsafePointer<UInt8>?
        var spsSize = 0
        var ppsPointer: UnsafePointer<UInt8>?
        var ppsSize = 0
        var count = 0

        guard CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
            formatDescription, parameterSetIndex: 0, parameterSetPointerOut: &spsPointer,
            parameterSetSizeOut: &spsSize, parameterSetCountOut: &count, nalUnitHeaderLengthOut: nil
        ) == noErr, count >= 2, let spsPointer else { return nil }

        guard CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
            formatDescription, parameterSetIndex: 1, parameterSetPointerOut: &ppsPointer,
            parameterSetSizeOut: &ppsSize, parameterSetCountOut: nil, nalUnitHeaderLengthOut: nil
        ) == noErr, let ppsPointer else { return nil }

        return (Data(bytes: spsPointer, count: spsSize), Data(bytes: ppsPointer, count: ppsSize))
    }
}

// MARK: - VideoToolbox output callback

private func broadcastCompressionOutputCallback(
    outputCallbackRefCon: UnsafeMutableRawPointer?,
    sourceFrameRefCon: UnsafeMutableRawPointer?,
    status: OSStatus,
    infoFlags: VTEncodeInfoFlags,
    sampleBuffer: CMSampleBuffer?
) {
    guard status == noErr, let sampleBuffer, let outputCallbackRefCon else { return }
    let handler = Unmanaged<SampleHandler>.fromOpaque(outputCallbackRefCon).takeUnretainedValue()
    handler.handleEncodedFrame(sampleBuffer: sampleBuffer)
}
