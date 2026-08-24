# App logo (Mipsync Engine)

## 1. PNG を置く

新しいロゴは **ここ** に置いてください:

```
resources/icons/app_icon_source.png
```

| 項目 | 推奨 |
|------|------|
| 形式 | **PNG**（32-bit RGBA） |
| サイズ | **512×512** 以上（正方形） |
| 背景 | 透明 or 単色どちらでも可 |

`app_icon_source.png` はマスター用です。変換スクリプトが下記を **上書き生成** します:

| 生成ファイル | 用途 |
|--------------|------|
| **`app_icon.png`** | ウィンドウタイトルバー（GLFW） |
| **`app_icon.ico`** | Windows `.exe` アイコン（エクスプローラー・タスクバー） |

## 2. ICO / PNG を生成

リポジトリ直下で:

```powershell
cd C:\path\to\Mipsync
npm install
npm run icon
```

（初回だけ `npm install`。2 回目以降は `npm run icon` のみで OK）

## 3. ビルド

```powershell
cmake --build build --config Release
```

- **`app_icon.ico`** が無い状態で初めて CMake する場合は、先に `npm run icon` を実行してから `cmake ..` してください。
- 以降は `.ico` / `.png` を差し替えたら **再ビルド** するだけで反映されます（`app_icon.png` は exe 横に自動コピー）。

## タスクバーのピンが古いアイコンのまま

Windows はピン留め用アイコンを別キャッシュします。

1. タスクバーから古い Mipsync ピンを **すべて外す**
2. 新しい exe を起動し、実行中のアイコンを確認
3. エクスプローラーで `MipsyncEngine.exe` を右クリック → **タスクバーにピン留め**

## レガシー: SVG から生成

`app_icon_source.png` が無く **`app_icon.svg`** がある場合は SVG から変換します（Figma 書き出し用）。

## Project パネル用アイコン

種別アイコン（フォルダー・スクリプト等）は別フォルダです:

→ [`project/README.md`](project/README.md)
