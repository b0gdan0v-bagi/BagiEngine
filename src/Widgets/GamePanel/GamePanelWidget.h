#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Widgets/IWidget.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/GamePanel/GamePanelModel.h>
#include <cstddef>

namespace BECore {

    /**
     * @brief The editor's Game panel: the active game's state, its actions as buttons, its reflected fields and its `BE_FUNCTION`s.
     *
     * Hidden at start; the command `view.game` shows and hides it. Editor only (src/Widgets/GamePanel/ is in the editor folders of
     * src/CMakeLists.txt). Knows no game by name: everything goes through IGame, IControllableGame and the reflection (GamePanelModel).
     */
    class GamePanelWidget : public IWidget {
        BE_CLASS(GamePanelWidget)
    public:
        GamePanelWidget() = default;

        bool Initialize(IDeserializer& deserializer) override;
        void Update() override;
        void Draw() override;

        /// Bytes of the arguments typed for one method
        static constexpr size_t kMaxArgumentsLength = 255;
        static constexpr size_t kMaxFilterLength = 127;

    private:
        void OnCommand(const EditorCommandEvent& event);

        void DrawHeader();
        void DrawActions();
        void DrawFields();
        void DrawMethods();
        void DrawState();
        void DrawLastResult();

        struct ArgumentsBuffer {
            char text[kMaxArgumentsLength + 1] = {};
        };

        BE_REFLECT_FIELD bool _open = false;
        GamePanelModel _model;
        char _filterBuffer[kMaxFilterLength + 1] = {};
        Vector<ArgumentsBuffer> _arguments;  // one per method of the model, made again when the game changes
        IGame* _argumentsFor = nullptr;
    };

}  // namespace BECore
