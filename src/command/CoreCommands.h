#pragma once

namespace MipsyncEngine::Command {

class CommandRegistry;
class SymbolRegistry;

void RegisterCoreCommands(CommandRegistry& commands, SymbolRegistry& symbols);

} // namespace MipsyncEngine::Command
