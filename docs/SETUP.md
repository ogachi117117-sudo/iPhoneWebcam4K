# セットアップ手順

iPhoneをUSB-C有線接続でPCの4K Webカメラとして使うための、ビルド〜実行までの手順です。

## 全体像

```
iPhone(このリポジトリのiOSアプリ) --USB-C(インターネット共有)--> Windows PC(このリポジトリのWindowsアプリ)
                                                                    ↓ 書き込み
                                                          OBS Virtual Camera(共有メモリ)
                                                                    ↓
                                                     Zoom / Teams / ブラウザ等がカメラとして選択
```

Windows側アプリは自前でカメラドライバを名乗らず、OBS Studioが既に提供している「OBS Virtual Camera」に映像を書き込みます。**OBS Studio自体を起動したり、OBSの「仮想カメラ開始」ボタンを押す必要はありません**(インストールしてカメラを1回だけシステムに登録させれば十分です)。ただし、OBSの仮想カメラ機能を同時に使う(OBS側で「仮想カメラ開始」を押す)と、書き込み役が競合してこのアプリの映像が出せなくなるので、その場合はOBS側の仮想カメラは止めてください。

---

## 1. iOSアプリをビルドする(GitHub Actions)

Macを使わず、GitHub Actions上のクラウドMacでビルドします。

1. https://github.com で無料アカウントを作成(未作成の場合)。
2. 新しいリポジトリを作成(Public/Privateどちらでも可。Privateの場合は月次のActions無料枠に注意)。
3. このフォルダ(`iPhoneWebcam4K`)の中身をそのリポジトリにpushする:
   ```
   cd C:\Users\MIRAI-3\iPhoneWebcam4K
   git init
   git add .
   git commit -m "Initial commit"
   git branch -M main
   git remote add origin https://github.com/<あなたのアカウント>/<リポジトリ名>.git
   git push -u origin main
   ```
4. GitHubのリポジトリページ → 「Actions」タブ → 「Build iOS IPA」ワークフローが自動実行されます(mainにpushすると自動起動。手動実行も可能)。
5. 実行が完了したら、そのワークフロー実行画面の下部「Artifacts」から `iPhoneWebcam4K-unsigned-ipa` をダウンロードし、展開して `iPhoneWebcam4K.ipa` を取り出す。

この時点の `.ipa` は**署名されていません**。次のSideloadlyの手順で、あなたの無料Apple IDを使って署名しながらインストールします。

## 2. WindowsにApple Mobile Device Supportを入れる

iPhoneをUSB接続した際にPCが認識するために必要です(iTunesまたは単体の「Apple Devices」アプリに同梱)。

- Microsoft Store で「Apple Devices」を検索してインストール、**または**
- https://www.apple.com/itunes/ から iTunes をインストール

インストール後、一度iPhoneをUSB-Cケーブルで接続し、iPhone側で表示される「このコンピュータを信頼しますか?」ダイアログで「信頼」を選択してください。

## 3. Sideloadlyで.ipaを実機にインストールする

1. https://sideloadly.io/ から Sideloadly(Windows版)をダウンロード・インストール。
2. iPhoneをUSB-Cで接続した状態でSideloadlyを起動。
3. 手順1で作成した `iPhoneWebcam4K.ipa` をSideloadlyにドラッグ&ドロップ。
4. Apple ID(お使いの通常のApple IDでOK。専用に新規作成しても構いません)を入力してサインイン。
5. 「Start」でインストール。iPhone側で初回は 設定 > 一般 > VPNとデバイス管理 から、使用したApple IDの開発者用プロファイルを「信頼」する必要があります。

**注意:** 無料Apple IDで署名したアプリは **7日間で証明書が失効** します。期限が切れたら同じ手順(Sideloadlyに.ipaをドラッグしてStart)を繰り返すだけで再インストールできます。有料のApple Developer Program($99/年)に加入すると1年間有効になります。

## 4. OBS Studioをインストールする

https://obsproject.com/ から通常インストールするだけで、Windowsに「OBS Virtual Camera」というカメラデバイスが登録されます。OBS自体を起動する必要はありません(インストールするだけでOK)。

## 5. Windowsアプリをビルドする

`windows-app\IPhoneWebcam4K.sln` を Visual Studio 2022 で開き、構成を `Release / x64` にしてビルドしてください(ビルド環境のセットアップ自体はこのプロジェクトで別途進めています)。

## 6. 使い方(背面カメラ配信)

1. iPhoneとPCをUSB-Cケーブルで接続する。
2. iPhoneの「設定」→「インターネット共有(パーソナルホットスポット)」をONにする(Wi-Fiやテザリング先が無くてもONにできます。セルラーデータは消費しません)。
3. iPhoneで「iPhoneWebcam4K」アプリを起動し、「配信を開始」をタップ。画面ロックは自動的に無効化されます。
4. Windows側でビルドした `IPhoneWebcam4K.exe` を起動し、「開始」を押す。iPhoneの固定IP `172.20.10.1` へ自動接続します。
5. Zoom / Teams / ブラウザ会議等のカメラ選択で **「OBS Virtual Camera」** を選ぶと、iPhoneの映像が表示されます(アプリの名前ではなくOBSのカメラ名で表示される点に注意してください)。

画面ミラーリング機能(iPhoneの画面全体をPCに配信する機能)も`ios-app/BroadcastExtension/`に実装済みで、アプリ内のブロードキャストピッカーから利用できますが、現在のメイン画面(`ContentView.swift`)ではカメラ配信のボタンを表示しています。

## トラブルシューティング

- **Windowsアプリが「接続失敗」を繰り返す**: iPhoneのインターネット共有がONになっているか、USBケーブルがデータ通信対応(充電専用ケーブルでないか)を確認してください。
- **OBS Virtual Cameraが選択肢に出てこない**: OBS Studioのインストールが完了しているか確認し、PCを再起動してみてください。
- **映像がカクつく/止まる**: iPhone側アプリの表示ビットレートを確認し、極端に低い場合はUSBケーブル/ポートを変えてみてください(USB 2.0規格の古いケーブルだと帯域が不足する場合があります)。
- **7日後に「信頼されていないデベロッパ」エラーが出る**: 無料Apple IDの証明書期限切れです。手順3を再実行してください。
