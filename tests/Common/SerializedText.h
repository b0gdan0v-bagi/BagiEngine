#pragma once

#include <BECore/Control/JsonStreamSerializer.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Types/Types.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// The JSON document a value saves through `Serialize` (the production writer: members in the order they are written), the text a reader test feeds
    /// to `JsonDeserializer::Load`
    template <typename T>
    [[nodiscard]] String SerializedText(const T& value) {
        String text;
        JsonWriter writer(text);
        JsonStreamSerializer serializer(writer);
        value.Serialize(serializer);
        serializer.Finish();
        EXPECT_TRUE(writer.IsComplete());
        return text;
    }

}  // namespace BECore::Tests
