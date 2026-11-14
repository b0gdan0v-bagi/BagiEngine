#include "Match3GameControl.h"

#include <BECore/Control/Json.h>

namespace BECore::Match3 {

    namespace {

        constexpr StringView kStatusPlaying = "playing";
        constexpr StringView kStatusNoMoves = "no_moves";

        std::unexpected<ControlError> NoActiveModel() {
            return std::unexpected(MakeControlError(GameControlErrors::kNoActiveModel, "the Match3 board is not active"));
        }

        Json CellToJson(Cell cell) {
            Json json = Json::object();
            json["col"] = cell.col;
            json["row"] = cell.row;
            return json;
        }

        /// The cell in member @p name of @p action, or the invalid_params error that says what is wrong with it
        std::expected<Cell, ControlError> ParseCell(const Json& action, StringView name) {
            const Json* member = FindJsonMember(action, name);
            if (member == nullptr || !member->is_object()) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'{}' has to be an object {{\"col\", \"row\"}}", name)));
            }

            const auto col = GetJsonInt(*member, "col");
            const auto row = GetJsonInt(*member, "row");
            if (!col || !row) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'{}' needs integer 'col' and 'row'", name)));
            }
            // Checked as int64_t before the narrowing, so that a huge number cannot wrap into the board
            if (*col < 0 || *col >= kBoardSize || *row < 0 || *row >= kBoardSize) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'{}' is outside the {}x{} board", name, kBoardSize, kBoardSize)));
            }
            return Cell{static_cast<int8_t>(*col), static_cast<int8_t>(*row)};
        }

    }  // namespace

    Match3GameControl::Match3GameControl(TargetProvider provider) : _provider(std::move(provider)) {}

    std::expected<Json, ControlError> Match3GameControl::GetState() const {
        const Match3ControlTarget target = _provider();
        if (target.model == nullptr) {
            return NoActiveModel();
        }
        const Match3GameModel& model = *target.model;

        Json board = Json::array();
        for (int8_t row = 0; row < kBoardSize; ++row) {
            std::string line;
            for (int8_t col = 0; col < kBoardSize; ++col) {
                line.push_back(kGemLetters[static_cast<size_t>(model.GetCellAt(Cell{col, row}))]);
            }
            board.push_back(std::move(line));
        }

        const size_t legalSwapCount = model.GetLegalSwaps().size();

        Json state = Json::object();
        state["size"] = kBoardSize;
        state["board"] = std::move(board);
        state["score"] = model.GetScore();
        state["seed"] = model.GetSeed();
        state["legalSwapCount"] = legalSwapCount;
        state["status"] = MakeJsonString(legalSwapCount > 0 ? kStatusPlaying : kStatusNoMoves);
        return state;
    }

    std::expected<Json, ControlError> Match3GameControl::GetActions() const {
        const Match3ControlTarget target = _provider();
        if (target.model == nullptr) {
            return NoActiveModel();
        }

        Json actions = Json::array();
        for (const auto& [from, to] : target.model->GetLegalSwaps()) {
            Json action = Json::object();
            action["type"] = "swap";
            action["from"] = CellToJson(from);
            action["to"] = CellToJson(to);
            actions.push_back(std::move(action));
        }
        return actions;
    }

    std::expected<Json, ControlError> Match3GameControl::PerformAction(const Json& action) {
        const Match3ControlTarget target = _provider();
        if (target.model == nullptr) {
            return NoActiveModel();
        }
        Match3GameModel& model = *target.model;

        const auto type = GetJsonString(action, "type");
        if (!type || *type != "swap") {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'type' has to be \"swap\""));
        }

        const auto from = ParseCell(action, "from");
        if (!from) {
            return std::unexpected(from.error());
        }
        const auto to = ParseCell(action, "to");
        if (!to) {
            return std::unexpected(to.error());
        }

        if (!Match3GameModel::IsAdjacent(*from, *to)) {
            return std::unexpected(MakeControlError(GameControlErrors::kIllegalAction, "the cells have to be neighbours: one step along a row or a column"));
        }
        if (!model.WouldSwapMatch(*from, *to)) {
            return std::unexpected(MakeControlError(GameControlErrors::kIllegalAction, "the swap makes no match"));
        }

        const int scoreBefore = model.GetScore();
        const bool committed = model.TrySwap(*from, *to);
        EXPECT(committed, "Match3GameControl: WouldSwapMatch and TrySwap disagree");

        Json outcome = Json::object();
        outcome["scoreGained"] = model.GetScore() - scoreBefore;
        outcome["score"] = model.GetScore();
        if (target.enterGame) {
            target.enterGame();
        }
        return outcome;
    }

    std::expected<void, ControlError> Match3GameControl::Reset(const Json& options) {
        const Match3ControlTarget target = _provider();
        if (target.model == nullptr) {
            return NoActiveModel();
        }

        uint32_t seed = target.defaultSeed;
        if (FindJsonMember(options, "seed") != nullptr) {
            const auto requested = GetJsonInt(options, "seed");
            if (!requested || *requested < 0 || *requested > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'seed' has to be an integer from 0 to 4294967295"));
            }
            seed = static_cast<uint32_t>(*requested);
        }

        target.model->Reset(seed);
        if (target.enterGame) {
            target.enterGame();
        }
        return {};
    }

    uint64_t Match3GameControl::GetStateVersion() const {
        const Match3ControlTarget target = _provider();
        return target.model != nullptr ? target.model->GetVersion() : 0;
    }

}  // namespace BECore::Match3
