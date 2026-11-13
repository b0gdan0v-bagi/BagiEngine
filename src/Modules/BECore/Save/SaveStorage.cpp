#include "SaveStorage.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/em_asm.h>
#include <emscripten/threading.h>
#endif

namespace BECore {

    namespace {

        eastl::function<void()>& Handler() {
            static eastl::function<void()> handler;
            return handler;
        }

#ifdef __EMSCRIPTEN__
        // The page's script and its mount live on the main thread (a Worker has no window)
        void CheckOwner() {
            ASSERT(emscripten_is_main_browser_thread(), "The save storage is used off the main thread");
        }

        // The JavaScript of EM_ASM is stringized as it stands, so clang-format must not touch it (it splits === into == =)
        // clang-format off
        // 1 persistent, 0 anything else (the page has no Module.bagiSaves, or its state is "memory")
        bool PageSavesArePersistent() {
            return EM_ASM_INT({
                var saves = Module.bagiSaves;
                return saves && saves.state === 'persistent' ? 1 : 0;
            }) != 0;
        }

        void MarkPageDirty() {
            EM_ASM({
                var saves = Module.bagiSaves;
                if (saves && typeof saves.markDirty === 'function') {
                    saves.markDirty();
                }
            });
        }
        // clang-format on
#endif

    }  // namespace

    std::filesystem::path PersistentSaveDirectory() {
#ifdef __EMSCRIPTEN__
        return std::filesystem::path(kWebSaveDirectory.data(), kWebSaveDirectory.data() + kWebSaveDirectory.size());
#else
        return {};
#endif
    }

    SaveStorageState GetSaveStorageState() {
#ifdef __EMSCRIPTEN__
        CheckOwner();
        return PageSavesArePersistent() ? SaveStorageState::Persistent : SaveStorageState::NotRemembered;
#else
        return SaveStorageState::NotApplicable;
#endif
    }

    void NotifySaveChanged() {
        if (const auto& handler = Handler()) {
            handler();
            return;
        }
#ifdef __EMSCRIPTEN__
        CheckOwner();
        MarkPageDirty();
#endif
    }

    void SetSaveChangedHandler(eastl::function<void()> handler) {
        Handler() = std::move(handler);
    }

}  // namespace BECore
