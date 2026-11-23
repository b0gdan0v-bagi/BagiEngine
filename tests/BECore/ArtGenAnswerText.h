#pragma once

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Types/Types.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// What a reflected answer writes through `JsonWriter`, parsed back: a test that calls a session method directly reads the same shape as one that goes through the control API
    template <typename T>
    Json AnswerText(const T& answer) {
        String text;
        JsonWriter writer(text);
        answer.WriteJson(writer);
        EXPECT_TRUE(writer.IsComplete());
        auto parsed = ParseJson(text);
        EXPECT_TRUE(parsed.has_value());
        return parsed ? std::move(*parsed) : Json();
    }

}  // namespace BECore::Tests
