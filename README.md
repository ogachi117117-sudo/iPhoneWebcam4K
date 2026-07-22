# iPhoneWebcam4K

iPhoneをUSB-C有線接続でPCの4K Webカメラ/画面ミラーリング元として使うためのアプリ一式。

- `ios-app/iPhoneWebcam4K/` — iPhone側メインアプリ(Swift / SwiftUI)。画面配信の開始ボタン(ReplayKitのブロードキャストピッカー)を表示する。背面カメラ配信用の`CameraStreamer`も同梱しているが、現在のUIからは未使用(将来切り替え予定)。
- `ios-app/BroadcastExtension/` — ReplayKitの Broadcast Upload Extension(Swift)。iPhoneの画面全体(ホーム画面や他アプリ含む)を最大60fpsでキャプチャし、H.264ハードウェアエンコードしてTCPで配信する。実際の映像配信はすべてこの拡張(別プロセス)が担う。
- `windows-app/` — Windows側アプリ(C++ / Win32)。iPhoneからのH.264ストリームを受信・デコードし、OBS Studioの仮想カメラ(OBS Virtual Camera)に映像を書き込む。
- `.github/workflows/build-ios.yml` — Macを持たなくてもGitHub Actions上でiOSアプリをビルドできるCI設定。
- `docs/SETUP.md` — ビルドから実運用までの手順書。

詳しい手順は [docs/SETUP.md](docs/SETUP.md) を参照してください。

## アーキテクチャ概要

```
[iPhone]                                    [Windows PC]
RPBroadcastSampleHandler(画面全体, 最大60fps)
  → VTCompressionSession(H.264 HWエンコード)
  → NWListener TCPサーバ(:5959)             → Winsock TCPクライアント (172.20.10.1:5959)
                                               → Media Foundation H.264デコーダMFT (NV12)
                                               → OBS Virtual Camera 共有メモリへ書き込み
                                                 → Zoom / Teams / OBS 等で選択可能に
```

USB-C接続中にiPhoneの「インターネット共有」をONにすると、Windows側に仮想ネットワークアダプタ(Apple Mobile Device Ethernet)が作られ、iPhoneは固定IP `172.20.10.1` になる。これを使って通常のTCPソケット通信でUSB経由の有線映像伝送を実現している(MFi認証や独自USBプロトコルは不要)。
