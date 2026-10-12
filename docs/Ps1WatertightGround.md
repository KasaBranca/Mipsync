# PS1の床ポリゴンの隙間を防ぐ修正

2026-10-10。共通ランタイム、配布版エディタのPS1テンプレート、Trainの生成ランタイムに反映済み。

床の書き出しデータを調べると、片面962三角形の内部の辺はすべて対になっていた。データの内部に開いた辺はなかった。

描画処理では、GPUの座標／ポリゴンサイズ制限を超えた三角形だけをCPUで画面端にクリップしていた。隣の三角形がGPUの画面外切り取りに任される場合、CPU側は丸めた交点から新しい辺を引き、GPU側は元の長い辺を使う。この二つの線が整数画素上で一致せず、斜めに背景が露出していた。CPUの正確な除算＋ゼロ方向への切り捨ても、GTEのUNR除算＋算術シフトと異なっていた。

修正:

- 画面端をまたぐ面を同じ境界でクリップする。GPUの上限を超える一方の面だけを切らない。
- CPUクリッピング用の投影もGTEのUNR除算・算術シフトに合わせる。極端な座標は64ビットのまま保持し、近すぎる点は上限に潰さず画面端までクリップする。
- 深度と画面端の交点は辺の方向を統一して計算する。逆方向から共有辺を処理しても同じ座標になる。
- 床の頂点を三角形ごとに1ピクセル押し広げる処理を廃止する。

実装:

- `templates/ps1/starter/runtime/render.c`
- `templates/ps1/starter/runtime/clip_math.h`
- `templates/ps1/starter/runtime/watertight_projection.h`
- `tests/Ps1ClipMathTests.c`

[GTEの計算仕様](https://psx-spx.consoledev.net/geometrytransformationenginegte/#gte-division-inaccuracy)に合わせた投影を使用する。

検証:

| 条件 | 床の隙間が出た視点 | 背景が露出した画素 |
|---|---:|---:|
| 修正前、180視点 | 100 | 5,159 |
| 修正後、単色の床、180視点 | 0 | 0 |
| 修正後、実際の床テクスチャ、180視点 | 0 | 0 |

位置・高さ・ヨー・俯角を毎フレーム変え、他のオブジェクトを隠した独立したPS1ビルドで、床が覆う画面下部310×81画素を読む。1回あたり4,519,800画素を検査した。背景をマゼンタにして、床の隙間から背景が見える画素を数える。プロジェクトのシーンやマテリアルの設定は変更していない。

- `tmp/train-floor-topology.json`: 書き出し床の頂点・三角形・内部開放辺の調査。
- `tmp/train-floor-gap-before.log`: 修正前の180視点の背景露出。
- `tmp/train-floor-gap-watertight.log`: 修正後の単色床。
- `tmp/train-floor-gap-textured.log`: 修正後のテクスチャ付き床。
- `tmp/PrepareTrainFloorGapProbe.ps1`: 独立した検証ビルドを作成するスクリプト。
- `tmp/RunFloorGapProbe.ps1`: 検証用の独立したエミュレーターを実行し、設定を復元するスクリプト。

関連する4つのネイティブテスト（ClipMath、ViewmodelProjection、TextureResolution、UnlitRenderer）は成功した。ClipMathでは、共有辺の逆方向の交点一致、UNR投影、負の座標の算術シフト、近距離の上限に潰さない投影、極端な座標の保持を確認する。

TrainのPS1再ビルド、全セクターのEDC/ECC、日本版SCPH-9000と同一のv4.0 BIOSによる通常起動も確認した。

最終CUE:

`C:/Users/Owner/MipsyncProjects/Train/Builds/PS1/Train/out-110568-2561921015/game.cue`

- `tmp/train-floor-gap-final-build.log`
- `tmp/train-floor-gap-disc-verification.json`
- `tmp/train-floor-gap-jp-boot.log`

処理量の限界: 画面端をまたぐ面を正しくクリップするためCPU負荷は増える。同じTrainの360度回転測定で、修正前11.5fpsに対して修正後9.3fpsだった。PS1自体のアフィンテクスチャの歪みは残るが、上記の検証では床の面の隙間はなくなった。
