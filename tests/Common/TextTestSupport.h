#pragma once

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Text/FontAtlasLibrary.h>

namespace BECore::Tests {

    /// A font table for the tests of the face choice: the family "Test" has a regular and a bold face. Both are the PT Sans Regular that ships with the
    /// engine, under two spellings of its path (the bold file is another asset), so a test tells which face was asked for by the path it got
    inline constexpr const char* kTestRegularPath = "assets/fonts/PT_Sans-Web-Regular.ttf";
    inline constexpr const char* kTestBoldPath = "assets/fonts/./PT_Sans-Web-Regular.ttf";

    /// The character a layout puts at the end of a line it cut
    inline constexpr char32_t kEllipsis = U'\u2026';

    inline TextSettings MakeTestFamilySettings() {
        TextSettings settings = TextSettings::Default();
        FontFamily family;
        family.name = "Test"_intern;
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

}  // namespace BECore::Tests
