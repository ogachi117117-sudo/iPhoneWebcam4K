import Foundation

/// Wire format shared with the Windows receiver (windows-app/IPhoneWebcam4K/NetworkClient.cpp).
///
/// Each packet on the TCP stream is:
///   [4 bytes big-endian: payload length N]
///   [1 byte: PacketType]
///   [N-1 bytes: payload]
///
/// PacketType 0x01 (parameterSets) payload:
///   [2 bytes BE: spsLength][sps bytes][2 bytes BE: ppsLength][pps bytes]
///
/// PacketType 0x02 (videoSample) payload:
///   Raw AVCC-formatted sample data as produced by VideoToolbox: one or more
///   NAL units, each prefixed with a 4-byte big-endian length. The Windows
///   side converts this to Annex-B before feeding the H.264 decoder MFT.
enum PacketType: UInt8 {
    case parameterSets = 0x01
    case videoSample = 0x02
}

enum WireProtocol {
    static let defaultPort: UInt16 = 5959

    /// Builds a full frame ([length][type][payload]) ready to send on the socket.
    static func makeFrame(type: PacketType, payload: Data) -> Data {
        var frame = Data(capacity: 4 + 1 + payload.count)
        let payloadLength = UInt32(1 + payload.count)
        frame.append(contentsOf: payloadLength.bigEndianBytes)
        frame.append(type.rawValue)
        frame.append(payload)
        return frame
    }

    static func makeParameterSetsPayload(sps: Data, pps: Data) -> Data {
        var payload = Data()
        payload.append(contentsOf: UInt16(sps.count).bigEndianBytes)
        payload.append(sps)
        payload.append(contentsOf: UInt16(pps.count).bigEndianBytes)
        payload.append(pps)
        return payload
    }
}

extension FixedWidthInteger {
    var bigEndianBytes: [UInt8] {
        withUnsafeBytes(of: self.bigEndian, Array.init)
    }
}
