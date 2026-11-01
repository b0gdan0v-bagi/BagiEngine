# Chess: bitboard position, full legal rules, check / checkmate

## Context

Checkmate cannot be delivered in BagiChess. `MoveGenerator` returns pseudo-legal targets only (`games/Chess/CLAUDE.md`, "Rules scope (MVP)"): a king can stay in check or be captured, and nothing ever ends the game. The board is a plain `ChessBoard` (64 `Piece` cells) with `MoveGenerator::Generate(board, from)`; there is no notion of a move, castling rights, en passant square or history.

Decisions from the user:
- **Rules:** full chess rules: legal moves, check, checkmate, stalemate, castling, en passant, promotion (plus the automatic draws below).
- **Board:** implemented the way mature engines (Stockfish and the like) do it: bitboards, precomputed attack tables with magic bitboards, packed 16-bit moves, make / unmake with a state stack, Zobrist keys, FEN, perft-verified. (The user's "венгерская нотация" meant this, not identifier naming. New code keeps the surrounding Chess style: `kName` constants in anonymous namespaces.)
- **Showing the result in Game mode:** no text in the renderer, so: red king square on check, dark red on mate, dimmed board with a result bar (white / black / gray), one `LOG_INFO` line, and a click on the board after the game ends starts a new game.
- **Promotion in the window:** always a queen. Other pieces only through the model API and UCI (`e7e8n`). A picker UI is a separate later task.

Out of scope: `IControllableGame` for Chess, WebSocket / MCP / Stockfish bridges, Chess960, SAN, undo UI, clocks (all in the "No external control" entry of `TODO.md`; this work only delivers the pieces that entry lists: FEN, UCI, full legal moves checked by perft, a model-level move).

## Design (all new code in `games/Chess/src/Chess/Game/`, plain C++, no scene or renderer)

| File | Content |
|------|---------|
| `ChessTypes.h` | keep `Square` (file, rank; `Index()` = rank*8+file already equals the bitboard bit order, a1 = bit 0), `Piece`, `PieceType`, `PieceColor`; add `Opposite(PieceColor)` |
| `Bitboard.h` | `using Bitboard = uint64_t`, `SquareBB`, file / rank masks, shifts with wrap masks, `PopCount` / `Lsb` / `PopLsb` over `<bit>` |
| `Move.h` | `Move`: `uint16_t` = from(6) \| to(6) \| promotion piece(2: N B R Q) \| kind(2); `CORE_ENUM(MoveKind, uint8_t, Normal, Promotion, EnPassant, Castling)`. Castling is encoded as king from -> king destination (`e1g1`), the UCI form. `ToUci()` |
| `Attacks.{h,cpp}` | `AttackTables : Singleton<AttackTables>`: knight / king / pawn tables, `Between` / `Line`, rook and bishop magic bitboards (`RookAttacks(sq, occ)`, `BishopAttacks`, `QueenAttacks`), Zobrist keys. A slow ray-walking `SlidingAttacks(sq, occ)` is the reference used to build the tables and in tests |
| `Position.{h,cpp}` | replaces `ChessBoard`: `_byType[7]`, `_byColor[2]` bitboards + a 64-cell mailbox for `GetPieceAt`; side to move, castling rights (4-bit mask), en passant square, halfmove clock, fullmove number, Zobrist key; `StateInfo` stack (captured piece, previous castling / ep / clock / key / checkers) for `DoMove` / `UndoMove`. `FromFen` -> `std::expected<Position, FenError>`, `ToFen`, `Start()`, `AttackersTo`, `Checkers`, `IsInCheck`, pinned / blocker bitboards for each king, `IsLegal(Move)` (Stockfish `legal()` logic: pins, king destination attacked with the king removed from the occupancy, en passant simulated on the occupancy, castling path not attacked), `IsInsufficientMaterial`, `RepetitionCount` (keys in the stack) |
| `MoveGenerator.{h,cpp}` | `GenerateLegal(const Position&) -> MoveList` (`eastl::fixed_vector<Move, 256, false>`): pseudo-legal per piece type from the attack tables (pawn pushes / captures by shifts, promotions, en passant, castling), evasions when in check (king moves, or capture / block of a single checker), then `Position::IsLegal` filter. Also `ParseUci(position, text) -> std::optional<Move>` (matches against the legal list, so a bad string can never produce an illegal move) |
| `ChessGameModel.{h,cpp}` | owns a `Position`; `CORE_ENUM(GameStatus, uint8_t, Ongoing, Checkmate, Stalemate, DrawInsufficientMaterial, DrawThreefold, DrawFiftyMoves)`, `GetStatus`, `GetWinner`, `IsInCheck`, `GetKingSquare`; public `TryMove(Move)` (the one place a move is applied, also used by `OnSquareClicked`); click flow unchanged for the player; selected piece keeps the legal `Move`s from its square and the distinct target squares for highlighting; clicks are ignored when the status is not `Ongoing` |

Rules details to get right:
- Status after each move: no legal moves -> checkmate if in check (winner = side that moved), else stalemate; then insufficient material (K vs K, K+minor vs K, same-color bishops only), threefold repetition and the 50-move rule, applied automatically because the UI has no way to claim them (FIDE ends a game by itself only at fivefold and 75 moves; the owner chose the shorter ones; thresholds are named constants).
- Castling rights are lost when the king or a rook moves and when a rook is captured on its home square; a pawn double push sets the en passant square only as FEN needs it (always, as in UCI engines) and the capture is legal only if it does not expose the own king (rank pin case).
- Promotion generates four moves per target; a click resolves to the queen move.

Piece handles (`PieceTrack`, scene glue): the track gains `type` (+ color) so the sprite follows promotion. `RelocatePieces` becomes move-kind aware: a normal capture deactivates the handle on `to`, en passant deactivates the handle on (`to.file`, `from.rank`), castling also moves the rook handle, promotion changes the handle's type. `Reset()` restores start squares and start types.

Scene glue:
- `ChessBoardComponent`: red check / mate king square, game-over dim + result bar, click after game over calls `model->Reset()`, `LOG_INFO` once when the status changes ("Checkmate, White wins").
- `ChessPieceComponent`: when the model reports a different type for this handle, set the sibling `SpriteRendererComponent::_spriteId` to `Format("{}{}", color, type)` (sprite names `WhiteQueen` ... already exist in `TextureLibrary.xml`) and call `ReloadTexture()`; reset puts the pawn sprite back.

## Steps

Each step builds and has green tests before the next one. New files only need `cmake --preset macos-debug` re-run (the module globs sources).

1. **Baseline:** `cmake --preset macos-debug`, build `ChessTests`, run `ctest` to confirm the starting state.
2. **Foundations:** `Bitboard.h`, `Move.h`, `Opposite`. Tests: shifts / popcount / lsb, `Move` pack and unpack, `ToUci`.
3. **Attack tables:** leapers, `Between` / `Line`, magic bitboards, Zobrist. Spike first: build the magics with a seeded PRNG at first use and measure Debug time. `ctest` runs every test in its own process, so the table build is paid per test; if it takes more than ~100 ms in Debug, print the found magics once and embed them as constants (the build then only fills the tables and asserts there is no collision). Tests: every square, many random occupancies, `RookAttacks` / `BishopAttacks` equal the ray-walking reference; knight / king / pawn corner cases.
4. **Position:** storage, FEN in / out, attackers, checkers, pins, `DoMove` / `UndoMove`, `IsLegal`. Tests: FEN round trip for the standard test positions, `DoMove` then `UndoMove` restores FEN and key, Zobrist key after a move equals the key of the same position loaded from FEN, castling-rights and en passant bookkeeping.
5. **Move generator + perft:** replace the old `Generate`. Perft tests with the well-known counts (re-check each number against the chessprogramming wiki "Perft Results" while writing): start position 20 / 400 / 8902 / 197281, Kiwipete 48 / 2039 / 97862, position 3 14 / 191 / 2812 / 43238 / 674624, position 4 6 / 264 / 9467, position 5 44 / 1486 / 62379, position 6 46 / 2079 / 89890. Depths chosen so the whole suite stays a few seconds in Debug (deeper ones can be `DISABLED_` for Release runs). Perft is the real proof that castling, en passant, promotion and pins are right.
6. **Game status:** checkmate / stalemate / draw detection. Tests: fool's mate, scholar's mate, back-rank mate, smothered mate, a check that only en passant can answer (not mate), a pinned piece that cannot block, double check, classic stalemate, K vs K and K+B vs K, threefold by a knight shuffle, 50-move clock from FEN.
7. **Model:** rewrite on `Position`, `PieceTrack` types, `TryMove`, status, delete `ChessBoard.{h,cpp}`. Rewrite `games/Chess/tests/MoveGeneratorTest.cpp` for the new API (keep its intent: piece reach and blocking, starting position = 20 moves) and add `ChessGameModelTest.cpp`: play fool's mate through `OnSquareClicked` (f2 f3, e7 e5, g2 g4, d8 h4) and assert `Checkmate`, winner Black, clicks ignored afterwards, `Reset()` returns to `Ongoing` with handles on their start squares; castling / en passant / promotion move the right handles and the promoted handle reports a queen.
8. **Scene glue:** the two components above. No `SceneConfig.xml` change is needed (the board node already exists; no new nodes).
9. **Docs and bookkeeping:** `games/Chess/CLAUDE.md` (layout table, replace "Rules scope (MVP)" with the real scope: standard chess, no Chess960, promotion queen-only in the UI, automatic draws, no text in the result display), header comment of the test file. `TODO.md`: in the "No external control" entry, edit only the Chess lines (item 3 of "What is missing" and the "Chess rules and notation" bullet: FEN, UCI, full legal moves and the model-level move are done, what remains is the control adapter and bridges); `TODO.md` already has uncommitted edits in the working tree, so touch nothing else; run `todo-sync check`.
10. **Finish:** `clang-format` skill on the changed files, `strip-pch-includes` (PCH has the EASTL containers; `<bit>` is not in it), build all game targets and tests.

## Critical files

- Modify: `games/Chess/src/Chess/Game/ChessGameModel.{h,cpp}`, `ChessTypes.h`, `MoveGenerator.{h,cpp}`, `games/Chess/src/Chess/Components/ChessBoardComponent.{h,cpp}`, `ChessPieceComponent.{h,cpp}`, `games/Chess/tests/MoveGeneratorTest.cpp`, `games/Chess/CLAUDE.md`, `TODO.md`
- Add: `Bitboard.h`, `Move.h`, `Attacks.{h,cpp}`, `Position.{h,cpp}` (+ tests `BitboardTest`, `AttacksTest`, `PositionTest`, `PerftTest`, `RulesTest`, `ChessGameModelTest`)
- Remove: `ChessBoard.{h,cpp}` (only the Chess model used it; checked by grep)
- Reuse: `Singleton<T>` (`BECore/Utils/Singleton.h`), `CORE_ENUM` / `EnumUtils`, `eastl::fixed_vector` (in the PCH), `Format()` / `LOG_INFO`, `SpriteRendererComponent::ReloadTexture`, test helpers in `tests/Common/Printers.h`

## Verification

1. `cmake --preset macos-debug && cmake --build --preset macos-debug --target ChessTests BagiEngineTests Match3Tests`, then `ctest --preset macos-debug`: all green, the Chess suite includes the perft and mate cases above. `BagiEngineTests` and `Match3Tests` must be unchanged (the engine and Match3 are not touched).
2. `cmake --build --preset macos-debug` builds `BagiChess` and `BagiMatch3`.
3. By hand in `BagiChess` (needs a person at the window; the result display is not covered by tests): fool's mate by clicks (f2-f3, e7-e5, g2-g4, d8-h4) shows the red king, the dim and the black bar, logs "Checkmate", ignores board clicks until one click restarts the game; pawn to the last rank turns into a queen sprite; castling moves the rook; a king cannot step onto an attacked square; a pinned piece cannot leave the pin line.
4. If the table build is slow in Debug (step 3), record the measured time in `games/Chess/CLAUDE.md` next to the decision to embed the magics.
