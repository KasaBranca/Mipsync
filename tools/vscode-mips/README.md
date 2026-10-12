# Mips# for Mipsync Engine

VSCode / Cursor 向けの Mips# 拡張です。

## できること

- `.mips` の構文・意味に応じた色分け（フィールド、ローカル変数の参照、引数、列挙値、型、演算子、配列、プロパティ、コルーチン）
- MipsyncEngine の実コンパイラによる赤波線診断
- `transform.`, `Input.`, `Physics.`, `Camera.`, `UI.`, `AudioSource.` など、実コンパイラの組み込み API の補完
- Mips# API の hover
- 同一ファイル内の簡易定義ジャンプ

## 開発中に使う

1. VSCode/Cursor で `tools/vscode-mips` を Extension Development Host として開きます。
2. 必要なら設定で `mipsync.enginePath` に `D:\Nostalty\build\src\MipsyncEngine.exe` を指定します。
3. `.mips` ファイルを開くと Language Server が自動起動します。

この拡張は同梱の `mips-language-server` を起動します。診断は `mipsync.exe language compile` を使います。

## スクリプトの書式

クラス直下に `public` フィールドを用途ごとにまとめ、その後に `Start`・`Update` などのメソッドを置きます。一時変数にも `float`・`bool` などの型を明記できます。`var` も引き続き使えます。

## 検証

`vscode-textmate` と `vscode-oniguruma` を開発依存としてインストール後、`npm test` で実際の TextMate エンジン、全識別子の意味色分け、API とコンパイラの対応、補完・hover を確認できます。
## インスペクター属性

公開フィールドには `[Header("Movement")]`, `[Space(12)]`, `[Tooltip("Help")]`,
`[Range(0, 10)]`, `[Min(0)]`, `[ReadOnly]`, `[HideInInspector]`,
`[InspectorName("Move Speed")]`, `[Order(10)]` が使えます。
フィールドの前で `[` を入力すると属性を補完します。属性名の色分け、hover の説明、
実コンパイラによる診断に対応しています。表示順やラベルを変更しても保存値と
PS1 用バイトコードのフィールド番号は維持されます。
