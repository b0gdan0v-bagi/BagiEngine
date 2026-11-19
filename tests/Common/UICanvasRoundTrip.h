#pragma once

#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIElement.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// @p element as the `<element>` XML a scene file holds: two elements that save the same text hold the same authored state, every reflected field included
    [[nodiscard]] inline String ElementToXml(const UIElement& element) {
        XmlSerializer serializer;
        EXPECT_TRUE(serializer.BeginObject("element"));
        element.Serialize(serializer);
        serializer.EndObject();
        return serializer.SaveToString();
    }

    namespace UICanvasRoundTripDetail {

        /// A new canvas read from @p deserializer, initialised; null after a test failure when the reader reported an error
        inline IntrusivePtr<UICanvasComponent> ReadCanvas(IDeserializer& deserializer) {
            auto loaded = New<UICanvasComponent>();
            if (!deserializer.BeginObject("component")) {
                ADD_FAILURE() << "the saved canvas has no component";
                return {};
            }
            loaded->Deserialize(deserializer);
            deserializer.EndObject();
            if (deserializer.HasErrors()) {
                ADD_FAILURE() << "the saved canvas does not read back without errors";
                return {};
            }
            loaded->Initialize();
            return loaded;
        }

    }  // namespace UICanvasRoundTripDetail

    /// @p canvas saved as the XML of a scene file and read into a new canvas, initialised; null after a test failure when a step fails (`ASSERT_TRUE(loaded)`).
    /// @p savedXml, when given, receives the text that was written
    [[nodiscard]] inline IntrusivePtr<UICanvasComponent> XmlRoundTrip(const UICanvasComponent& canvas, String* savedXml = nullptr) {
        XmlSerializer serializer;
        if (!serializer.BeginObject("component")) {
            ADD_FAILURE() << "the canvas cannot be saved";
            return {};
        }
        canvas.Serialize(serializer);
        serializer.EndObject();
        const String xml = serializer.SaveToString();
        if (savedXml != nullptr) {
            *savedXml = xml;
        }
        XmlDeserializer deserializer;
        if (!deserializer.LoadFromString(StringView(xml))) {
            ADD_FAILURE() << "the saved XML does not parse: " << xml.c_str();
            return {};
        }
        return UICanvasRoundTripDetail::ReadCanvas(deserializer);
    }

    /// The same through the binary stream of a build's content
    [[nodiscard]] inline IntrusivePtr<UICanvasComponent> BinaryRoundTrip(const UICanvasComponent& canvas) {
        BinarySerializer serializer;
        if (!serializer.BeginObject("component")) {
            ADD_FAILURE() << "the canvas cannot be saved";
            return {};
        }
        canvas.Serialize(serializer);
        serializer.EndObject();
        BinaryDeserializer deserializer;
        if (!deserializer.LoadFromBuffer(serializer.GetData().data(), serializer.GetData().size())) {
            ADD_FAILURE() << "the saved stream does not load";
            return {};
        }
        return UICanvasRoundTripDetail::ReadCanvas(deserializer);
    }

    /// Every root element of @p loaded saves as the same XML as the same element of @p expected (ElementToXml), and there are as many
    inline void ExpectSameElementXml(const UICanvasComponent& expected, const UICanvasComponent& loaded) {
        ASSERT_EQ(loaded._elements.size(), expected._elements.size());
        for (size_t i = 0; i < loaded._elements.size(); ++i) {
            EXPECT_STREQ(ElementToXml(*expected._elements[i]).c_str(), ElementToXml(*loaded._elements[i]).c_str()) << i;
        }
    }

}  // namespace BECore::Tests
