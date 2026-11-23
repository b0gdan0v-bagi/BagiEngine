#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <Games/SeatedAnswers.h>
#include <cstdint>

namespace BECore::Tests {

    // Reflected answers of a made-up game, shared by the tests of the answer bases and of the view writer.
    // The .gen.hpp is generated from this header and included in SeatedAnswersTest.cpp only.
    // The names are unique among all test headers: a type is found by its simple name.

    struct AnswerPoint {
        BE_CLASS(AnswerPoint)

        BE_REFLECT_FIELD int32_t x = 0;
        BE_REFLECT_FIELD int32_t y = 0;
    };

    struct AnswerView : SeatedView {
        BE_CLASS(AnswerView)

        BE_REFLECT_FIELD String name;
        BE_REFLECT_FIELD Vector<AnswerPoint> points;
        BE_REFLECT_FIELD int32_t turn = 0;
    };

    struct AnswerAction : SeatedAction {
        BE_CLASS(AnswerAction)

        BE_REFLECT_FIELD int32_t card = 0;
        BE_REFLECT_FIELD String type;
    };

    struct AnswerOutcome : SeatedOutcome {
        BE_CLASS(AnswerOutcome)

        BE_REFLECT_FIELD int32_t seat = 0;
    };

}  // namespace BECore::Tests
