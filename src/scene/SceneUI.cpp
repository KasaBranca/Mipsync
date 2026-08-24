#include "Scene.h"
#include "../ui/UIRenderer.h"

namespace MipsyncEngine {

void Scene::RenderUI(UIRenderer& uiRenderer, const Camera& camera, int viewportWidth, int viewportHeight,
                     Framebuffer* targetFBO, uint32_t activeCameraEntityId, bool sceneView3D,
                     int layoutWidth, int layoutHeight) {
    uiRenderer.Render(*this, camera, viewportWidth, viewportHeight, targetFBO, activeCameraEntityId,
                      sceneView3D, layoutWidth, layoutHeight);
}

} // namespace MipsyncEngine
