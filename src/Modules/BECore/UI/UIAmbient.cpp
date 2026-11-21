#include "UIAmbient.h"

#include <BECore/UI/UIStrings.h>
#include <Generated/UIAmbient.gen.hpp>

namespace BECore::UIAmbient {

    std::expected<UIAmbientLevels, LinkError> Declare(ContextDecl& declaration) {
        UIAmbientLevels levels;
        const auto language = declaration.Add<UILanguageView>(kLanguageLevel);
        if (!language) {
            return std::unexpected(language.error());
        }
        levels.language = *language;
        const auto window = declaration.Add<UIWindowView>(kWindowLevel);
        if (!window) {
            return std::unexpected(window.error());
        }
        levels.window = *window;
        return levels;
    }

    void Fill(Context& context, const UIAmbientLevels& levels, const UIStrings& strings, const UIWindowView& window, const uint64_t& windowVersion) {
        context.Set(levels.language, strings.GetLanguageView(), strings.GetVersionCounter());
        context.Set(levels.window, window, &windowVersion);
    }

}  // namespace BECore::UIAmbient
