# iPhoneWebcam4K

iPhoneをUSB-C有線接続でPCの4K Webカメラとして使うためのアプリ一式。

- `ios-app/` — iPhone側アプリ(Swift / SwiftUI)。背面カメラを最大4Kでキャプチャし、H.264ハードウェアエンコードしてTCPで配信する。
- `windows-app/` — Windows側アプリ(C++ / Win32)。iPhoneからのH.264ストリームを受信・デコードし、OBS Studioの仮想カメラ(OBS Virtual Camera)に映像を書き込む。
- `.github/workflows/build-ios.yml` — Macを持たなくてもGitHub Actions上でiOSアプリをビルドできるCI設定。
- `docs/SETUP.md` — ビルドから実運用までの手順書。

詳しい手順は [docs/SETUP.md](docs/SETUP.md) を参照してください。

## アーキテクチャ概要

```
[iPhone]                                    [Windows PC]
AVCaptureSession(4K)
  → VTCompressionSession(H.264 HWエンコード)
  → NWListener TCPサーバ(:5959)             → Winsock TCPクライアント (172.20.10.1:5959)
                                               → Media Foundation H.264デコーダMFT (NV12)
                                               → OBS Virtual Camera 共有メモリへ書き込み
                                                 → Zoom / Teams / OBS 等で選択可能に
```

USB-C接続中にiPhoneの「インターネット共有」をONにすると、Windows側に仮想ネットワークアダプタ(Apple Mobile Device Ethernet)が作られ、iPhoneは固定IP `172.20.10.1` になる。これを使って通常のTCPソケット通信でUSB経由の有線映像伝送を実現している(MFi認証や独自USBプロトコルは不要)。
