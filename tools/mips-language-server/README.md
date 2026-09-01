# Mips# Language Server

Mips# 用の軽量 Language Server です。VSCode/Cursor 拡張と同じ診断・補完データを使います。

## 方針

- 診断は `MipsyncEngine.exe --validate-mips` を呼び出し、実ビルド用コンパイラを真実の情報源にします。
- 補完/hover/定義ジャンプは JS 側の軽量解析で即時応答します。
- 将来的に C++ の lexer/parser/type checker をライブラリ化したら、このサーバーから直接呼び出す構造へ差し替えられます。

## 起動

```powershell
node tools/mips-language-server/server.js
```

通常は `tools/vscode-mips` の拡張から自動起動されます。

## 設定

- `mipsync.enginePath`: `MipsyncEngine.exe` への明示パス。
- `mipsync.validationDebounceMs`: 入力後に診断を走らせるまでの待機 ms。
- `mipsync.validationTimeoutMs`: `--validate-mips` のタイムアウト ms。

`enginePath` 未設定時は、リポジトリ内の `build/src/MipsyncEngine.exe` などを自動探索します。
