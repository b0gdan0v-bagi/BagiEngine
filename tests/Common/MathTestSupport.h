#pragma once

#include <BECore/Math/Vec3.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Types/Types.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// The default tolerance of ExpectVecNear: the error a float rotation of a unit vector stays within
    inline constexpr float kVecTolerance = 1e-5f;

    inline void ExpectVecNear(const Vec3& actual, const Vec3& expected, float tolerance = kVecTolerance) {
        EXPECT_NEAR(actual.x, expected.x, tolerance);
        EXPECT_NEAR(actual.y, expected.y, tolerance);
        EXPECT_NEAR(actual.z, expected.z, tolerance);
    }

    /// The visitor of a math compound's VisitProperties: adds one to every component of type @p TValue and records the names in the order of
    /// the visit, so a test sees both the order and that the visit edits the component it names
    template <typename TValue>
    struct StepVisitor {
        Vector<String> names;

        bool Visit(const char* name, TValue& value) {
            names.push_back(name);
            value += TValue{1};
            return true;
        }
    };

    /// @p original written by XmlSerializer and read back by XmlDeserializer; nullopt when the written text does not load back
    template <typename T>
    Optional<T> RoundTripThroughXml(const T& original) {
        XmlSerializer serializer;
        if (!serializer.BeginObject("value")) {
            return std::nullopt;
        }
        original.Serialize(serializer);
        serializer.EndObject();

        XmlDeserializer deserializer;
        if (!deserializer.LoadFromString(serializer.SaveToString()) || !deserializer.BeginObject("value")) {
            return std::nullopt;
        }
        T loaded;
        loaded.Deserialize(deserializer);
        deserializer.EndObject();
        return loaded;
    }

}  // namespace BECore::Tests
