#pragma once

#include <RmlUiHost/RmlUiFileInterface.h>
#include <RmlUiHost/RmlUiRenderInterface.h>
#include <RmlUiHost/RmlUiSystemInterface.h>

namespace BECore {

    /**
     * @brief RmlUi's global state: the interfaces it calls and the library itself, brought up for as long as any document needs it.
     *
     * RmlUi is initialised once per process. Every RmlUiDocumentComponent takes a hold with Acquire() and gives it back with Release(); the first one installs
     * the interfaces, initialises the library and loads the default font of `TextConfig.xml` (its family name, "PT Sans" for the shipped one, is what a document's
     * `font-family` names), and the last one shuts it down. The library is also shut down when the application cleans up, before the renderer goes, because the
     * textures it made belong to that renderer (the holders that are left then find it down). Main thread only.
     */
    class RmlUiRuntime : public Singleton<RmlUiRuntime>, public SubscriptionHolder {
    public:
        ~RmlUiRuntime() override;

        /// Takes a hold. @return false when RmlUi could not be initialised
        bool Acquire();

        /// Gives a hold back; the last one shuts RmlUi down. A call without a hold is ignored
        void Release();

        [[nodiscard]] bool IsUp() const {
            return _isUp;
        }
        [[nodiscard]] int GetHolderCount() const {
            return _holders;
        }
        [[nodiscard]] RmlUiRenderInterface& GetRenderInterface() {
            return _render;
        }

        /// A name for a new context, unique for the life of the process
        [[nodiscard]] Rml::String NextContextName();

    private:
        bool Startup();
        void Shutdown();

        RmlUiRenderInterface _render;
        RmlUiSystemInterface _system;
        RmlUiFileInterface _files;
        int _holders = 0;
        uint32_t _nextContext = 1;
        bool _isUp = false;
        bool _subscribed = false;
    };

}  // namespace BECore
