#include "CascadeTrace.h"

namespace BECore::Match3 {

    Match3Board ReplayTrace(Match3Board board, const SwapTrace& trace) {
        board.Swap(trace.a, trace.b);
        for (const CascadeStep& step : trace.steps) {
            for (const Cell c : step.removed) {
                board.Set(c, GemType::None);
            }
            for (const GemFall& fall : step.falls) {
                board.Set(fall.col, fall.toRow, board.Get(fall.col, fall.fromRow));
                board.Set(fall.col, fall.fromRow, GemType::None);
            }
            for (const GemRefill& refill : step.refills) {
                board.Set(refill.col, refill.toRow, refill.gem);
            }
        }
        return board;
    }

}  // namespace BECore::Match3
