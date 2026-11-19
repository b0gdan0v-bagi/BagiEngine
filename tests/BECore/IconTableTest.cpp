#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Scene/Components/ChangeToRandomColorDebugComponent.h>
#include <BECore/Scene/Components/ClickableComponent.h>
#include <BECore/Scene/Components/PointerTargetComponent.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/RenderProbeComponent.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Text/TextComponent.h>
#include <BECore/Text/Utf8.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Widgets/EditorLayout/IconTable.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <imgui.h>

namespace BECore::Tests {

    namespace {

        constexpr unsigned int kIconSourceIdx = 1;
        constexpr size_t kMinWidgetIconTokens = 20;
        constexpr const char* kIconHeaderPath = "src/Widgets/Icons/IconsLucide.h";
        constexpr const char* kWidgetsPath = "src/Widgets";
        constexpr const char* kIconTokenPrefix = "ICON_LC_";

        /// The code point of the one well-formed UTF-8 character in @p glyph, or 0 when it is not exactly one
        unsigned int DecodeSingleCharacter(StringView glyph) {
            size_t pos = 0;
            const char32_t codepoint = Utf8::DecodeNext(glyph, pos);
            return pos == glyph.size() && codepoint != Utf8::kReplacement ? static_cast<unsigned int>(codepoint) : 0;
        }

        bool IsTokenCharacter(char c) {
            return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        }

        bool ReadWholeFile(const std::filesystem::path& path, String& text) {
            std::ifstream file(path, std::ios::binary);
            if (path.empty() || !file) {
                return false;
            }
            std::ostringstream content;
            content << file.rdbuf();
            text = FromStdString(content.str());
            return true;
        }

        /// `#define ICON_LC_NAME "<bytes>"  // U+xxxx` of the vendored header: name to codepoint
        Map<String, unsigned int> ReadHeaderCodepoints() {
            Map<String, unsigned int> codepoints;
            String text;
            if (!ReadWholeFile(CoreManager::GetFileSystem().ResolvePath(kIconHeaderPath), text)) {
                return codepoints;
            }
            const String definePrefix = Format("#define {}", kIconTokenPrefix);
            std::istringstream lines(ToStdString(text));
            std::string stdLine;  //-NoStd: std::getline fills a std::string
            while (std::getline(lines, stdLine)) {
                const String line = FromStdString(stdLine);
                if (line.rfind(definePrefix, 0) != 0) {
                    continue;
                }
                const size_t nameStart = std::strlen("#define ");
                const size_t nameEnd = line.find(' ', nameStart);
                const size_t comment = line.find("// U+");
                if (nameEnd == String::npos || comment == String::npos) {
                    continue;
                }
                const char* hexStart = line.c_str() + comment + std::strlen("// U+");
                unsigned int value = 0;
                if (std::from_chars(hexStart, line.c_str() + line.size(), value, 16).ec != std::errc{}) {
                    continue;
                }
                codepoints[line.substr(nameStart, nameEnd - nameStart)] = value;
            }
            return codepoints;
        }

        /// Every distinct `ICON_LC_*` token of the widget sources that is not in a `//` comment (the vendored header folder is skipped); false when a file cannot be read
        bool CollectWidgetIconTokens(Set<String>& tokens) {
            const std::filesystem::path root = CoreManager::GetFileSystem().ResolvePath(kWidgetsPath);
            if (root.empty()) {
                return false;
            }
            std::error_code error;
            const std::filesystem::recursive_directory_iterator finished;                                                         //-NoStd: a test walks the widget sources once, outside the file layer
            for (std::filesystem::recursive_directory_iterator it(root, error); !error && it != finished; it.increment(error)) {  //-NoStd: see above
                const std::filesystem::directory_entry& entry = *it;
                if (!entry.is_regular_file() || (entry.path().extension() != ".cpp" && entry.path().extension() != ".h")) {
                    continue;
                }
                if (entry.path().parent_path().filename() == "Icons") {
                    continue;
                }
                String text;
                if (!ReadWholeFile(entry.path(), text)) {
                    return false;
                }
                for (size_t at = text.find(kIconTokenPrefix); at != String::npos; at = text.find(kIconTokenPrefix, at + 1)) {
                    // A name in a comment ("`{"name", ICON_LC_NAME}`") is not a use
                    const size_t lineStart = text.rfind('\n', at);
                    const size_t comment = text.find("//", lineStart == String::npos ? 0 : lineStart);
                    if (comment != String::npos && comment < at) {
                        continue;
                    }
                    size_t end = at + std::strlen(kIconTokenPrefix);
                    while (end < text.size() && IsTokenCharacter(text[end])) {
                        ++end;
                    }
                    if (end > at + std::strlen(kIconTokenPrefix)) {
                        tokens.insert(text.substr(at, end - at));
                    }
                }
            }
            return !error;
        }

        /// A context with no backend and the UI font as the editor builds it, beside a font of the icon file alone
        class IconTableFontTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                fonts = LoadImGuiFonts(io, buffers);
                ImFontConfig iconOnlyConfig;
                iconOnlyConfig.FontDataOwnedByAtlas = false;
                iconOnly = io.Fonts->AddFontFromMemoryTTF(buffers.icons.data(), static_cast<int>(buffers.icons.size()), kUiFontSize, &iconOnlyConfig);
            }

            void TearDown() override {
                // The context goes first: its atlas reads the buffers
                ImGui::DestroyContext();
                SetActiveImGuiFonts({});
            }

            /// The icon font has the codepoint and the UI font draws it from the Lucide source
            void ExpectDrawnFromLucide(unsigned int codepoint, const char* what) {
                ASSERT_NE(fonts.ui, nullptr);
                ASSERT_NE(iconOnly, nullptr);
                EXPECT_GE(codepoint, static_cast<unsigned int>(ICON_MIN_LC)) << what;
                EXPECT_LE(codepoint, static_cast<unsigned int>(ICON_MAX_LC)) << what;
                EXPECT_TRUE(iconOnly->IsGlyphInFont(static_cast<ImWchar>(codepoint))) << what << ": lucide.ttf has no glyph for it";
                ImFontBaked* baked = fonts.ui->GetFontBaked(kUiFontSize);
                ASSERT_NE(baked, nullptr);
                const ImFontGlyph* glyph = baked->FindGlyph(static_cast<ImWchar>(codepoint));
                ASSERT_NE(glyph, nullptr) << what;
                EXPECT_EQ(glyph->SourceIdx, kIconSourceIdx) << what << ": not drawn from Lucide";
            }

            ImGuiFontBuffers buffers;
            ImGuiFonts fonts;
            ImFont* iconOnly = nullptr;
        };

    }  // namespace

    TEST(IconTableTest, RowsAreSortedByNameWithoutDuplicates) {
        const auto table = GetIconTable();
        ASSERT_FALSE(table.empty());
        for (size_t i = 1; i < table.size(); ++i) {
            EXPECT_LT(table[i - 1].name, table[i].name) << "at row " << i;
        }
    }

    TEST(IconTableTest, EveryGlyphIsNonEmptyAndEveryNameIsPlain) {
        for (const IconEntry& entry : GetIconTable()) {
            ASSERT_NE(entry.glyph, nullptr) << entry.name.data();
            EXPECT_GT(std::strlen(entry.glyph), 0u) << entry.name.data();
            EXPECT_FALSE(entry.name.empty());
            for (const char c : entry.name) {
                EXPECT_TRUE((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') << entry.name.data();
            }
        }
    }

    TEST_F(IconTableFontTest, EveryRowOfTheTableIsOneLucideCharacterTheFontDrawsFromTheIconSource) {
        for (const IconEntry& entry : GetIconTable()) {
            ASSERT_NE(entry.glyph, nullptr) << entry.name.data();
            const unsigned int codepoint = DecodeSingleCharacter(entry.glyph);
            ASSERT_NE(codepoint, 0u) << entry.name.data() << " is not one UTF-8 character";
            ASSERT_NO_FATAL_FAILURE(ExpectDrawnFromLucide(codepoint, entry.name.data()));
        }
    }

    TEST_F(IconTableFontTest, EveryIconMacroTheWidgetsUseIsDrawnFromTheIconSource) {
        const Map<String, unsigned int> header = ReadHeaderCodepoints();
        ASSERT_FALSE(header.empty()) << "cannot read " << kIconHeaderPath;
        Set<String> tokens;
        ASSERT_TRUE(CollectWidgetIconTokens(tokens)) << "cannot read a source under " << kWidgetsPath;
        ASSERT_GE(tokens.size(), kMinWidgetIconTokens) << "the scan of " << kWidgetsPath << " found too few ICON_LC_* tokens";
        for (const String& token : tokens) {
            const auto found = header.find(token);
            ASSERT_NE(found, header.end()) << token << " is not defined in " << kIconHeaderPath;
            ASSERT_NO_FATAL_FAILURE(ExpectDrawnFromLucide(found->second, token.c_str()));
        }
    }

    TEST(IconTableTest, FindsEveryRowAndNothingElse) {
        for (const IconEntry& entry : GetIconTable()) {
            EXPECT_EQ(FindIcon(entry.name), entry.glyph) << entry.name.data();
        }
        EXPECT_STREQ(FindIcon("save"), ICON_LC_SAVE);
        EXPECT_EQ(FindIcon("no-such-icon"), nullptr);
        EXPECT_EQ(FindIcon(""), nullptr);
        EXPECT_EQ(FindIcon("sav"), nullptr);
        EXPECT_EQ(FindIcon("savee"), nullptr);
    }

    TEST(IconTableTest, TheGenericNodeIconIsABox) {
        EXPECT_STREQ(GetNodeIcon(), ICON_LC_BOX);
        EXPECT_EQ(FindIcon("box"), GetNodeIcon());
    }

    TEST(IconTableTest, EveryIconOfTheEnginesComponentsResolves) {
        const ClassMeta metas[] = {
            TransformComponent::GetStaticTypeMeta(),     SpriteRendererComponent::GetStaticTypeMeta(),
            QuadRendererComponent::GetStaticTypeMeta(),  TextComponent::GetStaticTypeMeta(),
            UICanvasComponent::GetStaticTypeMeta(),      ClickableComponent::GetStaticTypeMeta(),
            RenderProbeComponent::GetStaticTypeMeta(),   ChangeToRandomColorDebugComponent::GetStaticTypeMeta(),
            PointerTargetComponent::GetStaticTypeMeta(),
        };
        for (const ClassMeta& meta : metas) {
            EXPECT_FALSE(meta.icon.empty()) << meta.typeName.data();
            EXPECT_NE(FindIcon(meta.icon), nullptr) << meta.typeName.data() << " names '" << meta.icon.data() << "'";
        }
    }

    TEST(IconTableTest, IconOfClassFallsBackToTheGenericIcon) {
        EXPECT_EQ(IconOfClass(TransformComponent::GetStaticTypeMeta()), FindIcon("move"));

        ClassMeta none{1, "NoIcon"};
        EXPECT_EQ(IconOfClass(none), GetNodeIcon());

        ClassMeta unknown{2, "UnknownIcon", "no-such-icon"};
        EXPECT_EQ(IconOfClass(unknown), GetNodeIcon());
        EXPECT_EQ(IconOfClass(unknown), GetNodeIcon());  // the answer does not depend on whether the name was reported before
    }

    TEST(IconTableTest, ANodeShowsTheIconOfItsFirstComponentThatNamesOne) {
        EngineEnvironment::EnsureCoreServices();
        auto scene = New<Scene>();
        scene->Initialize();
        auto node = scene->CreateNode("Icons"_intern, nullptr);
        EXPECT_EQ(IconOfNode(*node), GetNodeIcon());

        node->AddComponent(New<TransformComponent>());
        EXPECT_EQ(IconOfNode(*node), FindIcon("move"));

        node->AddComponent(New<SpriteRendererComponent>());
        EXPECT_EQ(IconOfNode(*node), FindIcon("move"));
    }

}  // namespace BECore::Tests
