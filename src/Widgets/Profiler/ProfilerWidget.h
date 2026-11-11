#pragma once

#include <BECore/Widgets/IWidget.h>
#include <Widgets/EditorLayout/EditorCommands.h>

namespace BECore {

    namespace SDLEvents {
        struct SDLEventWrapper;
    }

    /**
     * @brief The editor's Profiler panel: the frame time plot, the last frame's statistics, the heap, the allocations per zone and the
     * Chrome trace controls.
     *
     * Hidden at start; Ctrl+Shift+P (read from the raw SDL event, Profiler/ProfilerToggle.h) or the command `view.profiler` shows and
     * hides it, and its close button hides it. Editor only (src/Widgets/Profiler/ is in the editor folders of src/CMakeLists.txt).
     */
    class ProfilerWidget : public IWidget {
        BE_CLASS(ProfilerWidget)
    public:
        ProfilerWidget() = default;
        ~ProfilerWidget() override = default;

        BE_FUNCTION bool Initialize(IDeserializer& deserializer) override;
        BE_FUNCTION void Update() override;
        BE_FUNCTION void Draw() override;

    private:
        void OnCommand(const EditorCommandEvent& event);
        void OnSDLEvent(const SDLEvents::SDLEventWrapper& event);

        void DrawFrames();
        void DrawMemory();
        void DrawZoneAllocs();
        void DrawTrace();

        BE_REFLECT_FIELD bool _open = false;
        eastl::string _traceMessage;
        char _tracePath[256] = "trace.json";
    };

}  // namespace BECore
