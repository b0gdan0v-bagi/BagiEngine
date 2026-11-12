#pragma once

#include <RoomClient/RoomTransport.h>
#include <Rooms/RoomProtocol.h>
#include <gtest/gtest.h>
#include <memory>

namespace BECore::Tests {

    /// One fake connection, shared by the transport the client owns and the test that plays the server
    struct FakeLink {
        eastl::string url;
        /// Set before Open() to make it fail
        std::optional<eastl::string> openError;
        bool openCalled = false;
        /// What the client sent, in order
        eastl::vector<eastl::string> sent;
        /// SendText() answers this; false plays a connection that is closing
        bool sendOk = true;
        bool closeCalled = false;
        uint16_t closeCode = 0;
        eastl::string closeReason;
        bool aborted = false;
        /// The transport object was destroyed (the client let go of it)
        bool destroyed = false;
        /// Close() and Abort() queue the Closed event, as a real transport does
        bool closeAnswers = true;

        /// What the test plays: events the client will poll
        eastl::vector<RoomTransportEvent> inbox;
        size_t nextEvent = 0;
        bool closedQueued = false;

        void PushOpened() {
            RoomTransportEvent event;
            event.kind = RoomTransportEventKind::Opened;
            inbox.push_back(std::move(event));
        }

        void PushText(eastl::string_view text) {
            RoomTransportEvent event;
            event.kind = RoomTransportEventKind::Text;
            event.text = eastl::string(text);
            inbox.push_back(std::move(event));
        }

        /// Queues the one Closed of the connection; a second call does nothing
        void PushClosed(uint16_t code = kRoomTransportAbnormalClosure, eastl::string_view reason = "connection lost") {
            if (closedQueued) {
                return;
            }
            closedQueued = true;
            RoomTransportEvent event;
            event.kind = RoomTransportEventKind::Closed;
            event.closeCode = code;
            event.reason = eastl::string(reason);
            inbox.push_back(std::move(event));
        }

        /// What the client sent as message @p index, parsed
        [[nodiscard]] std::expected<ClientMessage, ControlError> ParseSent(size_t index) const {
            return ParseClientMessage(sent.at(index));
        }

        template <typename T>
        [[nodiscard]] const T* SentAs(size_t index, std::expected<ClientMessage, ControlError>& holder) const {
            holder = ParseSent(index);
            return holder ? std::get_if<T>(&*holder) : nullptr;
        }
    };

    class FakeRoomTransport final : public IRoomTransport {
    public:
        explicit FakeRoomTransport(std::shared_ptr<FakeLink> link) : _link(std::move(link)) {}

        ~FakeRoomTransport() override {
            _link->destroyed = true;
        }

        std::expected<void, eastl::string> Open(eastl::string_view url) override {
            _link->openCalled = true;
            _link->url = eastl::string(url);
            if (_link->openError) {
                return std::unexpected(*_link->openError);
            }
            return {};
        }

        bool SendText(eastl::string_view text) override {
            if (!_link->sendOk) {
                return false;
            }
            _link->sent.push_back(eastl::string(text));
            return true;
        }

        void Close(uint16_t code, eastl::string_view reason) override {
            _link->closeCalled = true;
            _link->closeCode = code;
            _link->closeReason = eastl::string(reason);
            if (_link->closeAnswers) {
                _link->PushClosed(code, reason);
            }
        }

        void Abort() override {
            _link->aborted = true;
            if (_link->closeAnswers) {
                _link->PushClosed();
            }
        }

        bool PollEvent(RoomTransportEvent& event) override {
            if (_link->nextEvent >= _link->inbox.size()) {
                return false;
            }
            event = _link->inbox[_link->nextEvent++];
            return true;
        }

    private:
        std::shared_ptr<FakeLink> _link;
    };

    /// Hands a RoomClient one fake transport per connection and keeps their links for the test
    class FakeTransports {
    public:
        [[nodiscard]] RoomTransportFactory Factory() {
            return [this]() -> eastl::unique_ptr<IRoomTransport> {
                if (_unavailable) {
                    return nullptr;
                }
                auto link = std::make_shared<FakeLink>();
                link->openError = _nextOpenError;
                _nextOpenError.reset();
                _links.push_back(link);
                return eastl::make_unique<FakeRoomTransport>(link);
            };
        }

        /// The next transport fails to start connecting
        void FailNextOpen(eastl::string_view message) {
            _nextOpenError = eastl::string(message);
        }

        /// The factory makes no transport
        void SetUnavailable(bool unavailable) {
            _unavailable = unavailable;
        }

        [[nodiscard]] size_t Count() const {
            return _links.size();
        }

        /// The link of the latest connection
        [[nodiscard]] FakeLink& Last() {
            return *_links.back();
        }

        [[nodiscard]] FakeLink& At(size_t index) {
            return *_links.at(index);
        }

    private:
        eastl::vector<std::shared_ptr<FakeLink>> _links;
        std::optional<eastl::string> _nextOpenError;
        bool _unavailable = false;
    };

}  // namespace BECore::Tests
