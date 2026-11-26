#include "ControlLinkPage.h"

#if BE_CONTROL_LINK

#include <RoomClient/EmscriptenRoomTransport.h>
#include <cstdlib>
#include <emscripten/em_asm.h>
#include <emscripten/em_js.h>

// The JavaScript of EM_ASM calls this runtime function; EM_ASM code does not pull it in by itself
EM_JS_DEPS(bagi_control_link, "$stringToNewUTF8");

namespace BECore {

    namespace {

        /// `location.search` of the page
        [[nodiscard]] String ReadPageQuery() {
            // The JavaScript makes the copy with malloc
            char* const text = static_cast<char*>(EM_ASM_PTR({ return stringToNewUTF8(window.location.search); }));
            String query(text != nullptr ? text : "");
            std::free(text);
            return query;
        }

    }  // namespace

    Optional<ControlLinkSettings> ReadPageControlLinkSettings() {
        auto parsed = ParseControlLinkQuery(ReadPageQuery());
        if (!parsed) {
            LOG_WARNING("Control link: {}; the page does not dial", parsed.error());
            return std::nullopt;
        }
        return std::move(*parsed);
    }

    UniquePtr<ControlLink> StartPageControlLink(ControlService& service, ControlLinkSettings settings) {
        RefuseHostFileMethods(service);
        return MakeUnique<ControlLink>(service, std::move(settings), []() -> UniquePtr<IRoomTransport> { return MakeUnique<EmscriptenRoomTransport>(ControlLink::kTransportLimits); });
    }

}  // namespace BECore

#endif  // BE_CONTROL_LINK
