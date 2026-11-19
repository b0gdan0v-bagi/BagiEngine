#pragma once

#include <BECore/Config/XmlConfig.h>
#include <BECore/Types/Types.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// Loads the XML text @p xml into a table read from a config node (`UIStrings`, `UIThemes`) and returns the problems it reports, one line each
    template <typename TTable>
    Vector<String> LoadTableXml(TTable& table, const char* xml) {
        auto config = XmlConfig::Create();
        EXPECT_TRUE(config.LoadFromString(xml)) << xml;
        return table.LoadFromXml(config.GetRoot());
    }

    /// Whether one of the problems a table reported contains @p word
    inline bool Mentions(const Vector<String>& problems, StringView word) {
        for (const String& problem : problems) {
            if (StringView(problem).find(word) != StringView::npos) {
                return true;
            }
        }
        return false;
    }

}  // namespace BECore::Tests
