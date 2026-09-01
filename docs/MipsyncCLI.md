# MipsyncCLI

MipsyncCLI is the external frontend for Mipsync's shared Command Platform. The Editor Console,
the `mipsync` executable, and future adapters consume the same command descriptors, validation,
typed results, symbol metadata, and executor.

## Agent Skill

The reusable Agent Skill for live project authoring is distributed at
`skills/mipsync-cli/`. It teaches compatible agents to route the correct Editor
instance, discover the current command surface, use structured JSON, verify
mutations, preserve Undo, validate Mips#, and save only completed work. Engine
builds and Windows release packages copy the Skill beside the editor so its
instructions stay versioned with the CLI protocol.

When a project is opened, the Editor synchronizes that bundled copy to
`.agents/skills/mipsync-cli/` inside the project. It also creates or updates a
marked Mipsync section in the project's `AGENTS.md`, requiring agents to invoke
`$mipsync-cli` for Mipsync project operations. Existing `AGENTS.md` content
outside the marked section is preserved. The CLI repeats this check on use and,
for live commands, takes the Skill from the selected Editor executable's
version directory. The synchronized Skill also receives a generated
`references/runtime.md` binding with the exact matching CLI path, so agents do
not have to search `PATH` or mistake a game build executable for the tool.

## Quick start

```powershell
mipsync help
mipsync capabilities --json
mipsync search "move object every frame"
mipsync describe Transform.position
mipsync language compile assets/scripts/Player.mips --json
```

The user starts Mipsync Editor normally. During Editor startup it starts and owns a Command Host
for that project. Each `mipsync` invocation is a short-lived CLI client that discovers the live
Editor session, sends one typed request, and exits. The CLI never starts the Editor itself.

Commands that inspect or control live Editor state are routed to the correct running Editor instance:

```powershell
mipsync instances
mipsync --project D:\Games\MyGame scene inspect
mipsync --project D:\Games\MyGame scene get-json --json
mipsync --project D:\Games\MyGame entity list --json
mipsync --project D:\Games\MyGame entity inspect Player
mipsync --project D:\Games\MyGame entity select Player
mipsync --project D:\Games\MyGame entity duplicate Crate --name CrateCopy
mipsync --project D:\Games\MyGame entity set CrateCopy --static true --tag Environment
mipsync --project D:\Games\MyGame entity create Crate --primitive cube --x 2 --y 1 --z -3
mipsync --project D:\Games\MyGame entity transform Crate --x 4 --ry 45
mipsync --project D:\Games\MyGame entity set-parent Crate Environment
mipsync --project D:\Games\MyGame material create assets/materials/Brick.nmat 0.7 0.2 0.1
mipsync --project D:\Games\MyGame material apply Crate assets/materials/Brick.nmat
mipsync --project D:\Games\MyGame component add Crate Collider
mipsync --project D:\Games\MyGame entity delete Crate --confirm true
mipsync --project D:\Games\MyGame editor undo
mipsync --project D:\Games\MyGame ide open assets/scripts/Player.mips --line 12
mipsync --project D:\Games\MyGame scene save
mipsync --project D:\Games\MyGame runtime play
mipsync --project D:\Games\MyGame runtime stop
```

When the current directory is inside a Mipsync project, `--project` is optional. If multiple
Editors are open for the same project, select one with `--instance <instanceId>`.

Authoring commands execute on the Editor main thread. Created entities are selected and framed in
Scene View; transformed or reparented entities are selected in Hierarchy and Inspector. Incoming
commands and failures are also written to the Editor Console with a `[CLI]` prefix, so the user can
watch an agent work in real time. Closing the Editor ends its CLI session; subsequent Editor-only
commands fail instead of launching another Editor.

## Editor Console

The Editor Console accepts the same command syntax at its bottom input field. It executes the
shared `CommandExecutor` in-process; it never launches the external CLI.

## Protocol

External Editor commands use a versioned JSON request over a local named pipe on Windows and a
Unix domain socket on macOS/Linux:

```json
{
  "protocolVersion": 1,
  "requestId": "42",
  "command": "entity.inspect",
  "arguments": { "entity": "Player" },
  "projectPath": "D:\\Games\\MyGame"
}
```

Responses contain typed data and structured diagnostics. Human output and `--json` output are
rendered from that same result; human text is never parsed to produce JSON.

## MVP command set

| Command | Mode | Side effect |
| --- | --- | --- |
| `help` | Local | Read only |
| `capabilities` | Local | Read only |
| `instances` | Local | Read only |
| `search` | Local | Read only |
| `describe` | Local | Read only |
| `project.inspect` | Local | Read only |
| `language.compile` | Local | Read only |
| `scene.inspect` | Editor | Read only |
| `scene.get-json` | Editor | Read only |
| `scene.patch` | Editor | Dangerous editor mutation |
| `scene.save` | Editor | Project mutation + filesystem write |
| `entity.list` | Editor | Read only |
| `entity.inspect` | Editor | Read only |
| `entity.select` | Editor | Editor selection |
| `entity.set` | Editor | Editor mutation |
| `entity.duplicate` | Editor | Editor mutation |
| `entity.create` | Editor | Editor mutation |
| `entity.transform` | Editor | Editor mutation |
| `entity.set-parent` | Editor | Editor mutation |
| `entity.delete` | Editor | Dangerous editor mutation |
| `component.add` | Editor | Editor mutation |
| `component.remove` | Editor | Dangerous editor mutation |
| `component.set-enabled` | Editor | Editor mutation |
| `mesh.set` | Editor | Editor mutation |
| `material.create` | Editor | Project mutation + filesystem write |
| `material.apply` | Editor | Editor mutation |
| `editor.undo` | Editor | Editor mutation |
| `editor.redo` | Editor | Editor mutation |
| `ide.open` | Editor | External process |
| `runtime.inspect` | Editor | Read only |
| `runtime.play` | Editor | Runtime mutation |
| `runtime.stop` | Editor | Runtime mutation |

The descriptor schema already models hierarchical IDs, typed parameters, execution mode,
result type, examples, and side-effect capabilities. New commands register once and are then
available to help, discovery, Editor Console, CLI, and future protocol adapters.

Authoring mutations are rejected during Play Mode and each successful mutation records one Editor
Undo snapshot. Deletion recursively removes children, requires `--confirm true`, and can be
reverted with the normal Editor Undo command before saving.
