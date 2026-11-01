#include <BECore/Renderer/IRenderer.h>
#include <Generated/IRenderer.gen.hpp>

namespace BECore {

    void IRenderer::RequestCapture(CaptureCallback callback) {
        if (callback) {
            callback(std::unexpected(eastl::string("this renderer backend cannot read its output back")));
        }
    }

}  // namespace BECore
