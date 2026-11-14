#include "RmlUiRuntime.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Text/TextSettings.h>
#include <Events/ApplicationEvents.h>
#include <RmlUi/Core.h>

namespace BECore {

    RmlUiRuntime::~RmlUiRuntime() {
        Shutdown();
    }

    bool RmlUiRuntime::Acquire() {
        if (!_isUp && !Startup()) {
            return false;
        }
        ++_holders;
        return true;
    }

    void RmlUiRuntime::Release() {
        if (_holders == 0) {
            return;
        }
        --_holders;
        if (_holders == 0) {
            Shutdown();
        }
    }

    Rml::String RmlUiRuntime::NextContextName() {
        // std: Rml::String is a std::string
        return Rml::String("RmlUiDocument") + std::to_string(_nextContext++);
    }

    bool RmlUiRuntime::Startup() {
        Rml::SetSystemInterface(&_system);
        Rml::SetRenderInterface(&_render);
        Rml::SetFileInterface(&_files);
        // The library has no font engine of its own in this build (RMLUI_FONT_ENGINE is none); the engine's `Font` draws the text
        Rml::SetFontEngineInterface(&_fonts);
        if (!Rml::Initialise()) {
            LOG_ERROR("RmlUiRuntime: Rml::Initialise failed");
            return false;
        }
        _isUp = true;

        const TextSettings settings = TextSettings::FromConfig(CoreManager::GetConfigManager().GetConfig("TextConfig"_intern));
        const Rml::String fontPath(settings.defaultFont.ToStringView().data(), settings.defaultFont.ToStringView().size());
        if (!Rml::LoadFontFace(fontPath)) {
            LOG_ERROR("RmlUiRuntime: cannot load the font '{}'"_format(settings.defaultFont.ToStringView()));
        }

        // Subscribed once, after the renderer is up (its own cleanup handler is older, and EnTT calls the newest handler first, so this one runs before the renderer is destroyed)
        if (!_subscribed) {
            _subscribed = true;
            Subscribe<ApplicationEvents::ApplicationCleanUpEvent, &RmlUiRuntime::Shutdown>(this);
        }
        return true;
    }

    void RmlUiRuntime::Shutdown() {
        if (!_isUp) {
            return;
        }
        _isUp = false;
        Rml::Shutdown();
        _render.Clear();
    }

}  // namespace BECore
