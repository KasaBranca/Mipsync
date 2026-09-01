# Mips# for Mipsync Engine

VSCode / Cursor 向けの Mips# 拡張です。

## できること

- `.mips` のシンタックスハイライト
- MipsyncEngine の実コンパイラによる赤波線診断
- `transform.`, `Input.`, `Physics.`, `AudioSource.` などの補完
- Mips# API の hover
- 同一ファイル内の簡易定義ジャンプ

## 開発中に使う

1. VSCode/Cursor で `tools/vscode-mips` を Extension Development Host として開きます。
2. 必要なら設定で `mipsync.enginePath` に `D:\Nostalty\build\src\MipsyncEngine.exe` を指定します。
3. `.mips` ファイルを開くと Language Server が自動起動します。

この拡張は `tools/mips-language-server` を直接起動します。診断は `MipsyncEngine.exe --validate-mips` を使うため、エディタ上で通ったのにビルドで落ちる、というズレを避ける設計です。
