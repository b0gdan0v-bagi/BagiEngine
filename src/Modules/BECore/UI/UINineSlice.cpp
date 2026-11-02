#include "UINineSlice.h"

namespace BECore {

    namespace {

        /// The three spans of one axis: the start border, the middle, the end border (in source pixels, or in units for the destination)
        struct Spans {
            float start = 0.0f;
            float middle = 0.0f;
            float end = 0.0f;
        };

        /// The borders of an axis in the source, never larger than the source itself
        Spans SourceSpans(float extent, float startBorder, float endBorder) {
            const float start = std::clamp(startBorder, 0.0f, extent);
            const float end = std::clamp(endBorder, 0.0f, extent - start);
            return {start, extent - start - end, end};
        }

        /// The same borders on the destination: one unit per source pixel, shrunk together when the destination is smaller than the two of them
        Spans DestinationSpans(float extent, const Spans& source) {
            const float borders = source.start + source.end;
            const float factor = borders > extent && borders > 0.0f ? extent / borders : 1.0f;
            const float start = source.start * factor;
            const float end = source.end * factor;
            return {start, std::max(0.0f, extent - start - end), end};
        }

    }  // namespace

    eastl::array<UINineSlicePiece, UINineSlice::PieceCount> UINineSlice::Compute(const Rect& source, const UIEdges& border, const Rect& destination) {
        const Spans sourceColumns = SourceSpans(source.w, border.left, border.right);
        const Spans sourceRows = SourceSpans(source.h, border.top, border.bottom);
        const Spans destinationColumns = DestinationSpans(destination.w, sourceColumns);
        const Spans destinationRows = DestinationSpans(destination.h, sourceRows);

        const float sourceColumnX[3] = {source.x, source.x + sourceColumns.start, source.x + sourceColumns.start + sourceColumns.middle};
        const float sourceColumnW[3] = {sourceColumns.start, sourceColumns.middle, sourceColumns.end};
        const float sourceRowY[3] = {source.y, source.y + sourceRows.start, source.y + sourceRows.start + sourceRows.middle};
        const float sourceRowH[3] = {sourceRows.start, sourceRows.middle, sourceRows.end};
        const float destinationColumnX[3] = {destination.x, destination.x + destinationColumns.start, destination.x + destinationColumns.start + destinationColumns.middle};
        const float destinationColumnW[3] = {destinationColumns.start, destinationColumns.middle, destinationColumns.end};
        const float destinationRowY[3] = {destination.y, destination.y + destinationRows.start, destination.y + destinationRows.start + destinationRows.middle};
        const float destinationRowH[3] = {destinationRows.start, destinationRows.middle, destinationRows.end};

        eastl::array<UINineSlicePiece, PieceCount> pieces;
        for (size_t row = 0; row < 3; ++row) {
            for (size_t column = 0; column < 3; ++column) {
                UINineSlicePiece& piece = pieces[row * 3 + column];
                const bool hasArea = sourceColumnW[column] > 0.0f && sourceRowH[row] > 0.0f && destinationColumnW[column] > 0.0f && destinationRowH[row] > 0.0f;
                if (hasArea) {
                    piece.source = {sourceColumnX[column], sourceRowY[row], sourceColumnW[column], sourceRowH[row]};
                    piece.destination = {destinationColumnX[column], destinationRowY[row], destinationColumnW[column], destinationRowH[row]};
                }
            }
        }
        return pieces;
    }

}  // namespace BECore
