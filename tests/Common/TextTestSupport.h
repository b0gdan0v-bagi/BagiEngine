#pragma once

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <Events/SubscriptionHolder.h>

namespace BECore::Tests {

    /// A font table for the tests of the face choice: the family "Test" has a regular and a bold face. Both are the PT Sans Regular that ships with the
    /// engine, under two spellings of its path (the bold file is another asset), so a test tells which face was asked for by the path it got
    inline constexpr const char* kTestRegularPath = "assets/fonts/PT_Sans-Web-Regular.ttf";
    inline constexpr const char* kTestBoldPath = "assets/fonts/./PT_Sans-Web-Regular.ttf";

    inline TextSettings MakeTestFamilySettings() {
        TextSettings settings = TextSettings::Default();
        FontFamily family;
        family.name = PoolString::Intern("Test");
        family.faces.push_back({400, PoolString::Intern(kTestRegularPath)});
        family.faces.push_back({700, PoolString::Intern(kTestBoldPath)});
        settings.families.push_back(eastl::move(family));
        return settings;
    }

    /// Puts @p settings in the font atlas library for the life of the object and puts the old ones back
    class ScopedTextSettings {
    public:
        explicit ScopedTextSettings(TextSettings settings) : _saved(CoreManager::GetFontAtlasLibrary().GetSettings()) {
            CoreManager::GetFontAtlasLibrary().SetSettings(eastl::move(settings));
        }
        ~ScopedTextSettings() {
            CoreManager::GetFontAtlasLibrary().SetSettings(eastl::move(_saved));
        }
        ScopedTextSettings(const ScopedTextSettings&) = delete;
        ScopedTextSettings& operator=(const ScopedTextSettings&) = delete;

    private:
        TextSettings _saved;
    };

    /// Counts the warnings with @p part in the text that LogEvent::Flush() delivers while it lives
    class WarningCounter : public SubscriptionHolder {
    public:
        explicit WarningCounter(eastl::string part) : _part(eastl::move(part)) {
            Subscribe<LogEvent, &WarningCounter::OnLog>(this);
        }

        /// Delivers the pending log entries, then returns how many matched so far
        [[nodiscard]] size_t Count() const {
            LogEvent::Flush();
            return _count;
        }

    private:
        void OnLog(const LogEvent& event) {
            const eastl::string_view message = event.message;
            if (event.level == LogLevel::Warning && message.find(eastl::string_view(_part.data(), _part.size())) != eastl::string_view::npos) {
                ++_count;
            }
        }

        eastl::string _part;
        size_t _count = 0;
    };

}  // namespace BECore::Tests
