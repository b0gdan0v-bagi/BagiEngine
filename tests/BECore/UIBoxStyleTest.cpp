#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIPanel.h>
#include <Common/RecordingRenderer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kPixelTolerance = 1.5f;  // the feather ring lies about a pixel outside the rect

        bool SameColor(const Color& a, const Color& b) {
            return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
        }

        IntrusivePtr<UIElement> MakeElement(const Rect& rect = {10.0f, 20.0f, 100.0f, 50.0f}, const Color& color = {200, 100, 50, 255}) {
            auto element = New<UIElement>();
            element->rect = rect;
            element->color = color;
            return element;
        }

        eastl::vector<DrawCall> Draw(UIElement& element, RecordingRenderer& renderer) {
            renderer.ClearCalls();
            element.DrawSelf(renderer);
            return renderer.calls;
        }

        /// The element of a one-element canvas file: what a screen's XML makes of @p attributes
        struct LoadedElement {
            IntrusivePtr<UICanvasComponent> canvas;
            bool hadErrors = false;
            eastl::vector<DeserializeError> errors;

            [[nodiscard]] UIElement& Get() const {
                return *canvas->_elements[0];
            }
        };

        LoadedElement LoadElement(const char* type, const char* attributes) {
            const eastl::string xml = Format("<root><component><elements><element type=\"{}\" name=\"A\" {}/></elements></component></root>", type, attributes);
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size())));
            LoadedElement loaded;
            loaded.canvas = New<UICanvasComponent>();
            EXPECT_TRUE(deserializer.BeginObject("component"));
            loaded.canvas->Deserialize(deserializer);
            deserializer.EndObject();
            loaded.hadErrors = deserializer.HasErrors();
            loaded.errors = deserializer.GetErrors();
            EXPECT_EQ(loaded.canvas->_elements.size(), 1u);
            return loaded;
        }

        eastl::string ElementToXml(const UIElement& element) {
            XmlSerializer serializer;
            EXPECT_TRUE(serializer.BeginObject("element"));
            element.Serialize(serializer);
            serializer.EndObject();
            return serializer.SaveToString();
        }

        UIBoxStyle EveryField() {
            UIBoxStyle box;
            box.cornerRadius = {4.0f, 8.0f, 12.0f, 0.0f};
            box.borderWidth = 2.5f;
            box.borderColor = Color{1, 2, 3, 4};
            box.gradientColor = Color{5, 6, 7, 8};
            box.gradientDirection = UIGradientDirection::Horizontal;
            box.shadow = {1.0f, 2.0f, 3.0f, 4.0f, Color{9, 10, 11, 12}};
            box.glow = {-1.0f, -2.0f, 5.0f, -0.5f, Color{13, 14, 15, 16}};
            box.highlight = Color{17, 18, 19, 20};
            return box;
        }

        void ExpectValidMesh(const DrawCall& call) {
            ASSERT_EQ(call.kind, DrawKind::Geometry);
            ASSERT_FALSE(call.indices.empty());
            EXPECT_EQ(call.indices.size() % 3, 0u);
            for (const int index : call.indices) {
                ASSERT_GE(index, 0);
                ASSERT_LT(static_cast<size_t>(index), call.vertices.size());
            }
        }

    }  // namespace

    // ---- drawing -----------------------------------------------------------------------------------------------------------

    TEST(UIBoxStyleTest, ADefaultStyleIsOneRectCallAsBefore) {
        auto element = MakeElement();
        RecordingRenderer renderer;
        EXPECT_FALSE(element->box.HasEffect());

        const auto calls = Draw(*element, renderer);
        ASSERT_EQ(calls.size(), 1u);
        EXPECT_EQ(calls[0].kind, DrawKind::Rect);
        EXPECT_TRUE(SameColor(calls[0].color, element->color));
        EXPECT_FLOAT_EQ(calls[0].rect.x, 10.0f);
        EXPECT_FLOAT_EQ(calls[0].rect.y, 20.0f);
        EXPECT_FLOAT_EQ(calls[0].rect.w, 100.0f);
        EXPECT_FLOAT_EQ(calls[0].rect.h, 50.0f);
    }

    TEST(UIBoxStyleTest, ADefaultStyleWithATransparentColorDrawsNothing) {
        auto element = MakeElement({10.0f, 20.0f, 100.0f, 50.0f}, Color{});
        RecordingRenderer renderer;
        EXPECT_TRUE(Draw(*element, renderer).empty());
    }

    TEST(UIBoxStyleTest, SettingsThatCannotShowKeepTheRectCall) {
        auto element = MakeElement();
        element->box.borderColor = Color{255, 0, 0, 255};  // no width
        element->box.borderWidth = 0.0f;
        element->box.gradientColor = Color{0, 255, 0, 255};  // the direction is None
        element->box.shadow.blur = 6.0f;                     // no alpha
        element->box.glow.spread = 3.0f;
        EXPECT_FALSE(element->box.HasEffect());
        RecordingRenderer renderer;
        const auto calls = Draw(*element, renderer);
        ASSERT_EQ(calls.size(), 1u);
        EXPECT_EQ(calls[0].kind, DrawKind::Rect);
    }

    TEST(UIBoxStyleTest, EachFieldAloneDrawsOneGeometryCall) {
        const UIBoxStyle every = EveryField();
        const eastl::vector<eastl::pair<const char*, UIBoxStyle>> cases = [&] {
            eastl::vector<eastl::pair<const char*, UIBoxStyle>> list;
            UIBoxStyle box;
            box.cornerRadius = every.cornerRadius;
            list.emplace_back("cornerRadius", box);
            box = {};
            box.borderWidth = every.borderWidth;
            box.borderColor = Color{1, 2, 3, 255};
            list.emplace_back("border", box);
            box = {};
            box.gradientDirection = UIGradientDirection::Vertical;
            box.gradientColor = Color{5, 6, 7, 255};
            list.emplace_back("gradient", box);
            box = {};
            box.shadow = {0.0f, 2.0f, 6.0f, 0.0f, Color{0, 0, 0, 160}};
            list.emplace_back("shadow", box);
            box = {};
            box.glow = {0.0f, 0.0f, 6.0f, 1.0f, Color{255, 220, 0, 160}};
            list.emplace_back("glow", box);
            box = {};
            box.highlight = Color{255, 255, 255, 120};
            list.emplace_back("highlight", box);
            return list;
        }();

        RecordingRenderer renderer;
        for (const auto& [name, box] : cases) {
            auto element = MakeElement();
            element->box = box;
            EXPECT_TRUE(box.HasEffect()) << name;
            const auto calls = Draw(*element, renderer);
            ASSERT_EQ(calls.size(), 1u) << name;
            ExpectValidMesh(calls[0]);
        }
    }

    TEST(UIBoxStyleTest, TheBoxIsPaintedShadowGlowFillBorderHighlight) {
        auto element = MakeElement({10.0f, 20.0f, 100.0f, 50.0f}, Color{3, 0, 0, 255});
        element->box.cornerRadius = UICorners::Uniform(8.0f);
        element->box.shadow = {2.0f, 2.0f, 6.0f, 0.0f, Color{1, 0, 0, 200}};
        element->box.glow = {0.0f, 0.0f, 4.0f, 0.0f, Color{2, 0, 0, 200}};
        element->box.borderWidth = 2.0f;
        element->box.borderColor = Color{4, 0, 0, 255};
        element->box.highlight = Color{5, 0, 0, 255};

        RecordingRenderer renderer;
        const auto calls = Draw(*element, renderer);
        ASSERT_EQ(calls.size(), 1u) << "one draw: the order of the triangles is the order of the paint";
        ExpectValidMesh(calls[0]);

        // Every part has its own red channel; the triangles must visit them in this order, each one once
        eastl::vector<int> order;
        for (size_t i = 0; i + 2 < calls[0].indices.size(); i += 3) {
            const int part = calls[0].vertices[static_cast<size_t>(calls[0].indices[i])].color.r;
            if (order.empty() || order.back() != part) {
                order.push_back(part);
            }
        }
        const eastl::vector<int> expected = {1, 2, 3, 4, 5};
        EXPECT_EQ(order, expected);
    }

    TEST(UIBoxStyleTest, AGradientWithATransparentColorStillDraws) {
        auto element = MakeElement({10.0f, 20.0f, 100.0f, 50.0f}, Color{});
        element->box.gradientDirection = UIGradientDirection::Vertical;
        element->box.gradientColor = Color{255, 0, 0, 255};
        RecordingRenderer renderer;
        const auto calls = Draw(*element, renderer);
        ASSERT_EQ(calls.size(), 1u);
        ExpectValidMesh(calls[0]);
        unsigned char peak = 0;
        for (const GeometryVertex& vertex : calls[0].vertices) {
            peak = std::max(peak, vertex.color.a);
        }
        EXPECT_EQ(peak, 255) << "the far end is opaque";
    }

    TEST(UIBoxStyleTest, AStyleWithOnlyAShadowDrawsTheShadowAndNoFill) {
        auto element = MakeElement({10.0f, 20.0f, 100.0f, 50.0f}, Color{});
        element->box.shadow = {0.0f, 4.0f, 8.0f, 0.0f, Color{9, 8, 7, 200}};
        RecordingRenderer renderer;
        const auto calls = Draw(*element, renderer);
        ASSERT_EQ(calls.size(), 1u);
        ExpectValidMesh(calls[0]);
        for (const GeometryVertex& vertex : calls[0].vertices) {
            EXPECT_EQ(vertex.color.r, 9);
            EXPECT_EQ(vertex.color.g, 8);
            EXPECT_EQ(vertex.color.b, 7) << "every vertex is the shadow's: the element's own colour is transparent, so there is no fill";
        }
    }

    TEST(UIBoxStyleTest, TheViewTransformAndTheClipApplyToTheGeometry) {
        auto element = MakeElement({10.0f, 20.0f, 100.0f, 50.0f});
        element->box.cornerRadius = UICorners::Uniform(6.0f);

        RecordingRenderer renderer;
        renderer.SetViewTransform({2.0f, 5.0f, 7.0f});
        renderer.PushClipRect({0.0f, 0.0f, 400.0f, 300.0f});
        const auto calls = Draw(*element, renderer);
        ASSERT_EQ(calls.size(), 1u);
        ASSERT_EQ(calls[0].kind, DrawKind::Geometry);
        // The rect in output pixels is (25, 47, 200 x 100); the feather ring adds about a pixel on every side
        EXPECT_NEAR(calls[0].rect.x, 25.0f, kPixelTolerance);
        EXPECT_NEAR(calls[0].rect.y, 47.0f, kPixelTolerance);
        EXPECT_NEAR(calls[0].rect.w, 200.0f, 2.0f * kPixelTolerance);
        EXPECT_NEAR(calls[0].rect.h, 100.0f, 2.0f * kPixelTolerance);
        ASSERT_TRUE(calls[0].clip.has_value());
        EXPECT_FLOAT_EQ(calls[0].clip->w, 400.0f);

        renderer.PushClipRect({500.0f, 500.0f, 10.0f, 10.0f});  // no overlap: everything is hidden
        EXPECT_TRUE(Draw(*element, renderer).empty());
    }

    TEST(UIBoxStyleTest, TheFeatherFollowsTheScaleOfTheView) {
        auto element = MakeElement({10.0f, 20.0f, 100.0f, 50.0f});
        element->box.cornerRadius = UICorners::Uniform(6.0f);
        RecordingRenderer renderer;
        for (const float scale : {0.5f, 1.0f, 3.0f}) {
            renderer.SetViewTransform({scale, 0.0f, 0.0f});
            const auto calls = Draw(*element, renderer);
            ASSERT_EQ(calls.size(), 1u);
            // The outer ring is one output pixel wide, whatever the scale: the outline of the box in pixels grows by it
            EXPECT_NEAR(calls[0].rect.w, 100.0f * scale, 2.0f * kPixelTolerance) << scale;
            EXPECT_GE(calls[0].rect.w, 100.0f * scale) << scale;
        }
    }

    TEST(UIBoxStyleTest, AnEmptyRectDrawsNothing) {
        auto element = MakeElement({10.0f, 20.0f, 0.0f, 0.0f});
        element->box = EveryField();
        RecordingRenderer renderer;
        EXPECT_TRUE(Draw(*element, renderer).empty());
        element->box = {};
        EXPECT_TRUE(Draw(*element, renderer).empty());
    }

    TEST(UIBoxStyleTest, TheScratchVectorsAreReusedAndTheCallsDoNotGrow) {
        auto element = MakeElement();
        element->box = EveryField();
        element->box.shadow.color.a = 200;
        RecordingRenderer renderer;
        const auto first = Draw(*element, renderer);
        const auto second = Draw(*element, renderer);
        ASSERT_EQ(first.size(), 1u);
        ASSERT_EQ(second.size(), 1u);
        EXPECT_EQ(first[0].vertices.size(), second[0].vertices.size()) << "a second draw starts from empty lists";
        EXPECT_EQ(first[0].indices.size(), second[0].indices.size());
    }

    TEST(UIBoxStyleTest, ADrawBoxOfAControlUsesItsOwnColors) {
        // A control draws the box with the colours of its state: `DrawBox` takes them, the authored style keeps its shape
        class Probe : public UIElement {
        public:
            using UIElement::DrawBox;
        };
        auto probe = New<Probe>();
        probe->rect = {0.0f, 0.0f, 40.0f, 20.0f};
        UIBoxStyle box;
        box.cornerRadius = UICorners::Uniform(4.0f);
        box.gradientDirection = UIGradientDirection::Vertical;
        RecordingRenderer renderer;
        probe->DrawBox(renderer, box, Color{10, 20, 30, 255}, Color{200, 210, 220, 255});
        ASSERT_EQ(renderer.calls.size(), 1u);
        bool hasStart = false;
        bool hasEnd = false;
        for (const GeometryVertex& vertex : renderer.calls[0].vertices) {
            hasStart |= vertex.color.r == 10 && vertex.color.a == 255;
            hasEnd |= vertex.color.r == 200 && vertex.color.a == 255;
        }
        EXPECT_TRUE(hasStart);
        EXPECT_TRUE(hasEnd);
    }

    TEST(UIBoxStyleTest, APanelDrawsItsSpriteOverTheRoundedFill) {
        auto panel = New<UIPanel>();
        panel->rect = {0.0f, 0.0f, 64.0f, 64.0f};
        panel->color = Color{10, 20, 30, 255};
        panel->box.cornerRadius = UICorners::Uniform(8.0f);
        RecordingRenderer renderer;
        const eastl::vector<uint8_t> pixels(16 * 16 * 4, 128);
        panel->GetSprite().SetTexture(renderer.CreateTexture(pixels.data(), 16, 16), Rect{0, 0, 16, 16});
        panel->sprite = PoolString::Intern("Anything");

        const auto calls = Draw(*panel, renderer);
        ASSERT_EQ(calls.size(), 2u);
        EXPECT_EQ(calls[0].kind, DrawKind::Geometry);
        EXPECT_EQ(calls[1].kind, DrawKind::Texture);
    }

    // ---- the text forms ----------------------------------------------------------------------------------------------------

    TEST(UIBoxStyleTest, TheCornersTextFormRoundTrips) {
        EXPECT_EQ(UIText::FormatCorners(UICorners::Uniform(8.0f)), "8");
        EXPECT_EQ(UIText::FormatCorners({1.0f, 2.0f, 3.0f, 4.5f}), "1,2,3,4.5");

        const auto all = UIText::ParseCorners(" 8 ");
        ASSERT_TRUE(all.has_value());
        EXPECT_TRUE(*all == UICorners::Uniform(8.0f));
        const auto each = UIText::ParseCorners("1, 2,3 ,4.5");
        ASSERT_TRUE(each.has_value());
        EXPECT_TRUE(*each == (UICorners{1.0f, 2.0f, 3.0f, 4.5f}));

        for (const char* bad : {"", "a", "1,2", "1,2,3", "1,2,3,4,5", "1,2,3,x", "nan", "1,inf,3,4", "8px"}) {
            EXPECT_FALSE(UIText::ParseCorners(bad).has_value()) << bad;
        }
    }

    TEST(UIBoxStyleTest, TheShadowTextFormRoundTrips) {
        const UIShadowSpec shadow{1.0f, -2.0f, 6.5f, 0.5f, Color{9, 200, 11, 128}};
        const eastl::string text = UIText::FormatShadow(shadow);
        EXPECT_EQ(text, "1,-2,6.5,0.5,#09C80B80");
        const auto back = UIText::ParseShadow(text);
        ASSERT_TRUE(back.has_value());
        EXPECT_TRUE(*back == shadow);

        const auto decimal = UIText::ParseShadow(" 0, 4, 8, 0, 0,0,0,160 ");
        ASSERT_TRUE(decimal.has_value());
        EXPECT_TRUE(*decimal == (UIShadowSpec{0.0f, 4.0f, 8.0f, 0.0f, Color{0, 0, 0, 160}})) << "the colour may be written as r,g,b,a";

        for (const char* bad : {"", "1,2,3", "1,2,3,4", "1,2,3,4,", "1,2,3,4,zz", "1,2,-3,4,#000000", "a,2,3,4,#000000", "1,2,3,4,0,0,0", "1,2,3,4,300,0,0,0", "1,2,inf,4,#000000"}) {
            EXPECT_FALSE(UIText::ParseShadow(bad).has_value()) << bad;
        }
    }

    // ---- the file ----------------------------------------------------------------------------------------------------------

    TEST(UIBoxStyleTest, EveryFieldSurvivesAnXmlRoundTrip) {
        const UIBoxStyle every = EveryField();
        auto original = New<UIPanel>();
        original->name = PoolString::Intern("A");
        original->box = every;

        auto canvas = New<UICanvasComponent>();
        canvas->_elements.push_back(original);
        XmlSerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("component"));
        canvas->Serialize(serializer);
        serializer.EndObject();
        const eastl::string xml = serializer.SaveToString();
        for (const char* name : {"cornerRadius=\"4,8,12,0\"", "borderWidth=", "borderColor=", "gradientColor=", "gradientDirection=\"Horizontal\"", "shadow=", "glow=", "highlight="}) {
            EXPECT_NE(xml.find(name), eastl::string::npos) << name << " in " << xml.c_str();
        }

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size())));
        auto loaded = New<UICanvasComponent>();
        ASSERT_TRUE(deserializer.BeginObject("component"));
        loaded->Deserialize(deserializer);
        deserializer.EndObject();
        EXPECT_FALSE(deserializer.HasErrors());
        ASSERT_EQ(loaded->_elements.size(), 1u);
        EXPECT_TRUE(loaded->_elements[0]->box == every);
        EXPECT_STREQ(ElementToXml(*original).c_str(), ElementToXml(*loaded->_elements[0]).c_str());
    }

    TEST(UIBoxStyleTest, AFileCanWriteEveryFieldByHand) {
        const auto loaded = LoadElement("UIPanel", "cornerRadius=\"16\" borderWidth=\"2\" borderColor=\"#FFFFFF80\" gradientDirection=\"Vertical\" gradientColor=\"10,20,30,255\" "
                                                   "shadow=\"0,4,12,0,#00000099\" glow=\"0,0,8,2,255,200,0,128\" highlight=\"255,255,255,60\"");
        EXPECT_FALSE(loaded.hadErrors);
        const UIBoxStyle& box = loaded.Get().box;
        EXPECT_TRUE(box.cornerRadius == UICorners::Uniform(16.0f));
        EXPECT_FLOAT_EQ(box.borderWidth, 2.0f);
        EXPECT_TRUE(SameColor(box.borderColor, Color{255, 255, 255, 128}));
        EXPECT_EQ(box.gradientDirection, UIGradientDirection::Vertical);
        EXPECT_TRUE(SameColor(box.gradientColor, Color{10, 20, 30, 255}));
        EXPECT_TRUE(box.shadow == (UIShadowSpec{0.0f, 4.0f, 12.0f, 0.0f, Color{0, 0, 0, 153}}));
        EXPECT_TRUE(box.glow == (UIShadowSpec{0.0f, 0.0f, 8.0f, 2.0f, Color{255, 200, 0, 128}}));
        EXPECT_TRUE(SameColor(box.highlight, Color{255, 255, 255, 60}));
    }

    TEST(UIBoxStyleTest, AMalformedValueIsReportedAndTheDefaultStays) {
        for (const char* attribute : {"cornerRadius=\"round\"", "cornerRadius=\"1,2\"", "shadow=\"1,2,3\"", "shadow=\"0,0,-4,0,#000000\"", "glow=\"x\"", "borderColor=\"red\"", "gradientColor=\"1,2\"",
                                      "highlight=\"#12345\""}) {
            const auto loaded = LoadElement("UIElement", attribute);
            EXPECT_TRUE(loaded.hadErrors) << attribute;
            EXPECT_TRUE(loaded.Get().box == UIBoxStyle{}) << attribute;
        }
    }

    TEST(UIBoxStyleTest, ASavedDefaultElementWritesNoBoxAttribute) {
        for (const char* type : {"UIElement", "UIPanel", "UILabel", "UIButton", "UIImage", "UIStack"}) {
            auto element = AbstractFactory<UIElement>::GetInstance().Create(type);
            ASSERT_TRUE(element) << type;
            const eastl::string xml = ElementToXml(*element);
            for (const char* name : {"cornerRadius", "borderWidth", "borderColor", "gradientColor", "gradientDirection", "shadow", "glow", "highlight"}) {
                EXPECT_EQ(xml.find(name), eastl::string::npos) << type << " writes " << name << ": " << xml.c_str();
            }
        }
    }

    TEST(UIBoxStyleTest, ADocumentWithoutTheBoxLoadsUnchanged) {
        const auto loaded = LoadElement("UIPanel", "x=\"10\" y=\"20\" width=\"100\" height=\"50\" color=\"40,90,60,255\" blocksInput=\"false\"");
        EXPECT_FALSE(loaded.hadErrors);
        const UIElement& element = loaded.Get();
        EXPECT_TRUE(element.box == UIBoxStyle{});
        EXPECT_FALSE(element.box.HasEffect());
        EXPECT_TRUE(SameColor(element.color, Color{40, 90, 60, 255}));
        EXPECT_FLOAT_EQ(element.style.rect.x, 10.0f);
        EXPECT_FALSE(element.blocksInput);
    }

}  // namespace BECore::Tests
