#pragma once

#include <BECore/Types/Types.h>
#include <Widgets/RemoteEditor/RemoteSession.h>

namespace BECore {

    /// The link of a RemoteSession in a sentence, for the panels that show it
    class RemoteLinkStatus {
    public:
        /// Makes the sentence again when the state, the stall, the address or the reason of @p session changed; true when it did
        bool Update(const RemoteSession& session);

        /// Forgets the sentence, so the next Update() makes it
        void Reset() {
            _state.reset();
        }

        /// The state with the address and, for a lost link, the reason
        [[nodiscard]] const String& GetText() const {
            return _text;
        }

        /// The link is lost or the program does not answer: the sentence is a warning
        [[nodiscard]] bool IsTroubled() const {
            return _troubled;
        }

    private:
        String _text;
        bool _troubled = false;
        // What the sentence was made from; nullopt before the first one
        Optional<RemoteSessionState> _state;
        bool _stalled = false;
        String _address;
        String _reason;
    };

}  // namespace BECore
