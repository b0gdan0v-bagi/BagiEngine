#pragma once

#include <BECore/Types/Types.h>
#include <Widgets/RemoteEditor/RemoteSession.h>

namespace BECore {

    /// What became of the last save of a RemoteSession in a sentence, for the panel that asked for it
    class RemoteSaveLine {
    public:
        /// Makes the sentence again when the save status of @p status changed; true when it did
        bool Update(const RemoteSaveStatus& status);

        /// Forgets the sentence, so the next Update() makes it
        void Reset() {
            _shown = {};
            _text.clear();
        }

        /// Empty while no save was asked for. It holds the program's words
        [[nodiscard]] const String& GetText() const {
            return _text;
        }

        /// The scene is not known to be saved: the sentence is a warning
        [[nodiscard]] bool IsTroubled() const {
            return _shown.state == RemoteSaveState::Refused || _shown.state == RemoteSaveState::Unconfirmed;
        }

    private:
        /// What the sentence was made from
        RemoteSaveStatus _shown;
        String _text;
    };

}  // namespace BECore
