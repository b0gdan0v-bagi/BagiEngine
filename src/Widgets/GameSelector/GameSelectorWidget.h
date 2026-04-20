#pragma once

#include <BECore/Widgets/IWidget.h>

namespace BECore {

    class IDeserializer;

    /**
     * @brief ImGui combo box that switches between registered IGame instances.
     *
     * Lists every game registered in AbstractFactory<IGame>, lets the user pick one,
     * and triggers GameManager::SwitchTo on confirmation. A second button restarts
     * the active game via IGame::Reset.
     */
    class GameSelectorWidget : public IWidget {
        BE_CLASS(GameSelectorWidget)
    public:
        GameSelectorWidget() = default;
        ~GameSelectorWidget() override = default;

        BE_FUNCTION bool Initialize(IDeserializer& deserializer) override;
        BE_FUNCTION void Update() override;
        BE_FUNCTION void Draw() override;

    private:
        int _selectedIndex = -1;
    };

}  // namespace BECore
