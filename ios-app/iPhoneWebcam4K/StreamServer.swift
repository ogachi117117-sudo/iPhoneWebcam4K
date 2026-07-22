import Foundation
import Network

/// A minimal single-client TCP server. The Windows app connects to the iPhone
/// at 172.20.10.1:5959 (the fixed address the iPhone gets on the
/// USB "Personal Hotspot" link) and receives the framed H.264 stream.
final class StreamServer {
    enum State {
        case idle
        case listening
        case clientConnected
        case failed(String)
    }

    private(set) var state: State = .idle {
        didSet { onStateChange?(state) }
    }
    var onStateChange: ((State) -> Void)?

    private var listener: NWListener?
    private var currentClient: NWConnection?
    private let queue = DispatchQueue(label: "com.example.iphonewebcam4k.streamserver")

    func start(port: UInt16 = WireProtocol.defaultPort) {
        do {
            let params = NWParameters.tcp
            params.allowLocalEndpointReuse = true
            guard let nwPort = NWEndpoint.Port(rawValue: port) else {
                state = .failed("Invalid port \(port)")
                return
            }
            let listener = try NWListener(using: params, on: nwPort)
            listener.newConnectionHandler = { [weak self] connection in
                self?.accept(connection)
            }
            listener.stateUpdateHandler = { [weak self] newState in
                guard let self else { return }
                switch newState {
                case .failed(let error):
                    self.state = .failed(error.localizedDescription)
                case .ready:
                    if self.currentClient == nil { self.state = .listening }
                default:
                    break
                }
            }
            listener.start(queue: queue)
            self.listener = listener
            state = .listening
        } catch {
            state = .failed(error.localizedDescription)
        }
    }

    func stop() {
        currentClient?.cancel()
        currentClient = nil
        listener?.cancel()
        listener = nil
        state = .idle
    }

    private func accept(_ connection: NWConnection) {
        // Only one PC client at a time; replace any previous connection.
        currentClient?.cancel()
        currentClient = connection
        connection.stateUpdateHandler = { [weak self] newState in
            guard let self else { return }
            switch newState {
            case .ready:
                self.state = .clientConnected
            case .failed, .cancelled:
                if self.currentClient === connection {
                    self.currentClient = nil
                    self.state = .listening
                }
            default:
                break
            }
        }
        connection.start(queue: queue)
    }

    /// Sends one wire frame to the connected PC, if any. Safe to call from any queue.
    func send(type: PacketType, payload: Data) {
        guard let client = currentClient else { return }
        let frame = WireProtocol.makeFrame(type: type, payload: payload)
        client.send(content: frame, completion: .contentProcessed { _ in })
    }

    var hasClient: Bool { currentClient != nil }
}
