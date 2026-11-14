#include "DemoCardGame.h"

namespace BECore {

    namespace {

        /// splitmix64: a few lines, the same numbers on every platform (std::shuffle is not specified)
        uint64_t NextRandom(uint64_t& state) {
            state += 0x9E3779B97F4A7C15ULL;
            uint64_t z = state;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            return z ^ (z >> 31);
        }

        std::unexpected<ControlError> Fail(eastl::string_view code, eastl::string message) {
            return std::unexpected(MakeControlError(code, std::move(message)));
        }

        bool IsSeatInRange(int seat) {
            return seat >= 0 && seat < DemoCardGame::kSeatCount;
        }

        const eastl::vector<eastl::string> kNoCards;

    }  // namespace

    int DemoCardGame::GetSeatCount() const {
        return kSeatCount;
    }

    std::expected<void, ControlError> DemoCardGame::Start(uint64_t seed, const Json& options) {
        uint64_t state = seed;
        int firstSeat = static_cast<int>(NextRandom(state) % kSeatCount);
        if (FindJsonMember(options, "firstSeat") != nullptr) {
            const auto requested = GetJsonInt(options, "firstSeat");
            if (!requested || *requested < 0 || *requested >= kSeatCount) {
                return Fail(ControlErrors::kInvalidParams, "'firstSeat' has to be an integer from 0 to 3");
            }
            firstSeat = static_cast<int>(*requested);
        }

        for (int seat = 0; seat < kSeatCount; ++seat) {
            _hands[seat].clear();
            for (int n = 0; n < kCardsPerSeat; ++n) {
                _hands[seat].push_back(Format("c{}_{}", seat, n));
            }
            for (int i = kCardsPerSeat - 1; i > 0; --i) {
                const auto j = static_cast<int>(NextRandom(state) % static_cast<uint64_t>(i + 1));
                eastl::swap(_hands[seat][i], _hands[seat][j]);
            }
        }
        _table.clear();
        _turn = firstSeat;
        _started = true;
        ++_version;
        return {};
    }

    bool DemoCardGame::CanAct(int seat) const {
        return _started && !IsFinished() && IsSeatInRange(seat) && seat == _turn;
    }

    std::expected<Json, ControlError> DemoCardGame::GetSeatView(int seat) const {
        if (!IsSeatInRange(seat)) {
            return Fail(SeatedGameErrors::kInvalidSeat, Format("seat {} is out of range", seat));
        }
        Json view = Json::object();
        view["seat"] = seat;
        Json hand = Json::array();
        for (const eastl::string& card : _hands[seat]) {
            hand.push_back(card.c_str());
        }
        view["hand"] = hand;
        Json counts = Json::array();
        for (const auto& other : _hands) {
            counts.push_back(static_cast<int>(other.size()));
        }
        view["cardCounts"] = counts;
        Json table = Json::array();
        for (const Play& play : _table) {
            Json entry = Json::object();
            entry["seat"] = play.seat;
            entry["card"] = play.card.c_str();
            table.push_back(entry);
        }
        view["table"] = table;
        view["turn"] = IsFinished() ? -1 : _turn;
        view["finished"] = IsFinished();
        return view;
    }

    std::expected<Json, ControlError> DemoCardGame::GetSeatActions(int seat) const {
        if (!IsSeatInRange(seat)) {
            return Fail(SeatedGameErrors::kInvalidSeat, Format("seat {} is out of range", seat));
        }
        Json actions = Json::array();
        if (CanAct(seat)) {
            for (const eastl::string& card : _hands[seat]) {
                Json action = Json::object();
                action["type"] = "play";
                action["card"] = card.c_str();
                actions.push_back(action);
            }
        }
        return actions;
    }

    std::expected<Json, ControlError> DemoCardGame::PerformSeatAction(int seat, const Json& action) {
        if (!IsSeatInRange(seat)) {
            return Fail(SeatedGameErrors::kInvalidSeat, Format("seat {} is out of range", seat));
        }
        const auto type = GetJsonString(action, "type");
        if (!type) {
            return Fail(ControlErrors::kInvalidParams, "'type' has to be a string");
        }
        const auto card = GetJsonString(action, "card");
        if (!card) {
            return Fail(ControlErrors::kInvalidParams, "'card' has to be a string");
        }
        if (!CanAct(seat)) {
            return Fail(SeatedGameErrors::kNotYourTurn, Format("it is not the turn of seat {}", seat));
        }
        if (*type != "play") {
            return Fail(GameControlErrors::kIllegalAction, "only \"play\" is legal");
        }
        auto& hand = _hands[seat];
        const auto it = eastl::find(hand.begin(), hand.end(), *card);
        if (it == hand.end()) {
            return Fail(GameControlErrors::kIllegalAction, Format("seat {} does not hold '{}'", seat, card->c_str()));
        }
        hand.erase(it);
        _table.push_back(Play{seat, *card});
        _turn = (_turn + 1) % kSeatCount;
        ++_version;

        Json outcome = Json::object();
        outcome["seat"] = seat;
        outcome["card"] = card->c_str();
        return outcome;
    }

    Optional<Json> DemoCardGame::ChooseComputerAction(int seat) const {
        if (!CanAct(seat) || _hands[seat].empty()) {
            return std::nullopt;
        }
        const eastl::string* lowest = &_hands[seat].front();
        for (const eastl::string& card : _hands[seat]) {
            if (card < *lowest) {
                lowest = &card;
            }
        }
        Json action = Json::object();
        action["type"] = "play";
        action["card"] = lowest->c_str();
        return action;
    }

    bool DemoCardGame::IsFinished() const {
        return _started && static_cast<int>(_table.size()) >= kTotalPlays;
    }

    uint64_t DemoCardGame::GetStateVersion() const {
        return _version;
    }

    const eastl::vector<eastl::string>& DemoCardGame::GetHand(int seat) const {
        return IsSeatInRange(seat) ? _hands[seat] : kNoCards;
    }

    int DemoCardGame::GetTurn() const {
        return (_started && !IsFinished()) ? _turn : -1;
    }

    int DemoCardGame::GetPlayCount() const {
        return static_cast<int>(_table.size());
    }

    std::expected<Json, ControlError> LeakyDemoCardGame::GetSeatView(int seat) const {
        auto view = DemoCardGame::GetSeatView(seat);
        if (!view) {
            return view;
        }
        Json allHands = Json::array();
        for (int other = 0; other < kSeatCount; ++other) {
            Json hand = Json::array();
            for (const eastl::string& card : GetHand(other)) {
                hand.push_back(card.c_str());
            }
            allHands.push_back(hand);
        }
        (*view)["allHands"] = allHands;
        return view;
    }

    eastl::vector<eastl::string> FindHiddenCards(const eastl::string& text, int seat, const DemoCardGame& game) {
        eastl::vector<eastl::string> leaked;
        for (int other = 0; other < DemoCardGame::kSeatCount; ++other) {
            if (other == seat) {
                continue;
            }
            for (const eastl::string& card : game.GetHand(other)) {
                // "c1_2" must not match inside a longer id such as "c1_20"
                for (auto at = text.find(card); at != eastl::string::npos; at = text.find(card, at + 1)) {
                    const size_t end = at + card.size();
                    if (end >= text.size() || text[end] < '0' || text[end] > '9') {
                        leaked.push_back(card);
                        break;
                    }
                }
            }
        }
        return leaked;
    }

}  // namespace BECore
