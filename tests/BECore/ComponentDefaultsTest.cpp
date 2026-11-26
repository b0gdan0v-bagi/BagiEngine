// What a component is by default, compared and reset field by field (SceneEditor/ComponentDefaults).

#include "ComponentDefaultsTest.h"

#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Generated/ComponentDefaultsTest.gen.hpp>
#include <Widgets/SceneEditor/ComponentDefaults.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    void DefaultsProbeComponent::OnPropertyWritten(StringView field) {
        written.emplace_back(field.data(), field.size());
        speedAtWrite.push_back(speed);
    }

    namespace {

        class ComponentDefaultsTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            /// Every field away from its default, the ones that are never compared too
            void ChangeEverything() {
                _probe->speed = 4.0f;
                _probe->title = "changed";
                _probe->tag = "enemy"_intern;
                _probe->mode = DefaultsProbeMode::Fast;
                _probe->active = false;
                _probe->inner.note = "inside";
                _probe->values = {7, 8};
                _probe->limit = 10;
                _probe->item = New<DefaultsProbeItem>();
                _probe->holder.items.push_back(New<DefaultsProbeItem>());
                _probe->secret = 2;
                _probe->serial = 2;
            }

            /// The names of the differing fields, in the order they were answered
            String Differing(IComponent& component) {
                String names;
                for (const ComponentFieldDifference& difference : _defaults.Compare(component)) {
                    if (!names.empty()) {
                        names += ' ';
                    }
                    names.append(difference.field.data(), difference.field.size());
                }
                return names;
            }

            IntrusivePtr<DefaultsProbeComponent> _probe = New<DefaultsProbeComponent>();
            ComponentDefaults _defaults;
        };

    }  // namespace

    TEST_F(ComponentDefaultsTest, NothingOfAFreshComponentDiffers) {
        const IntrusivePtr<TransformComponent> transform = New<TransformComponent>();
        const IntrusivePtr<SpriteRendererComponent> sprite = New<SpriteRendererComponent>();

        EXPECT_EQ(Differing(*_probe), "");
        EXPECT_EQ(Differing(*transform), "");
        EXPECT_EQ(Differing(*sprite), "");
        EXPECT_FALSE(_defaults.ResetAll(*_probe));
        EXPECT_TRUE(_probe->written.empty());
    }

    TEST_F(ComponentDefaultsTest, EveryChangedFieldIsAnsweredButThoseThatHoldObjectsAndTheHiddenOne) {
        ChangeEverything();

        EXPECT_EQ(Differing(*_probe), "speed title tag mode active inner values limit serial");
    }

    TEST_F(ComponentDefaultsTest, ADifferenceTellsTheDefaultAndWhetherItCanBeReset) {
        ChangeEverything();
        Vector<String> texts;

        for (const ComponentFieldDifference& difference : _defaults.Compare(*_probe)) {
            texts.push_back("{}={}{}"_format(difference.field, difference.defaultText, difference.resettable ? "" : " (fixed)"));
        }

        const Vector<String> expected{"speed=1.5", "title=probe", "tag=empty", "mode=Slow", "active=true", "inner=count=2, note=", "values=1 items", "limit=none", "serial=1 (fixed)"};
        EXPECT_EQ(texts, expected);
    }

    TEST_F(ComponentDefaultsTest, AResetOfOneFieldChangesNoOther) {
        ChangeEverything();

        EXPECT_TRUE(_defaults.Reset(*_probe, "speed"));

        EXPECT_FLOAT_EQ(_probe->speed, 1.5f);
        EXPECT_EQ(Differing(*_probe), "title tag mode active inner values limit serial");
        EXPECT_EQ(_probe->limit, Optional<int32_t>(10));
        EXPECT_EQ(_probe->written, (Vector<String>{"speed"}));
    }

    TEST_F(ComponentDefaultsTest, AResetGivesEveryKindOfFieldItsDefault) {
        ChangeEverything();

        for (const StringView field : {"title", "tag", "mode", "active", "inner", "values", "limit"}) {
            EXPECT_TRUE(_defaults.Reset(*_probe, field)) << String(field).c_str();
        }

        EXPECT_EQ(_probe->title, "probe");
        EXPECT_TRUE(_probe->tag.Empty());
        EXPECT_EQ(_probe->mode, DefaultsProbeMode::Slow);
        EXPECT_TRUE(_probe->active);
        EXPECT_TRUE(_probe->inner.note.empty());
        EXPECT_EQ(_probe->values, (Vector<int32_t>{3}));
        EXPECT_FALSE(_probe->limit.has_value());
    }

    TEST_F(ComponentDefaultsTest, AnEmptiedListComesBack) {
        _probe->values.clear();

        EXPECT_EQ(Differing(*_probe), "values");
        EXPECT_TRUE(_defaults.Reset(*_probe, "values"));

        EXPECT_EQ(_probe->values, (Vector<int32_t>{3}));
    }

    TEST_F(ComponentDefaultsTest, AFieldThatIsNeverResetOrDoesNotDifferIsLeftAlone) {
        ChangeEverything();
        _probe->speed = 1.5f;

        for (const StringView field : {"speed", "serial", "secret", "item", "holder", "unknown"}) {
            EXPECT_FALSE(_defaults.Reset(*_probe, field)) << String(field).c_str();
        }

        EXPECT_EQ(_probe->serial, 2);
        EXPECT_EQ(_probe->secret, 2);
        EXPECT_NE(_probe->item.Get(), nullptr);
        EXPECT_EQ(_probe->holder.items.size(), 1u);
        EXPECT_TRUE(_probe->written.empty());
    }

    TEST_F(ComponentDefaultsTest, AResetOfAllWritesEveryValueBeforeTheFirstHook) {
        ChangeEverything();

        EXPECT_TRUE(_defaults.ResetAll(*_probe));

        EXPECT_EQ(Differing(*_probe), "serial");
        EXPECT_EQ(_probe->written, (Vector<String>{"speed", "title", "tag", "mode", "active", "inner", "values", "limit"}));
        for (const float speed : _probe->speedAtWrite) {
            EXPECT_FLOAT_EQ(speed, 1.5f);
        }
        EXPECT_NE(_probe->item.Get(), nullptr);
        EXPECT_EQ(_probe->holder.items.size(), 1u);
        EXPECT_EQ(_probe->secret, 2);
    }

    TEST_F(ComponentDefaultsTest, TheTransformAndTheSpriteAreComparedAndReset) {
        const IntrusivePtr<TransformComponent> transform = New<TransformComponent>();
        const IntrusivePtr<SpriteRendererComponent> sprite = New<SpriteRendererComponent>();
        transform->_x = 12.0f;
        transform->_width = 40.0f;
        sprite->_srcRect = Rect{1.0f, 2.0f, 3.0f, 4.0f};
        sprite->_flipX = true;

        EXPECT_EQ(Differing(*transform), "x width");
        EXPECT_EQ(Differing(*sprite), "srcRect flipX");
        EXPECT_TRUE(_defaults.Reset(*transform, "x"));
        EXPECT_TRUE(_defaults.ResetAll(*sprite));

        EXPECT_FLOAT_EQ(transform->_x, 0.0f);
        EXPECT_FLOAT_EQ(transform->_width, 40.0f);
        EXPECT_EQ(Differing(*sprite), "");
    }

    TEST_F(ComponentDefaultsTest, ASecondComparisonAllocatesNothing) {
        const IntrusivePtr<TransformComponent> transform = New<TransformComponent>();
        const IntrusivePtr<SpriteRendererComponent> sprite = New<SpriteRendererComponent>();
        ChangeEverything();
        transform->_x = 12.0f;
        const auto compare = [&] {
            EXPECT_EQ(_defaults.Compare(*_probe).size(), 9u);
            EXPECT_EQ(_defaults.Compare(*transform).size(), 1u);
            EXPECT_TRUE(_defaults.Compare(*sprite).empty());
        };
        compare();

        ExpectNoAllocations(compare);
    }

}  // namespace BECore::Tests
