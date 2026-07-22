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

## 6. 使い方(画面ミラーリング)

現在のバージョンは、背面カメラではなく **iPhoneの画面全体**(ホーム画面や他のアプリも含む)をPCに配信します。

1. iPhoneとPCをUSB-Cケーブルで接続する。
2. iPhoneの「設定」→「インターネット共有(パーソナルホットスポット)」をONにする(Wi-Fiやテザリング先が無くてもONにできます。セルラーデータは消費しません)。
3. Windows側でビルドした `IPhoneWebcam4K.exe` を起動し、「開始」を押しておく(iPhoneの固定IP `172.20.10.1` へ自動接続を試み続けます)。
4. iPhoneで「iPhoneWebcam4K」アプリを起動し、画面中央の丸いボタン(ブロードキャストピッカー)をタップ。表示された一覧から **「iPhoneWebcam4K」**(拡張機能名)を選び、「ブロードキャストを開始」をタップする。画面上部(機種によっては左上)が赤くなれば配信中です。
5. Zoom / Teams / ブラウザ会議等のカメラ選択で **「OBS Virtual Camera」** を選ぶと、iPhoneの画面がそのまま表示されます(アプリの名前ではなくOBSのカメラ名で表示される点に注意してください)。
6. 配信を止めるときは、画面上部の赤い帯(または赤いステータスバー)をタップ →「停止」。

**注意:** 配信中はiPhoneの画面に映っているもの(通知・パスワード入力画面なども含む)がそのままPCに映ります。人に見せたくない画面を開かないよう注意してください。

背面カメラでの配信(旧バージョンの動作)は、コード自体は`ios-app/iPhoneWebcam4K/CameraStreamer.swift`に残していますが、現在のUIからは呼び出していません。

## トラブルシューティング

- **Windowsアプリが「接続失敗」を繰り返す**: iPhoneのインターネット共有がONになっているか、USBケーブルがデータ通信対応(充電専用ケーブルでないか)を確認してください。
- **OBS Virtual Cameraが選択肢に出てこない**: OBS Studioのインストールが完了しているか確認し、PCを再起動してみてください。
- **映像がカクつく/止まる**: 60fps・高ビットレート(25Mbps)で配信しているため、USBケーブル/ポートを変えてみてください(USB 2.0規格の古いケーブルだと帯域が不足する場合があります)。
- **ブロードキャストピッカーに「iPhoneWebcam4K」が出てこない**: Sideloadlyでのインストールが完了しているか、Info.plistの`NSExtensionPointIdentifier`が正しいか確認してください。デバイスを再起動すると認識されることもあります。
- **7日後に「信頼されていないデベロッパ」エラーが出る**: 無料Apple IDの証明書期限切れです。手順3を再実行してください。
