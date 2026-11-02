#pragma once

#include <BECore/UI/UIElement.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// Expects two element trees to hold the same authored state: names, layout, style, color, flags and children, recursively. Layout results (`rect`, `active`) are not compared.
    inline void ExpectSameTree(const UIElement& expected, const UIElement& actual) {
        EXPECT_EQ(expected.name, actual.name);
        EXPECT_EQ(expected.layout, actual.layout);
        EXPECT_TRUE(expected.style == actual.style) << "style of " << expected.name.CStr();
        EXPECT_EQ(expected.color.r, actual.color.r);
        EXPECT_EQ(expected.color.g, actual.color.g);
        EXPECT_EQ(expected.color.b, actual.color.b);
        EXPECT_EQ(expected.color.a, actual.color.a);
        EXPECT_EQ(expected.blocksInput, actual.blocksInput);
        EXPECT_EQ(expected.interactive, actual.interactive);
        EXPECT_EQ(expected.ignoreSafeArea, actual.ignoreSafeArea);
        ASSERT_EQ(expected.children.size(), actual.children.size()) << "children of " << expected.name.CStr();
        for (size_t i = 0; i < expected.children.size(); ++i) {
            ExpectSameTree(*expected.children[i], *actual.children[i]);
        }
    }

    /// The same for the lists of elements under a canvas root
    inline void ExpectSameElements(const eastl::vector<IntrusivePtrNonAtomic<UIElement>>& expected, const eastl::vector<IntrusivePtrNonAtomic<UIElement>>& actual) {
        ASSERT_EQ(expected.size(), actual.size());
        for (size_t i = 0; i < expected.size(); ++i) {
            ExpectSameTree(*expected[i], *actual[i]);
        }
    }

}  // namespace BECore::Tests
