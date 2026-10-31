#include "SceneRenderWidget.h"

#include <BECore/GameManager/CoreManager.h>
#include <Generated/SceneRenderWidget.gen.hpp>

namespace BECore {

    bool SceneRenderWidget::Initialize(IDeserializer& /*deserializer*/) {
        return true;
    }

    void SceneRenderWidget::Update() {}

    void SceneRenderWidget::DrawScreen() {
        CoreManager::GetSceneManager().DrawAll();
    }

    void SceneRenderWidget::Draw() {}

}  // namespace BECore
