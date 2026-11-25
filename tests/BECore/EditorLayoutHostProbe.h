#pragma once

#include <BECore/File/Path.h>
#include <BECore/Types/Types.h>
#include <Widgets/EditorLayout/IEditorLayoutHost.h>

namespace BECore::Tests {

    /// The text a fake host saves as the current layout: it has the dock section that LooksLikeLayoutText asks for
    inline constexpr StringView kProbeLayoutText = "[Docking][Data]\nDockSpace ID=0x1 Pos=0,0 Size=100,100\n";

    /// An IEditorLayoutHost over the folder of a test: a real EditorSettings and LayoutStore (each reset to none to play a host without it) and the counts of the notify calls
    class ProbeEditorLayoutHost : public IEditorLayoutHost {
    public:
        explicit ProbeEditorLayoutHost(const Path& folder) : settings(folder), layouts(folder) {
            settings->Load();
        }

        [[nodiscard]] EditorSettings* GetSettings() override {
            return settings ? &*settings : nullptr;
        }

        [[nodiscard]] LayoutStore* GetLayouts() override {
            return layouts ? &*layouts : nullptr;
        }

        std::expected<void, String> SaveCurrentLayoutAs(StringView name) override {
            if (!layouts) {
                return std::unexpected(String("no layouts folder"));
            }
            if (const auto result = layouts->Save(name, kProbeLayoutText); !result) {
                return std::unexpected(result.error().detail);
            }
            return {};
        }

        void NotifyPreferencesChanged() override {
            ++preferencesNotified;
        }

        void NotifyLayoutsChanged() override {
            ++layoutsNotified;
        }

        Optional<EditorSettings> settings;
        Optional<LayoutStore> layouts;
        int preferencesNotified = 0;
        int layoutsNotified = 0;
    };

}  // namespace BECore::Tests
