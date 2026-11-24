#pragma once

#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonViewOps.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Types/Types.h>
#include <Common/ControlTestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// The compact text a reflected answer writes through `JsonWriter`: for a test that compares it with what a file writer wrote
    template <typename T>
    String AnswerString(const T& answer) {
        String text;
        JsonWriter writer(text);
        answer.WriteJson(writer);
        EXPECT_TRUE(writer.IsComplete());
        return text;
    }

    /// What a reflected answer writes through `JsonWriter`, read back: a test that calls a session method directly reads the same shape as one that goes through the control API
    template <typename T>
    ControlReply AnswerReply(const T& answer) {
        const String text = AnswerString(answer);
        ControlReply reply{StringView(text.c_str(), text.size())};
        EXPECT_TRUE(reply.View().IsValid()) << text.c_str();
        return reply;
    }

    /// @p value as compact text with the members of every object in byte order: what a document of a file or a request looks like when its bytes are compared
    inline String SortedTextOf(JsonView value) {
        String text;
        JsonWriter writer(text);
        WriteJsonSorted(writer, value);
        return text;
    }

}  // namespace BECore::Tests
