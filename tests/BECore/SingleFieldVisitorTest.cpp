#include <BECore/SingleFieldProbe.h>
#include <BECore/Widgets/PanelAssets/SingleFieldVisitor.h>
#include <Common/Printers.h>
#include <Generated/SingleFieldProbe.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kNoDepth = 0;

        /// Writes down every call it gets, with its name, and checks that the brackets stay balanced
        class RecordingVisitor : public IPropertyVisitor {
        public:
            bool Visit(StringView name, bool&) override {
                return Note("bool", name);
            }
            bool Visit(StringView name, int8_t&) override {
                return Note("int8", name);
            }
            bool Visit(StringView name, uint8_t&) override {
                return Note("uint8", name);
            }
            bool Visit(StringView name, int16_t&) override {
                return Note("int16", name);
            }
            bool Visit(StringView name, uint16_t&) override {
                return Note("uint16", name);
            }
            bool Visit(StringView name, int32_t&) override {
                return Note("int32", name);
            }
            bool Visit(StringView name, uint32_t&) override {
                return Note("uint32", name);
            }
            bool Visit(StringView name, int64_t&) override {
                return Note("int64", name);
            }
            bool Visit(StringView name, uint64_t&) override {
                return Note("uint64", name);
            }
            bool Visit(StringView name, float&) override {
                return Note("float", name);
            }
            bool Visit(StringView name, double&) override {
                return Note("double", name);
            }
            bool Visit(StringView name, PoolString&) override {
                return Note("pool", name);
            }
            bool Visit(StringView name, String&) override {
                return Note("string", name);
            }
            bool VisitEnum(StringView name, const StringView*, size_t count, size_t&) override {
                Note(Format("enum{}", count), name);
                return false;
            }
            bool BeginCompound(StringView name) override {
                Note("compound", name);
                ++_open;
                return true;
            }
            void EndCompound() override {
                Note("endCompound", "");
                Close();
            }
            void BeginField(const FieldAttributes& attributes) override {
                calls.push_back(Format("beginField hidden={}", attributes.hidden));
                ++_open;
            }
            void EndField() override {
                calls.push_back("endField");
                Close();
            }
            bool BeginList(StringView name, const ListInfo& info) override {
                Note(Format("list{}", info.size), name);
                ++_open;
                return true;
            }
            void EndListElement(size_t index, ListRequest&) override {
                calls.push_back(Format("endElement {}", index));
            }
            void EndList(ListRequest&) override {
                calls.push_back("endList");
                Close();
            }
            bool VisitPresence(StringView name, bool&) override {
                Note("presence", name);
                return false;
            }
            bool VisitAlternative(StringView name, size_t count, size_t& index) override {
                Note(Format("alternative{}/{}", count, index), name);
                return false;
            }
            void VisitValueField(StringView name, const ValueFieldInfo& info) override {
                Note(Format("valueField {}", info.kind), name);
            }
            bool ShowsHidden() const override {
                return showHidden;
            }

            bool Balanced() const {
                return _open == kNoDepth && !_underflow;
            }

            Vector<String> calls;
            bool showHidden = false;
            /// What Visit(int32_t) answers: "the field changed"
            bool changeInts = false;

        private:
            bool Note(StringView kind, StringView name) {
                calls.push_back(Format("{} {}", kind, name));
                return changeInts && kind == "int32";
            }

            void Close() {
                if (_open == kNoDepth) {
                    _underflow = true;
                } else {
                    --_open;
                }
            }

            size_t _open = 0;
            bool _underflow = false;
        };

        /// The calls of a full visit, cut at the top-level fields and keyed by the name of each field's first named call
        struct FullVisit {
            Vector<String> names;
            Vector<Vector<String>> calls;
        };

        bool IsBeginField(const String& call) {
            return StringView(call).substr(0, 10) == "beginField";
        }

        FullVisit Split(const Vector<String>& all) {
            FullVisit visit;
            size_t depth = 0;
            for (const String& call : all) {
                if (IsBeginField(call)) {
                    if (depth == 0) {
                        visit.names.emplace_back();
                        visit.calls.emplace_back();
                    }
                    ++depth;
                }
                visit.calls.back().push_back(call);
                if (visit.names.back().empty() && !IsBeginField(call)) {
                    visit.names.back() = call.substr(call.find(' ') + 1);
                }
                if (call == "endField") {
                    --depth;
                }
            }
            return visit;
        }

        SingleProbe MakeProbe(bool engaged) {
            SingleProbe probe;
            probe.items.resize(2);
            probe.items[1].label = "second";
            probe.numbers = {1, 2};
            if (engaged) {
                probe.maybe = 4;
                probe.choice = String("text");
            }
            return probe;
        }

        Vector<String> ShownCalls(SingleProbe& probe, StringView field, bool showHidden = false) {
            RecordingVisitor inner;
            inner.showHidden = showHidden;
            SingleFieldVisitor single(inner, field);
            probe.AcceptPropertyVisitor(single);
            EXPECT_TRUE(inner.Balanced()) << String(field).c_str();
            return inner.calls;
        }

    }  // namespace

    TEST(SingleFieldVisitorTest, AFieldGetsExactlyTheCallsOfAFullVisit) {
        for (const bool engaged : {false, true}) {
            SingleProbe probe = MakeProbe(engaged);
            RecordingVisitor full;
            probe.AcceptPropertyVisitor(full);
            ASSERT_TRUE(full.Balanced());
            const FullVisit expected = Split(full.calls);
            ASSERT_GE(expected.names.size(), 12u);

            for (size_t i = 0; i < expected.names.size(); ++i) {
                EXPECT_EQ(ShownCalls(probe, expected.names[i]), expected.calls[i]) << "field " << expected.names[i].c_str() << " engaged " << engaged;
            }
        }
    }

    TEST(SingleFieldVisitorTest, AFieldOfTheParentCounts) {
        SingleProbe probe = MakeProbe(false);
        const Vector<String> calls = ShownCalls(probe, "baseValue");
        ASSERT_EQ(calls.size(), 3u);
        EXPECT_EQ(calls[1], "int32 baseValue");
    }

    TEST(SingleFieldVisitorTest, ANameThatIsNoFieldGivesNoCall) {
        SingleProbe probe = MakeProbe(true);
        EXPECT_TRUE(ShownCalls(probe, "nope").empty());
        EXPECT_TRUE(ShownCalls(probe, "").empty());
        // A name that only exists inside a field is not a top-level field
        EXPECT_TRUE(ShownCalls(probe, "first").empty());
        EXPECT_TRUE(ShownCalls(probe, "[0]").empty());
    }

    TEST(SingleFieldVisitorTest, WhatTheInnerVisitorReturnsComesBack) {
        SingleProbe probe = MakeProbe(false);
        RecordingVisitor inner;
        inner.changeInts = true;

        SingleFieldVisitor count(inner, "count");
        EXPECT_TRUE(probe.AcceptPropertyVisitor(count));

        SingleFieldVisitor title(inner, "title");
        EXPECT_FALSE(probe.AcceptPropertyVisitor(title));

        // The change of a field inside a list reaches the caller too
        SingleFieldVisitor numbers(inner, "numbers");
        EXPECT_TRUE(probe.AcceptPropertyVisitor(numbers));
    }

    TEST(SingleFieldVisitorTest, HiddenFieldsFollowTheInnerVisitor) {
        SingleProbe probe = MakeProbe(false);
        EXPECT_TRUE(ShownCalls(probe, "secret", false).empty());
        const Vector<String> shown = ShownCalls(probe, "secret", true);
        ASSERT_EQ(shown.size(), 3u);
        EXPECT_EQ(shown[0], "beginField hidden=true");
    }

    TEST(SingleFieldVisitorTest, OneDecoratorServesSeveralVisits) {
        SingleProbe probe = MakeProbe(true);
        RecordingVisitor inner;
        SingleFieldVisitor single(inner, "count");
        probe.AcceptPropertyVisitor(single);
        EXPECT_TRUE(single.FoundField());
        const size_t first = inner.calls.size();

        single.Reset();
        single.SetFieldName("nope");
        probe.AcceptPropertyVisitor(single);
        EXPECT_FALSE(single.FoundField());
        EXPECT_EQ(inner.calls.size(), first);

        single.SetFieldName("count");
        probe.AcceptPropertyVisitor(single);
        EXPECT_EQ(inner.calls.size(), first * 2);
        EXPECT_TRUE(inner.Balanced());
    }

}  // namespace BECore::Tests
