#pragma once

namespace MipsyncEngine {

struct AnimatorComponent;

/// Play-mode diagnostics: clip stack ids vs transition probes.
void DrawAnimatorRuntimeDiagnostics(AnimatorComponent& animator, bool isPlaying);

} // namespace MipsyncEngine
