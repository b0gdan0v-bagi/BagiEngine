#pragma once

#include <BECore/MainWindow/WindowSimulation.h>
#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Reflection/IPropertyVisitor.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/StringUtils.h>

namespace BECore {

    /// The names of the fields of ControlFlags (the field names without the `_`): the keys a CommandLineDeserializer is asked for them by
    namespace ControlFlagFields {
        inline constexpr StringView kReplay = "replay";
        inline constexpr StringView kReplayExit = "replayExit";
        inline constexpr StringView kWindow = "window";
        inline constexpr StringView kDensity = "density";
    }  // namespace ControlFlagFields

    /**
     * @brief The control settings that exist only as command-line flags (`--replay=FILE`, `--replay-exit`, `--window=WxH`,
     *        `--density=D`), read by a CommandLineDeserializer in ResolveControlSettings.
     *
     * They are not attributes of ApplicationConfig.xml: a new attribute would change a public format. Every field has the value of
     * "not given"; the combination rules and the limits are ResolveControlSettings's.
     */
    struct ControlFlags {
        BE_CLASS(ControlFlags)

        BE_REFLECT_FIELD String _replay;
        BE_REFLECT_FIELD bool _replayExit = false;
        BE_REFLECT_FIELD Optional<WindowSize> _window;
        BE_REFLECT_FIELD Optional<float> _density;
    };

    namespace Ser {

        /// A window size is one text, `WxH` (`480x480`): the form of `--window=`, which a flag cannot hold as an object
        template <>
        struct DataAccessor<WindowSize> {
            static void Save(ISerializer& s, const WindowSize& value, StringView key) {
                const String text = Format("{}x{}", value.width, value.height);
                s.WriteAttribute(key, StringView(text));
            }

            static bool Load(IDeserializer& d, WindowSize& value, StringView key) {
                String text;
                if (!d.ReadAttribute(key, text)) {
                    return false;
                }
                const StringView view(text);
                const size_t separator = view.find('x');
                if (separator != StringView::npos) {
                    const auto width = StringUtils::ParseNumber<int>(view.substr(0, separator));
                    const auto height = StringUtils::ParseNumber<int>(view.substr(separator + 1));
                    if (width && height) {
                        value = WindowSize{*width, *height};
                        return true;
                    }
                }
                d.ReportError(key, "is not a window size: two integers, WxH (for example 480x480)");
                return false;
            }
        };

    }  // namespace Ser

    namespace PropVisit {

        /// A start-up setting of the command line is not edited in the inspector
        inline bool Visit(IPropertyVisitor& /*v*/, WindowSize& /*value*/, StringView /*key*/) {
            return false;
        }

    }  // namespace PropVisit

}  // namespace BECore
