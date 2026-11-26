#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Types/Types.h>
#include <Widgets/RemoteEditor/RemoteCallError.h>
#include <Widgets/RemoteEditor/RemoteLinkStatus.h>
#include <cstdint>

namespace BECore {

    class RemotePicture;
    class RemoteSession;

    /// A point of a picture as fractions 0..1 of its sides, from its top-left corner
    struct RemotePicturePoint {
        double x = 0.0;
        double y = 0.0;
    };

    /// Where a picture of @p width by @p height pixels lies in @p area: as large as fits with its aspect kept, centred, with bars on two sides.
    /// An empty rect when the picture or the area has no size
    [[nodiscard]] Rect FitRemotePicture(const Rect& area, uint32_t width, uint32_t height);

    /// The point @p x, @p y within the picture drawn at @p image; nothing outside it
    [[nodiscard]] Optional<RemotePicturePoint> PointOfRemotePicture(const Rect& image, float x, float y);

    /**
     * @brief The Viewport's content in remote mode: the attached program's picture, on which a click selects, and what stands in its way
     *
     * Nothing is forwarded to a game, this process's or the attached one. It owns the texture of the frame, so it ends, or is released, while the
     * renderer lives. A frame with no new picture and no new message allocates nothing.
     */
    class RemoteViewportView {
    public:
        RemoteViewportView() = default;
        ~RemoteViewportView() {
            Release();
        }

        RemoteViewportView(const RemoteViewportView&) = delete;
        RemoteViewportView& operator=(const RemoteViewportView&) = delete;

        /// Draws into the current ImGui window and asks the session for the picture. Needs a frame in progress
        void Draw(RemoteSession& session);

        /// Gives the texture back, after the GPU is done with it
        void Release();

    private:
        /// The sentence of a RemoteCallError, made again only when the error changed
        struct Message {
            RemoteCallError source;
            String text;
        };

        void UpdateTexture(const RemotePicture& picture);
        void UpdateMessages(const RemoteSession& session);
        void DrawImage(RemoteSession& session);

        RemoteLinkStatus _link;
        Message _pictureError;
        Message _pickError;
        IntrusivePtr<ITexture> _texture;
        /// The revision of the picture the texture was made of; 0 is that of a session that has had no frame
        uint64_t _revision = 0;
    };

}  // namespace BECore
