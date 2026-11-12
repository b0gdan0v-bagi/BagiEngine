#pragma once

#include "WireJudge.h"

#include <RoomClient/RoomTransport.h>
#include <Rooms/RoomProtocol.h>
#include <memory>

namespace BECore::Tests {

    /// What the wire judge saw on the texts one client received (over all its connections)
    struct WireJudgeLog {
        size_t texts = 0;
        /// The seat the texts so far named (`joined`, `view`); -1 before the first and after `left`
        int seat = -1;
        /// Card ids a text named that its receiver may not know, with the text
        std::vector<std::string> leaks;
    };

    /// Wraps a transport and runs the wire judge (WireJudge.h) on every text it delivers, before the client sees it
    class JudgedTransport final : public IRoomTransport {
    public:
        JudgedTransport(eastl::unique_ptr<IRoomTransport> inner, std::shared_ptr<WireJudgeLog> log) : _inner(std::move(inner)), _log(std::move(log)) {}

        std::expected<void, eastl::string> Open(eastl::string_view url) override {
            return _inner->Open(url);
        }

        bool SendText(eastl::string_view text) override {
            return _inner->SendText(text);
        }

        void Close(uint16_t code, eastl::string_view reason) override {
            _inner->Close(code, reason);
        }

        void Abort() override {
            _inner->Abort();
        }

        bool PollEvent(RoomTransportEvent& event) override {
            if (!_inner->PollEvent(event)) {
                return false;
            }
            if (event.kind == RoomTransportEventKind::Text) {
                Judge(event.text);
            }
            return true;
        }

    private:
        void Judge(const eastl::string& text) {
            ++_log->texts;
            const auto parsed = ParseJson(text);
            const eastl::string type = parsed ? GetJsonString(*parsed, "type").value_or(eastl::string()) : eastl::string();
            if (parsed && (type == RoomMessageTypes::kJoined || type == RoomMessageTypes::kView)) {
                _log->seat = static_cast<int>(GetJsonInt(*parsed, "seat").value_or(-1));
            }
            for (const std::string& id : FindLeakedCardIds(std::string_view(text.data(), text.size()), _log->seat)) {
                _log->leaks.push_back(id + " in " + std::string(text.c_str()));
            }
            if (parsed && type == RoomMessageTypes::kLeft) {
                _log->seat = -1;
            }
        }

        eastl::unique_ptr<IRoomTransport> _inner;
        std::shared_ptr<WireJudgeLog> _log;
    };

    /// A factory whose transports are judged
    [[nodiscard]] inline RoomTransportFactory MakeJudgedFactory(RoomTransportFactory inner, std::shared_ptr<WireJudgeLog> log) {
        return [inner = std::move(inner), log = std::move(log)]() -> eastl::unique_ptr<IRoomTransport> {
            auto transport = inner();
            if (!transport) {
                return nullptr;
            }
            return eastl::make_unique<JudgedTransport>(std::move(transport), log);
        };
    }

}  // namespace BECore::Tests
