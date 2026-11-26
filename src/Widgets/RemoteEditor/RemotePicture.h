#pragma once

#include <BECore/Control/JsonView.h>
#include <BECore/Types/Types.h>
#include <CoreSDL/ImageFile.h>
#include <Widgets/RemoteEditor/RemoteCallError.h>
#include <cstdint>
#include <expected>

namespace BECore {

    /**
     * @brief The last frame of a program's screen that a `screenshot` brought, as RGBA
     *
     * The answers are a peer's: one that cannot be shown leaves the frame and sets the error. An answer that repeats the frame, or the last
     * picture that could not be shown, costs a compare and allocates nothing.
     */
    class RemotePicture {
    public:
        /// Takes the `result` of a `screenshot`
        void Load(JsonView result);

        /// Takes the `error` of a refused `screenshot`
        void Refuse(JsonView error) {
            _error.Read(error);
        }

        void ClearError() {
            _error.Clear();
        }

        /// Forgets the frame and the error
        void Clear();

        /// No pixels while there is no frame
        [[nodiscard]] const RgbaImage& GetImage() const {
            return _image;
        }

        /// Moves when GetImage() changes, going away included, and never goes back: what a texture of the frame is keyed by
        [[nodiscard]] uint64_t GetRevision() const {
            return _revision;
        }

        /// Why the latest answer gave no frame
        [[nodiscard]] const RemoteCallError& GetError() const {
            return _error;
        }

    private:
        [[nodiscard]] std::expected<RgbaImage, String> Decode(JsonView result, JsonView png);

        RgbaImage _image;
        uint64_t _revision = 0;
        RemoteCallError _error;
        /// The `png_base64` as written: of the frame, and of the latest picture that could not be shown
        String _shownSource;
        String _brokenSource;
        String _brokenReason;
        // Members for their capacity
        String _text;
        Vector<uint8_t> _png;
    };

}  // namespace BECore
