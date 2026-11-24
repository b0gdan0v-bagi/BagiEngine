#pragma once

#include <BECore/Control/ControlParams.h>
#include <BECore/Rooms/JudgedTransport.h>
#include <BECore/Types/Types.h>
#include <Common/JsonDoc.h>
#include <Common/SharedPtr.h>
#include <RoomClient/RoomClient.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace BECore::Tests {

    /**
     * @brief A human at the table of the RoomClient tests: a RoomClient judged on the wire that plays the first action it is offered,
     * once per version of the view (RoomClientHubTest over the hub in memory, RoomClientSocketTest over sockets).
     *
     * A derived player makes `client` in its constructor, with a transport factory that feeds `judge`. What else the transports of
     * the client use (a tap, the links to a hub) is a member of the derived player, so its destructor resets `client` first.
     */
    struct RoomClientPlayer {
        [[nodiscard]] size_t Count(RoomClientEventKind kind) const {
            return static_cast<size_t>(std::count_if(events.begin(), events.end(), [kind](const RoomClientEvent& event) { return event.kind == kind; }));
        }

        /// The first event of @p kind, or null
        [[nodiscard]] const RoomClientEvent* Find(RoomClientEventKind kind) const {
            const auto found = std::find_if(events.begin(), events.end(), [kind](const RoomClientEvent& event) { return event.kind == kind; });
            return found == events.end() ? nullptr : &*found;
        }

        /// The number of cards on the table of the last view (-1 when it has none)
        [[nodiscard]] int TableSize() const {
            const JsonDoc view(client->GetViewText());
            const Optional<JsonView> table = FindParam(view, "table");
            return table && table->IsArray() ? static_cast<int>(table->Size()) : -1;
        }

        /// How many plays of @p seat the last view shows
        [[nodiscard]] int PlaysOf(int seat) const {
            int plays = 0;
            const JsonDoc view(client->GetViewText());
            if (const Optional<JsonView> table = FindParam(view, "table"); table && table->IsArray()) {
                for (const JsonView play : table->Items()) {
                    plays += GetParamInt(play, "seat").value_or(-1) == seat ? 1 : 0;
                }
            }
            return plays;
        }

        void TakeEvents() {
            const size_t before = events.size();
            client->DrainEvents(events);
            for (size_t i = before; i < events.size(); ++i) {
                if (events[i].kind == RoomClientEventKind::Resumed) {
                    // What was sent on the old connection may never have arrived
                    acted = false;
                }
            }
        }

        /// Plays the first action of the last view unless it already played on this version of it. False only when the client
        /// refused to send; the next call tries again
        bool ActIfItsTurn() {
            if (!client->IsInRoom() || !client->HasView() || client->IsFinished()) {
                return true;
            }
            const JsonDoc actions(client->GetActionsText());
            if (!actions.Root().IsArray() || actions.Root().Size() == 0) {
                return true;
            }
            if (acted && actedVersion == client->GetViewVersion()) {
                return true;
            }
            if (!client->SendAction((*actions.Root().Items().begin()).Raw()).has_value()) {
                return false;
            }
            acted = true;
            actedVersion = client->GetViewVersion();
            return true;
        }

        SharedPtr<WireJudgeLog> judge = MakeShared<WireJudgeLog>();
        UniquePtr<RoomClient> client;
        Vector<RoomClientEvent> events;
        bool acted = false;
        uint64_t actedVersion = 0;
    };

}  // namespace BECore::Tests
