#pragma once

#include "../project/Project.h"
#include <string>
#include <vector>

namespace MipsyncEngine {

struct ProjectBootstrapResult {
    std::string projectPath;
    std::string playerDataDirectory;
    std::string projectName;
    std::string startupSceneRelativePath;
    std::vector<std::string> buildScenes;
    ProjectInfo projectInfo;
};

class ProjectBootstrap {
public:
    static ProjectBootstrapResult Resolve(const std::string& requestedProjectPath,
                                          const std::string& playerDataDirectory,
                                          bool playerMode);
    static void EnsureProjectDirectories(const std::string& projectRoot);
};

} // namespace MipsyncEngine
