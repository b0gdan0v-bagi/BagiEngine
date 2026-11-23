#include "ArtGenAnswers.h"

#include <Generated/ArtGenAnswers.gen.hpp>

namespace BECore {

    ArtGenReferenceAnswer MakeArtGenReferenceAnswer(const ArtGenReference& reference) {
        ArtGenReferenceAnswer answer;
        answer.image = reference.image;
        answer.mode = reference.mode;
        answer.weight = reference.weight;
        return answer;
    }

}  // namespace BECore
