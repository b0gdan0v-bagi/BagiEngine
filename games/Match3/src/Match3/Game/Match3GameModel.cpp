#include "Match3GameModel.h"

#include <Match3/Game/Match3SaveState.h>
#include <Match3/Game/MatchFinder.h>

namespace BECore::Match3 {

    namespace {

        constexpr size_t kBoardCellCount = static_cast<size_t>(kBoardSize) * static_cast<size_t>(kBoardSize);

        /// The gem of a board letter, or None for '.' (an empty cell, which a save never has) and for any other character
        GemType GemOfLetter(char letter) noexcept {
            for (size_t index = 1; index < sizeof(kGemLetters) - 1; ++index) {
                if (kGemLetters[index] == letter) {
                    return static_cast<GemType>(index);
                }
            }
            return GemType::None;
        }

        /// True for the shape that operator<< gives a std::mt19937: decimal numbers that fit 32 bits with one space between, the 624 of the state as the standard has them (MSVC, libc++) or one more,
        /// the position, as libstdc++ writes. Checked before operator>> reads, which does not defend against other text or against a number out of range
        bool LooksLikeGeneratorText(StringView text) noexcept {
            size_t numbers = 0;
            uint64_t value = 0;
            bool inNumber = false;
            for (const char c : text) {
                if (c >= '0' && c <= '9') {
                    numbers += inNumber ? 0 : 1;
                    value = (inNumber ? value * 10 : 0) + static_cast<uint64_t>(c - '0');
                    if (value > std::numeric_limits<uint32_t>::max()) {
                        return false;
                    }
                    inNumber = true;
                } else if (c == ' ' && inNumber) {
                    inNumber = false;
                } else {
                    return false;
                }
            }
            return inNumber && (numbers == std::mt19937::state_size || numbers == std::mt19937::state_size + 1);
        }

        /// True for a state that gives only zeros for ever (all 19937 bits zero; the low 31 bits of the oldest word do not count). Every refill would then be the same gem and a cascade would never
        /// end. Exact and independent of the library: tempering is a bijection, so state_size zeros in a row mean a zero state, which stays zero
        bool IsDegenerateGenerator(std::mt19937 rng) noexcept {
            for (size_t draw = 0; draw < std::mt19937::state_size; ++draw) {
                if (rng() != 0) {
                    return false;
                }
            }
            return true;
        }

    }  // namespace

    Match3GameModel::Match3GameModel() {
        Reset(0);
    }

    void Match3GameModel::Reset(uint32_t seed) {
        _seed = seed;
        _rng.seed(seed);
        _score = 0;
        _selected.reset();
        _lastSwap.reset();
        GenerateInitialBoard();
        ++_version;
    }

    void Match3GameModel::OnCellClicked(Cell c) {
        if (!c.IsInside() || _board.Get(c) == GemType::None) {
            return;
        }

        if (!_selected.has_value()) {
            _selected = c;
            return;
        }

        const Cell prev = *_selected;
        if (prev == c) {
            _selected.reset();
            return;
        }

        if (IsAdjacent(prev, c)) {
            (void)TrySwap(prev, c);
            _selected.reset();
            return;
        }

        _selected = c;
    }

    bool Match3GameModel::TrySwap(Cell a, Cell b) {
        _lastSwap.reset();
        if (!a.IsInside() || !b.IsInside() || !IsAdjacent(a, b)) {
            return false;
        }

        _board.Swap(a, b);
        if (!MatchFinder::HasAnyMatch(_board)) {
            _board.Swap(a, b);
            return false;
        }

        SwapTrace trace;
        trace.a = a;
        trace.b = b;
        ResolveCascade(&trace.steps);
        ++_version;
        trace.version = _version;
        _lastSwap = eastl::move(trace);
        return true;
    }

    Match3SaveState Match3GameModel::CaptureState() const {
        Match3SaveState state;
        state.seed = _seed;
        state.score = _score;
        state.board.reserve(kBoardCellCount);
        for (int8_t row = 0; row < kBoardSize; ++row) {
            for (int8_t col = 0; col < kBoardSize; ++col) {
                state.board.push_back(kGemLetters[static_cast<size_t>(_board.Get(col, row))]);
            }
        }

        std::ostringstream rng;
        rng << _rng;
        // std: the text of an std::ostringstream
        const std::string text = rng.str();
        state.rng.assign(text.data(), text.size());
        return state;
    }

    std::expected<void, String> Match3GameModel::RestoreState(const Match3SaveState& state) {
        // Everything is built on the side first, so a refused state changes nothing
        if (state.board.size() != kBoardCellCount) {
            return std::unexpected(Format("the board has {} characters, {} are needed", state.board.size(), kBoardCellCount));
        }
        Match3Board board;
        for (size_t index = 0; index < kBoardCellCount; ++index) {
            const GemType gem = GemOfLetter(state.board[index]);
            if (gem == GemType::None) {
                return std::unexpected(Format("the board has '{}' at position {}, which is not a gem letter (R, O, Y, G, B, P)", state.board[index], index));
            }
            board.Set(Cell::FromIndex(static_cast<int>(index)), gem);
        }
        if (MatchFinder::HasAnyMatch(board)) {
            return std::unexpected(String("the board has a match on it, a saved board is always settled"));
        }
        if (state.score < 0) {
            return std::unexpected(Format("the score {} is negative", state.score));
        }

        // The text has to be what operator<< writes for the generator it reads as: the round trip also refuses a leading zero or a position out of range
        if (!LooksLikeGeneratorText(StringView(state.rng))) {
            return std::unexpected(String("the random generator text is not the text of a std::mt19937"));
        }
        // std: an std::istringstream reads it
        const std::string rngText(state.rng.data(), state.rng.size());
        std::istringstream in(rngText);
        std::mt19937 rng;
        in >> rng;
        if (!in) {
            return std::unexpected(String("the random generator text cannot be read"));
        }
        std::ostringstream written;
        written << rng;
        if (written.str() != rngText) {
            return std::unexpected(String("the random generator text is not the state of a generator (not the one that operator<< writes)"));
        }
        if (IsDegenerateGenerator(rng)) {
            return std::unexpected(String("the random generator is degenerate: it gives only zeros"));
        }

        _board = board;
        _score = state.score;
        _seed = state.seed;
        _rng = rng;
        _selected.reset();
        _lastSwap.reset();
        ++_version;
        return {};
    }

    bool Match3GameModel::WouldSwapMatch(Cell a, Cell b) const {
        if (!a.IsInside() || !b.IsInside() || !IsAdjacent(a, b)) {
            return false;
        }

        Match3Board trial = _board;
        trial.Swap(a, b);
        return MatchFinder::HasAnyMatch(trial);
    }

    eastl::vector<eastl::pair<Cell, Cell>> Match3GameModel::GetLegalSwaps() const {
        eastl::vector<eastl::pair<Cell, Cell>> swaps;
        for (int8_t row = 0; row < kBoardSize; ++row) {
            for (int8_t col = 0; col < kBoardSize; ++col) {
                const Cell from{col, row};
                const Cell right{static_cast<int8_t>(col + 1), row};
                const Cell below{col, static_cast<int8_t>(row + 1)};
                if (WouldSwapMatch(from, right)) {
                    swaps.emplace_back(from, right);
                }
                if (WouldSwapMatch(from, below)) {
                    swaps.emplace_back(from, below);
                }
            }
        }
        return swaps;
    }

    int Match3GameModel::ResolveCascade() {
        return ResolveCascade(nullptr);
    }

    int Match3GameModel::ResolveCascade(eastl::vector<CascadeStep>* steps) {
        int totalRemoved = 0;
        while (true) {
            const auto matched = MatchFinder::FindMatches(_board);
            if (matched.empty()) {
                break;
            }
            for (const Cell c : matched) {
                _board.Set(c, GemType::None);
            }
            totalRemoved += static_cast<int>(matched.size());
            _score += static_cast<int>(matched.size());

            CascadeStep* step = nullptr;
            if (steps != nullptr) {
                step = &steps->emplace_back();
                step->removed = matched;
            }
            ApplyGravity(step);
            RefillTop(step);
        }
        return totalRemoved;
    }

    bool Match3GameModel::IsAdjacent(Cell a, Cell b) noexcept {
        const int dc = static_cast<int>(a.col) - static_cast<int>(b.col);
        const int dr = static_cast<int>(a.row) - static_cast<int>(b.row);
        const int adc = dc < 0 ? -dc : dc;
        const int adr = dr < 0 ? -dr : dr;
        return (adc + adr) == 1;
    }

    GemType Match3GameModel::RandomGem() {
        // Not std::uniform_int_distribution: its output differs between standard libraries, mt19937's does not.
        // The modulo bias (6 does not divide 2^32) is below 1.5e-9 per gem, irrelevant for a game.
        return static_cast<GemType>(1 + _rng() % static_cast<uint32_t>(kGemTypeCount));
    }

    void Match3GameModel::GenerateInitialBoard() {
        _board.Clear();
        for (int8_t row = 0; row < kBoardSize; ++row) {
            for (int8_t col = 0; col < kBoardSize; ++col) {
                while (true) {
                    const GemType candidate = RandomGem();
                    const bool horizontalRun = (col >= 2) && _board.Get(col - 1, row) == candidate && _board.Get(col - 2, row) == candidate;
                    const bool verticalRun = (row >= 2) && _board.Get(col, row - 1) == candidate && _board.Get(col, row - 2) == candidate;
                    if (!horizontalRun && !verticalRun) {
                        _board.Set(col, row, candidate);
                        break;
                    }
                }
            }
        }
    }

    void Match3GameModel::ApplyGravity(CascadeStep* step) {
        // Convention: row 0 is the top of the board, gems fall toward higher row indices.
        for (int8_t col = 0; col < kBoardSize; ++col) {
            int8_t writeRow = kBoardSize - 1;
            for (int8_t row = kBoardSize - 1; row >= 0; --row) {
                const GemType g = _board.Get(col, row);
                if (g != GemType::None) {
                    if (writeRow != row) {
                        _board.Set(col, writeRow, g);
                        _board.Set(col, row, GemType::None);
                        if (step != nullptr) {
                            step->falls.push_back(GemFall{col, row, writeRow});
                        }
                    }
                    --writeRow;
                }
            }
        }
    }

    void Match3GameModel::RefillTop(CascadeStep* step) {
        for (int8_t col = 0; col < kBoardSize; ++col) {
            // After gravity the holes of a column are its top rows 0..holes-1
            int8_t holes = 0;
            while (holes < kBoardSize && _board.Get(col, holes) == GemType::None) {
                ++holes;
            }
            for (int8_t row = 0; row < kBoardSize; ++row) {
                if (_board.Get(col, row) == GemType::None) {
                    const GemType gem = RandomGem();
                    _board.Set(col, row, gem);
                    if (step != nullptr) {
                        step->refills.push_back(GemRefill{col, row, static_cast<int8_t>(row - holes), gem});
                    }
                }
            }
        }
    }

}  // namespace BECore::Match3
