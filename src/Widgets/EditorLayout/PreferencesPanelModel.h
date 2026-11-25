#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <Widgets/EditorLayout/EditorSettings.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <cstdint>

namespace BECore {

    class IEditorLayoutHost;

    /**
     * @brief What `PreferencesPanel.xml` binds to: the fields of the General tab
     *
     * The controls of the panel write `fontScale`, `viewportGrid`, `viewportGridStep` and `externalEditor` the moment the user changes them; the
     * model reads them in its next `Update`. `error`, `hasError`, `message` and `noFolder` are the model's.
     */
    struct PreferencesPanelView {
        BE_CLASS(PreferencesPanelView)

        BE_REFLECT_FIELD float fontScale = 1.0f;
        BE_REFLECT_FIELD bool viewportGrid = false;
        BE_REFLECT_FIELD int32_t viewportGridStep = 32;
        BE_REFLECT_FIELD String externalEditor;
        /// The text of the last failed save; empty after a save that worked
        BE_REFLECT_FIELD String error;
        BE_REFLECT_FIELD bool hasError = false;
        /// The line shown for `error`
        BE_REFLECT_FIELD String message;
        /// There is no editor host or it holds no settings: the fields are not applied, nor stored
        BE_REFLECT_FIELD bool noFolder = true;
    };

    /**
     * @brief The model of the Preferences window asset: the General tab over the host's `EditorSettings`, with no ImGui
     *
     * The host is the `IEditorLayoutHost` of the editor (`EditorLayoutHostRegistry`); the model never holds it between calls, so a host that
     * goes away leaves the panel with `noFolder`. A changed field is applied to the host's settings at once and the host is told
     * (`NotifyPreferencesChanged`: the font scale is live, the panels read the grid and the editor). It is saved when the user lets go of the
     * control (`preferences.commit`, the `commitCommand` of the controls), and a changed grid check is saved at once. A save that fails sets `error`.
     * The settings clamp what they take, and the view shows the clamped value from the next frame on.
     *
     * Commands (`EditorCommandIds`, run only while the panel is open): `preferences.commit`.
     */
    class PreferencesPanelModel : public IPanelModel {
        BE_CLASS(PreferencesPanelModel)
    public:
        PreferencesPanelModel() = default;

        [[nodiscard]] TypedRef GetObject() override;
        void Update() override;
        /// Reads the view from the host's preferences when the panel opens, so a value changed behind the panel's back is not written over
        void OnOpenChanged(bool open) override;
        bool RunCommand(StringView id, StringView argument) override;

        [[nodiscard]] PreferencesPanelView& GetView() {
            return _view;
        }
        [[nodiscard]] const PreferencesPanelView& GetView() const {
            return _view;
        }

    private:
        /// The fields of the view as they were last read from or given to the settings
        struct Applied {
            float fontScale = 1.0f;
            bool viewportGrid = false;
            int32_t viewportGridStep = 32;
            String externalEditor;
        };

        /// The host when it holds settings; sets `noFolder` either way
        [[nodiscard]] IEditorLayoutHost* FindHost();
        void LoadFromSettings(const EditorPreferences& preferences);
        /// Gives a changed view to the settings; saves at once when the grid check changed
        void ApplyView(IEditorLayoutHost& host);
        void Save(EditorSettings& settings);

        PreferencesPanelView _view;
        Applied _applied;
        bool _loaded = false;  // the view holds the settings' values
    };

}  // namespace BECore
