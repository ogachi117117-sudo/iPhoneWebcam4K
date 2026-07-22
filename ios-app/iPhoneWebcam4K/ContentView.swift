import SwiftUI

struct ContentView: View {
    @StateObject private var streamer = CameraStreamer()

    var body: some View {
        VStack(spacing: 24) {
            Text("iPhone 4K Webcam")
                .font(.title2).bold()

            VStack(alignment: .leading, spacing: 8) {
                Label(streamer.statusText, systemImage: streamer.isStreaming ? "dot.radiowaves.left.and.right" : "pause.circle")
                    .foregroundStyle(streamer.isStreaming ? .green : .secondary)
                Text("解像度: \(streamer.activeResolution) @ \(String(format: "%.0f", streamer.activeFPS)) fps")
                Text("送信ビットレート: \(String(format: "%.0f", streamer.measuredBitrateKbps)) kbps")
            }
            .font(.subheadline)
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding()
            .background(Color(.secondarySystemBackground))
            .clipShape(RoundedRectangle(cornerRadius: 12))

            Text("USB-CでPCに接続し、設定 > インターネット共有 をONにしてから開始してください。")
                .font(.footnote)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)

            Button {
                streamer.isStreaming ? streamer.stop() : streamer.start()
            } label: {
                Text(streamer.isStreaming ? "配信を停止" : "配信を開始")
                    .font(.headline)
                    .frame(maxWidth: .infinity)
                    .padding()
                    .background(streamer.isStreaming ? Color.red : Color.blue)
                    .foregroundStyle(.white)
                    .clipShape(RoundedRectangle(cornerRadius: 12))
            }
        }
        .padding()
    }
}

#Preview {
    ContentView()
}
