import SwiftUI
import ReplayKit

struct ContentView: View {
    var body: some View {
        VStack(spacing: 24) {
            Text("iPhone 4K Webcam")
                .font(.title2).bold()

            Text("画面ミラーリング配信")
                .font(.headline)

            Text("下の丸いボタンをタップし、「iPhoneWebcam4K」を選んで「ブロードキャストを開始」すると、iPhoneの画面(ホーム画面や他のアプリを含む)がPCに配信されます。")
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)

            BroadcastPickerView(preferredExtension: "com.example.iphonewebcam4k.BroadcastExtension")
                .frame(width: 84, height: 84)

            VStack(spacing: 4) {
                Text("USB-CでPCに接続し、設定 > インターネット共有 をONにしてから開始してください。")
                Text("配信中は画面上部(または左上)に赤い帯が表示されます。停止するときはその帯をタップしてください。")
            }
            .font(.footnote)
            .foregroundStyle(.secondary)
            .multilineTextAlignment(.center)
        }
        .padding()
    }
}

/// Wraps ReplayKit's UIKit-only broadcast picker button for SwiftUI.
/// Tapping it starts/stops a broadcast handled by BroadcastExtension/SampleHandler.swift.
private struct BroadcastPickerView: UIViewRepresentable {
    let preferredExtension: String

    func makeUIView(context: Context) -> RPSystemBroadcastPickerView {
        let view = RPSystemBroadcastPickerView(frame: .zero)
        view.preferredExtension = preferredExtension
        view.showsMicrophoneButton = false
        return view
    }

    func updateUIView(_ uiView: RPSystemBroadcastPickerView, context: Context) {}
}

#Preview {
    ContentView()
}
