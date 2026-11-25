#include "SDL3ApplicationFabric.h"

#include <BECore/Application/ApplicationConfig.h>
#include <BECore/Audio/AudioManager.h>
#include <BECore/Audio/NullAudioBackend.h>
#include <BECore/Clipboard/Clipboard.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/MainWindow/MainWindowManager.h>
#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Renderer/RendererManager.h>
#include <CoreSDL/SDLAudioBackend.h>
#include <CoreSDL/SDLClipboard.h>
#include <CoreSDL/SDLEventsProvider.h>
#include <CoreSDL/SDLMainWindow.h>
#include <CoreSDL/SDLScreenshot.h>
#include <Events/EventsProviderManager.h>
#include <Generated/SDL3ApplicationFabric.gen.hpp>

namespace BECore {

    namespace {

        constexpr StringView kSdlRendererName = "SDLRendererBackend";
        constexpr StringView kSdlAudioName = "SDLAudioBackend";
        constexpr StringView kVulkanRendererName = "VulkanRendererBackend";

        /// The one line a runner greps (`tools/control/game_process.py`, BACKEND_UNAVAILABLE_MARKER) to tell "this machine lacks the backend" from a crash
        void LogBackendUnavailable(RenderBackendChoice backend, StringView reason) {
            LOG_ERROR("Backend '{}' is not available on this machine: {}", RenderBackendName(backend), reason);
        }

        /// The backend AudioConfig names (empty: SDL's); one that is not registered or does not open leaves the game silent, never stopped
        void CreateAudio(bool headless) {
            AudioManager& audio = CoreManager::GetAudio();
            const String& configured = audio.GetSettings()._backend;
            const StringView name = configured.empty() ? kSdlAudioName : StringView(configured);
            IntrusivePtr<IAudioBackend> backend = AbstractFactory<IAudioBackend>::GetInstance().Create(name);
            if (!backend) {
                LOG_ERROR("AudioConfig: backend '{}' is not available in this build", name);
            } else if (auto* sdl = dynamic_cast<SDLAudioBackend*>(backend.Get())) {
                sdl->SetHeadless(headless);
            }
            audio.SetBackend(std::move(backend));
            if (!audio.GetBackend()) {
                audio.SetBackend(New<NullAudioBackend>());
            }
        }

    }  // namespace

    bool SDL3ApplicationFabric::Create(const ApplicationConfig& config, const ApplicationOptions& options) {
        BE_PROFILE_SCOPE("SDL3ApplicationFabric::Create");
        if (config._path.empty()) {
            LOG_ERROR("ApplicationConfig: no path (the window config of SDL3ApplicationFabric)");
            return false;
        }

        auto window = BECore::New<SDLMainWindow>();
        window->SetHeadless(options.headless);
        window->SetHeadlessSimulation(options.windowSize, options.density);
        window->SetStartOptions(options.hiddenWindow, options.backend);
        const bool explicitBackend = options.backend != RenderBackendChoice::Auto;
        if (!window->Initialize(config._path)) {
            if (explicitBackend) {
                LogBackendUnavailable(options.backend, "the window cannot be created, the error above says why");
            }
            return false;
        }
        CoreManager::GetInstance().GetMainWindowManager().SetMainWindow(window);

        // Headless drawing is the SDL software renderer: the Vulkan backend needs a real surface
        StringView rendererTypeName = options.headless || config._renderer.empty() ? kSdlRendererName : StringView(config._renderer);
        if (options.backend == RenderBackendChoice::Vulkan) {
            rendererTypeName = kVulkanRendererName;
        } else if (explicitBackend) {
            rendererTypeName = kSdlRendererName;
        }
        if (options.headless) {
            LOG_INFO("Headless: no window, drawing with {}", rendererTypeName);
        }

        auto renderer = AbstractFactory<IRenderer>::GetInstance().Create(rendererTypeName);
        if (!renderer) {
            // A name that is not registered, e.g. VulkanRendererBackend in a build without ENGINE_WITH_VULKAN
            LOG_ERROR("ApplicationConfig: renderer '{}' is not available in this build", rendererTypeName);
            if (explicitBackend) {
                LogBackendUnavailable(options.backend, Format("'{}' is not registered in this build", rendererTypeName));
            }
            return false;
        }
        renderer->SetUncapped(options.hiddenWindow);
        if (!renderer->Initialize(*window)) {
            if (explicitBackend) {
                LogBackendUnavailable(options.backend, "the renderer cannot be created, the error above says why");
            }
            return false;
        }
        CoreManager::GetRendererManager().SetRenderer(std::move(renderer));
        RegisterScreenshotControl(CoreManager::GetControlService());
        CreateAudio(options.headless);  // after the window: its clean-up handler runs before the window quits SDL

        auto sdlEventsProvider = BECore::New<SDLEventsProvider>();
        if (!sdlEventsProvider->Initialize()) {
            return false;
        }
        CoreManager::GetEventsProviderManager().RegisterProvider(sdlEventsProvider);
        // A headless run (the dummy driver) keeps the clipboard in the memory of SDL, so a control smoke does not touch the system one
        CoreManager::GetClipboard().SetBackend(MakePlatformClipboardBackend());

        return true;
    }

}  // namespace BECore
