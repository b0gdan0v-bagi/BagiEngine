#pragma once

#include <BECore/Control/Json.h>
#include <vector>

namespace BECore::Tests {

    /**
     * @brief The judge of the socket scenarios: what a client may know of the demo card game, decided from the text it received.
     *
     * The test cannot look at the game inside the hub (it runs in the server's thread, behind the sockets), so the judge works from the
     * wire alone. A card id is `c<digit>_<digit>`, matched as a whole token. A message to the client of seat S may name the ids
     * `c<S>_*` (its own hand) and the ids in its own `view.table` (played, so public); any other id is a leak. It runs on every text
     * frame a client received, whatever its type.
     *
     * `tools/rooms/smoke.py` holds the same rule in Python: change both together.
     */

    namespace WireJudgeDetail {

        inline bool IsWordCharacter(char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        }

        inline bool IsDigit(char c) {
            return c >= '0' && c <= '9';
        }

    }  // namespace WireJudgeDetail

    /// Every card id (`c<digit>_<digit>`, a whole token: "c1_20" and "xc1_2" are not ids) that @p text mentions, in order
    inline Vector<String> FindCardIds(StringView text) {
        using namespace WireJudgeDetail;
        constexpr size_t kIdLength = 4;
        Vector<String> ids;
        for (size_t at = 0; at + kIdLength <= text.size(); ++at) {
            if (text[at] != 'c' || !IsDigit(text[at + 1]) || text[at + 2] != '_' || !IsDigit(text[at + 3])) {
                continue;
            }
            const bool startsToken = at == 0 || !IsWordCharacter(text[at - 1]);
            const bool endsToken = at + kIdLength == text.size() || !IsWordCharacter(text[at + kIdLength]);
            if (startsToken && endsToken) {
                ids.emplace_back(text.substr(at, kIdLength));
            }
        }
        return ids;
    }

    /// The card ids @p text names that the client of @p seat (-1: a client that has no seat yet) may not know, in order
    inline Vector<String> FindLeakedCardIds(StringView text, int seat) {
        Vector<String> allowedTable;
        if (const auto parsed = ParseJson(text); parsed && parsed->is_object()) {
            const Json* view = FindJsonMember(*parsed, "view");
            const Json* table = view && view->is_object() ? FindJsonMember(*view, "table") : nullptr;
            if (table) {
                allowedTable = FindCardIds(FromStd(table->dump()));
            }
        }
        const String ownPrefix = seat >= 0 ? "c" + ToString(seat) + "_" : String();
        Vector<String> leaked;
        for (const String& id : FindCardIds(text)) {
            const bool own = !ownPrefix.empty() && StringView(id).starts_with(ownPrefix);
            const bool played = std::find(allowedTable.begin(), allowedTable.end(), id) != allowedTable.end();
            if (!own && !played) {
                leaked.push_back(id);
            }
        }
        return leaked;
    }

}  // namespace BECore::Tests
