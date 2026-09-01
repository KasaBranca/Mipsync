# Mips# IDE Integration

Mips# の IDE 連携は、Language Server Protocol を中心に構成する。

## 目的

- VSCode / Cursor で `.mips` を書くときに、補完・hover・赤波線診断を出す。
- Mipsync Editor 内の将来のスクリプト編集 UI でも同じ解析結果を使えるようにする。
- ビルド用コンパイラと IDE 用解析のズレを避ける。

## 現在の構成

```text
MipsyncEngine.exe --validate-mips
        ↑
        │ compiler diagnostics
        │
tools/mips-language-server
        ↑
        │ LSP over stdio
        │
tools/vscode-mips
```

### `tools/mips-language-server`

Node.js 製の軽量 LSP サーバー。

- `textDocument/publishDiagnostics`
- `textDocument/completion`
- `textDocument/hover`
- `textDocument/definition`

診断は `MipsyncEngine.exe --validate-mips` を呼ぶ。つまり、実際のビルドで使われる Mips# コンパイラが真実の情報源になる。

補完・hover・定義ジャンプは JS 側の軽量解析で実装している。これは初期実装としてレスポンスを軽くするためで、将来的には C++ 側の lexer/parser/type checker を共有ライブラリ化して差し替えられる。

### `tools/vscode-mips`

VSCode / Cursor 向け拡張。

外部依存なしで最小 LSP クライアントを内蔵している。`vscode-languageclient` に依存しないため、リポジトリを clone した状態でも拡張開発ホストで動かしやすい。

## 設定

VSCode / Cursor の設定:

```json
{
  "mipsync.enginePath": "D:\\Nostalty\\build\\src\\MipsyncEngine.exe",
  "mipsync.validationDebounceMs": 350,
  "mipsync.validationTimeoutMs": 8000
}
```

`mipsync.enginePath` が空の場合、Language Server は以下を自動探索する。

- `MIPSYNC_ENGINE_EXE` 環境変数
- `build/src/Release/MipsyncEngine.exe`
- `build/src/MipsyncEngine.exe`
- リポジトリ直下の `MipsyncEngine.exe`

## 次に入れるべきもの

1. ワークスペース全体の symbol index
2. 他 `.mips` ファイルへの定義ジャンプ
3. public field と Inspector 表示の型情報共有
4. Scene / Prefab / Asset path の存在チェック
5. Mipsync Editor 内 LSP client
6. Debug Adapter Protocol

最終的な理想形:

```text
Mips# Core
  lexer / parser / AST / type checker / compiler
        ↓
Mips# Language Server
        ↓
VSCode / Cursor / Mipsync Editor / CLI
```
