# PS1起動形式と日本版SCPH-9000／MM3の互換性

2026-10-09更新。標準ビルドはNTSC-Jの起動領域を保持し、Mipsyncロゴはゲーム実行開始後にフェード表示する。BIOS内のSONYロゴ、BIOSファイル、起動領域の純正TMDを変更しない。

## 共通エンジンの変更

- `templates/ps1/licensej.dat` を標準の日本向け起動領域として同梱する。セクター4の日本向け識別情報と、セクター5～11の純正PlayStation TMDを保持する。
- `src/ps1/BootLogo.cpp` は起動領域をそのままコピーする。2336バイト形式の12／16セクターを保持し、2352バイト形式の場合はセクターヘッダーのみを取り除く。ロゴを書き換える処理を廃止した。
- `src/ps1/Ps1Build.cpp` は標準で日本向けデータを選ぶ。プロジェクト直下に明示的な `licensej.dat`／`licensea.dat`／`licensee.dat` があれば、その地域データを優先する。米国向けデータを指定したプロジェクトは日本版MM3だけでの直接起動を想定しない。
- PSn00bSDK／mkpsxisoでISO9660のSYSTEM.CNFとPS-X EXEを、Track 1 MODE2/2352のBIN/CUEにする。セクターのEDC/ECCも生成する。
- ゲーム側の `runtime/boot_brand.c` は通常起動・高速起動とも約1.5秒のフェードを一度表示する。GPUSTATによるBIOS表示済み判定を廃止した。ゲーム内シーン遷移では再表示しない。
- エディタのネイティブコードを再リンクしないビルドでも、PS1テンプレートをビルド出力・最新インストールへ同期する。古いランタイムが次のPS1ビルドへ入り直す問題も防ぐ。

旧 `Patch-Ps1BootBios.ps1` とカスタムTMDは以前の実験用ファイルであり、現行ビルドから呼ばれない。BIOSパッチの適用は不要。

## なぜMM3だけでは以前の形式が問題だったか

MM3が送る地域コードはCDコントローラーのディスク認証用であり、日本版BIOSの追加の地域／ロゴ照合とは別である。日本版SCPH-9000には日本用MM3（SCEI）が必要になる。以前の米国識別情報＋Mipsync TMDは、この日本版BIOSの条件を満たしていなかった。

現行形式は日本向け識別情報と純正TMDを保持するため、ROM側でこの照合を回避する必要がなくなった。これは配線・レーザー・CD-Rの品質まで保証する意味ではない。

根拠:

- [PSX-SPX: システムエリアと日本版BIOSのロゴ照合](https://psx-spx.consoledev.net/cdromformat/#system-area-prior-to-volume-descriptors)
- [PSX-SPX: SCExとモッドチップ](https://psx-spx.consoledev.net/cdromformat/#cdrom-protection-scex-strings)
- [MM3の配線・地域別ファームウェアと日本版の追加制約](https://quade.co/ps1-modchip-guide/mm3/)
- [DuckStationのBIOS識別表](https://github.com/stenzek/duckstation/blob/master/src/core/bios.cpp): SCPH-7000／7500／9000はv4.0、1997-08-18 J、MD5 `8e4c14f567745eff2f0408c8129f72a6`。

## 実施した検証

ユーザー所有の未改変日本版v4.0 BIOSを使用し、DuckStationで高速起動とステート復帰を使わず通常起動した。使用したファイル名は `scph7000.bin` だが、上記のSCPH-9000と同一のMD5を持つ。検証前後のBIOSファイルのSHA256も一致した。検証用エミュレーターの設定は終了時に元へ戻した。

| 確認対象 | 結果 |
|---|---|
| 日本版v4.0 BIOSの通常起動 | BIOSの通常起動を経てMipsyncロゴとTrainのシーン初期化へ到達 |
| 米国版v2.2 BIOSの通常起動 | Mipsyncロゴとシーン初期化へ到達 |
| 日本版v4.0で意図的な高速起動 | ゲーム側Mipsyncロゴを表示 |
| 二つのシーンを相互遷移するビルド | ロゴ表示は起動時の一度のみ。遷移ではhandoffを消費して省略 |
| 最終Trainの生成BIN/CUE | 1,357セクター。1,353 Form1＋4 Form2のEDC、Form1の全ECCが一致 |
| 最終Trainの起動領域 | 標準日本向けデータの全12セクターの2048バイトペイロードが一致 |
| 実行ファイル | SYSTEM.CNFからPSX.EXEを起動。ロード範囲 `0x80010000`～`0x800AA000`、PS1の2MiB内 |
| 古い米国向けファイルがエンジン側に残る構成 | 日本向け標準データを優先し、二つのシーンとディスクを正常生成 |
| 共通コードの関連テスト | クリッピング、起動領域、ロゴ、テクスチャ、ViewModel、Unlitの6テスト成功 |

最終検証のディスク:

`C:/Users/Owner/MipsyncProjects/Train/Builds/PS1/Train/out-84444-2525234109/game.cue`

BIN SHA256: `c1c749cb7d1aa337f82db2c6165848c3fa84eaae825aded411106372e729a389`

標準日本向け起動領域 SHA256: `cf805a3850a5b299c76a9a25bd10be13b623e1106d4c64ba4e708d05a38c7bc8`

検証結果と再現手順:

- `tmp/ps1-default-over-legacy-us-test.log`: 古い米国向けファイルが標準日本向けデータを隠さないことの検証。
- `tmp/train-mm3-disc-verification.json`: 最終ROMの形式・全EDC/ECC・起動領域・EXEの照合結果。
- `tmp/train-mm3-scph9000-final-boot.log`: 最終ROMの日本版v4.0通常起動ログ。
- `tmp/train-us-standard-boot.log`: 米国版通常起動。
- `tmp/train-jp-fast-boot.log`: 日本版高速起動。
- `tmp/jp-scene-handoff-boot.log`: ロゴ一度・複数シーン遷移。
- `tools/Verify-Ps1Disc.cjs`: `node tools/Verify-Ps1Disc.cjs <game.cue> <licensej.dat> [report.json]` でROMを読み取り専用検証できる。

実機の日本版SCPH-9000＋MM3＋CD-Rによる起動は未検証。ここで確認できたのは、その実機と同じBIOSのチェックを通るROM形式であること。日本向けMM3が正しく動作し、CD-Rを読み取れる実機での最終確認は必要になる。書き込み時はCUEをディスクイメージとして指定する。

## 壁・床の描画も共通化

奥行き差の大きい面を視点に応じて細分化し、UVをカメラ空間の比率で補間し、分割後の面の深度で並べる。手前の壁だけでなく奥の階段・屋根にも同じ条件を適用する。大きな奥の面が手前へ重なる異常な折り返しを防ぐ。既に細分化された床には壁用の追加細分化判定を重ねない。

正面・斜め・床を見下ろす画像を独立したPS1ビルドで確認した。通常のPS1のアフィン歪みや整数座標による揺れは残る。固定条件の360度回転測定では、追加補正後9.9fpsから床の不要な処理を省いて11.5fpsへ改善した。補正前の14.2fpsと同じ条件なので、描画補正による負荷増は残っている。プロジェクトの設定やゲームコードを勝手に軽量化していない。
