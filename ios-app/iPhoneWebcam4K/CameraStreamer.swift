import AVFoundation
import VideoToolbox
import Combine
import UIKit

/// Captures the back camera at up to 4K, encodes it with the hardware H.264
/// encoder in real time, and forwards each encoded sample to `StreamServer`.
final class CameraStreamer: NSObject, ObservableObject {
    @Published var statusText: String = "停止中"
    @Published var isStreaming: Bool = false
    @Published var activeResolution: String = "-"
    @Published var activeFPS: Double = 0
    @Published var measuredBitrateKbps: Double = 0

    private let session = AVCaptureSession()
    private let videoOutput = AVCaptureVideoDataOutput()
    private let captureQueue = DispatchQueue(label: "com.example.iphonewebcam4k.capture")
    private var compressionSession: VTCompressionSession?
    private var formatDescriptionSent = false

    let server = StreamServer()

    private var bytesSinceLastMeasurement: Int = 0
    private var lastBitrateMeasurement = Date()

    // MARK: - Public control

    func start() {
        guard !isStreaming else { return }
        server.onStateChange = { [weak self] state in
            DispatchQueue.main.async {
                self?.updateStatus(for: state)
            }
        }
        server.start()

        captureQueue.async { [weak self] in
            self?.configureSessionAndStart()
        }
        UIApplication.shared.isIdleTimerDisabled = true
        isStreaming = true
    }

    func stop() {
        guard isStreaming else { return }
        captureQueue.async { [weak self] in
            self?.session.stopRunning()
            if let compressionSession = self?.compressionSession {
                VTCompressionSessionInvalidate(compressionSession)
            }
            self?.compressionSession = nil
            self?.formatDescriptionSent = false
        }
        server.stop()
        UIApplication.shared.isIdleTimerDisabled = false
        isStreaming = false
        statusText = "停止中"
    }

    private func updateStatus(for state: StreamServer.State) {
        switch state {
        case .idle: statusText = "停止中"
        case .listening: statusText = "PCからの接続を待機中 (172.20.10.1:\(WireProtocol.defaultPort))"
        case .clientConnected: statusText = "PCに接続済み・配信中"
        case .failed(let message): statusText = "エラー: \(message)"
        }
    }

    // MARK: - Capture session setup

    private func configureSessionAndStart() {
        session.beginConfiguration()
        session.sessionPreset = .inputPriority

        guard let device = bestBackCamera() else {
            DispatchQueue.main.async { self.statusText = "背面カメラが見つかりません" }
            session.commitConfiguration()
            return
        }

        guard let input = try? AVCaptureDeviceInput(device: device),
              session.canAddInput(input) else {
            DispatchQueue.main.async { self.statusText = "カメラ入力を追加できません" }
            session.commitConfiguration()
            return
        }
        session.addInput(input)

        videoOutput.videoSettings = [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
        ]
        videoOutput.alwaysDiscardsLateVideoFrames = true
        videoOutput.setSampleBufferDelegate(self, queue: captureQueue)
        guard session.canAddOutput(videoOutput) else {
            DispatchQueue.main.async { self.statusText = "ビデオ出力を追加できません" }
            session.commitConfiguration()
            return
        }
        session.addOutput(videoOutput)

        if let connection = videoOutput.connection(with: .video), connection.isVideoOrientationSupported {
            connection.videoOrientation = .landscapeRight
        }

        let (format, dimensions, fps) = pick4KFormat(for: device) ?? pickBestFallbackFormat(for: device)
        do {
            try device.lockForConfiguration()
            device.activeFormat = format
            device.activeVideoMinFrameDuration = CMTime(value: 1, timescale: Int32(fps))
            device.activeVideoMaxFrameDuration = CMTime(value: 1, timescale: Int32(fps))
            device.unlockForConfiguration()
        } catch {
            DispatchQueue.main.async { self.statusText = "カメラ設定に失敗: \(error.localizedDescription)" }
        }

        session.commitConfiguration()

        setUpCompressionSession(width: dimensions.width, height: dimensions.height, fps: fps)

        DispatchQueue.main.async {
            self.activeResolution = "\(dimensions.width)x\(dimensions.height)"
            self.activeFPS = fps
        }

        session.startRunning()
    }

    private func bestBackCamera() -> AVCaptureDevice? {
        let candidates: [AVCaptureDevice.DeviceType] = [
            .builtInTripleCamera, .builtInDualWideCamera, .builtInDualCamera, .builtInWideAngleCamera
        ]
        let discovery = AVCaptureDevice.DiscoverySession(
            deviceTypes: candidates, mediaType: .video, position: .back
        )
        return discovery.devices.first
    }

    /// Looks for the highest-framerate 4K (3840x2160) format the device offers.
    private func pick4KFormat(for device: AVCaptureDevice) -> (AVCaptureDevice.Format, (width: Int32, height: Int32), Double)? {
        var best: (AVCaptureDevice.Format, (width: Int32, height: Int32), Double)?
        for format in device.formats {
            let dims = CMVideoFormatDescriptionGetDimensions(format.formatDescription)
            guard dims.width == 3840, dims.height == 2160 else { continue }
            let maxFPS = format.videoSupportedFrameRateRanges.map(\.maxFrameRate).max() ?? 0
            if best == nil || maxFPS > best!.2 {
                // Cap at 30fps: keeps bitrate/bandwidth requirements sane over the
                // USB-tethered link and matches what most receivers expect.
                best = (format, (dims.width, dims.height), min(maxFPS, 30))
            }
        }
        return best
    }

    private func pickBestFallbackFormat(for device: AVCaptureDevice) -> (AVCaptureDevice.Format, (width: Int32, height: Int32), Double) {
        var best: (AVCaptureDevice.Format, (width: Int32, height: Int32), Double)?
        for format in device.formats {
            let dims = CMVideoFormatDescriptionGetDimensions(format.formatDescription)
            let pixels = Int64(dims.width) * Int64(dims.height)
            let bestPixels = best.map { Int64($0.1.width) * Int64($0.1.height) } ?? 0
            if pixels > bestPixels {
                let maxFPS = format.videoSupportedFrameRateRanges.map(\.maxFrameRate).max() ?? 30
                best = (format, (dims.width, dims.height), min(maxFPS, 30))
            }
        }
        return best ?? (device.activeFormat, (1920, 1080), 30)
    }

    // MARK: - VideoToolbox compression

    private func setUpCompressionSession(width: Int32, height: Int32, fps: Double) {
        var session: VTCompressionSession?
        let status = VTCompressionSessionCreate(
            allocator: kCFAllocatorDefault,
            width: width,
            height: height,
            codecType: kCMVideoCodecType_H264,
            encoderSpecification: nil,
            imageBufferAttributes: nil,
            compressedDataAllocator: kCFAllocatorDefault,
            outputCallback: compressionOutputCallback,
            refcon: Unmanaged.passUnretained(self).toOpaque(),
            compressionSessionOut: &session
        )
        guard status == noErr, let compressionSession = session else {
            DispatchQueue.main.async { self.statusText = "H.264エンコーダの初期化に失敗 (\(status))" }
            return
        }

        let bitrate: Int32 = (width >= 3840) ? 32_000_000 : 12_000_000
        VTSessionSetProperty(compressionSession, key: kVTCompressionPropertyKey_RealTime, value: kCFBooleanTrue)
        VTSessionSetProperty(compressionSession, key: kVTCompressionPropertyKey_ProfileLevel, value: kVTProfileLevel_H264_High_AutoLevel)
        VTSessionSetProperty(compressionSession, key: kVTCompressionPropertyKey_AllowFrameReordering, value: kCFBooleanFalse)
        VTSessionSetProperty(compressionSession, key: kVTCompressionPropertyKey_AverageBitRate, value: NSNumber(value: bitrate))
        VTSessionSetProperty(compressionSession, key: kVTCompressionPropertyKey_ExpectedFrameRate, value: NSNumber(value: fps))
        VTSessionSetProperty(compressionSession, key: kVTCompressionPropertyKey_MaxKeyFrameInterval, value: NSNumber(value: Int(fps) * 2))
        VTCompressionSessionPrepareToEncodeFrames(compressionSession)

        self.compressionSession = compressionSession
        self.formatDescriptionSent = false
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

        let payload = Data(bytes: dataPointer, count: length)
        server.send(type: .videoSample, payload: payload)

        bytesSinceLastMeasurement += length
        let now = Date()
        let elapsed = now.timeIntervalSince(lastBitrateMeasurement)
        if elapsed >= 1.0 {
            let kbps = Double(bytesSinceLastMeasurement) * 8.0 / 1000.0 / elapsed
            bytesSinceLastMeasurement = 0
            lastBitrateMeasurement = now
            DispatchQueue.main.async { self.measuredBitrateKbps = kbps }
        }
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

// MARK: - AVCaptureVideoDataOutputSampleBufferDelegate

extension CameraStreamer: AVCaptureVideoDataOutputSampleBufferDelegate {
    func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
        guard server.hasClient, let compressionSession, let imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }
        let pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer)
        let duration = CMSampleBufferGetDuration(sampleBuffer)
        VTCompressionSessionEncodeFrame(
            compressionSession, imageBuffer: imageBuffer, presentationTimeStamp: pts, duration: duration,
            frameProperties: nil, sourceFrameRefcon: nil, infoFlagsOut: nil
        )
    }
}

// MARK: - VideoToolbox output callback

private func compressionOutputCallback(
    outputCallbackRefCon: UnsafeMutableRawPointer?,
    sourceFrameRefCon: UnsafeMutableRawPointer?,
    status: OSStatus,
    infoFlags: VTEncodeInfoFlags,
    sampleBuffer: CMSampleBuffer?
) {
    guard status == noErr, let sampleBuffer, let outputCallbackRefCon else { return }
    let streamer = Unmanaged<CameraStreamer>.fromOpaque(outputCallbackRefCon).takeUnretainedValue()
    streamer.handleEncodedFrame(sampleBuffer: sampleBuffer)
}
