// The engine's own configs that a reflected type reads, as cookers of `ContentCookRegistry::GetInstance()`.
//
// A registration here claims that the file reads the same from its XML and from its `.bin`: `ContentCookEquivalenceTest` checks it over the
// shipped `config/` (`VerifyCooked`), and a config whose type does not round-trip is left out and copied (the test lists it with the reason).
// A config that no reflected type reads (`UIConfig`, `TextConfig`, `UIStrings`, `UIThemes`, `SDLWindowConfig`: the path in `ApplicationConfig` names the `.xml`) has no
// registrar and is copied. The Games module registers `GamesConfig` beside its type.

#include <BECore/Application/ApplicationConfig.h>
#include <BECore/Application/RunMode.h>
#include <BECore/Assert/AssertHandlers.h>
#include <BECore/Audio/AudioSettings.h>
#include <BECore/Cook/ContentCooker.h>
#include <BECore/Logger/LoggerManager.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Tests/TestList.h>
#include <BECore/Widgets/WidgetList.h>

namespace BECore {

    namespace {

        constexpr StringView kTextureLibraryStem = "TextureLibrary";
        constexpr StringView kTextureLibraryRootType = "TextureLibraryFile";

        void CookTextureLibrary(const ContentCooker& cooker, IDeserializer& reader, ISerializer& writer, Vector<String>& findings) {
            TextureLibrary library;
            library.Load(reader);
            reader.FinishLoad();
            AppendFindings(reader, findings);
            if (!findings.empty()) {
                return;
            }
            if (!WriteContentStamp(writer, cooker.rootType)) {
                findings.push_back(Format("the type '{}' is not reflected in this program", cooker.rootType));
                return;
            }
            library.Write(writer);
        }

        String DescribeTextureLibrary(const ContentCooker& /*cooker*/, IDeserializer& reader) {
            TextureLibrary library;
            library.Load(reader);
            reader.FinishLoad();
            XmlSerializer text;
            library.Write(text);
            return text.SaveToString();
        }

        // `ApplicationConfig` is read under strict values (`LoadApplicationConfig`), so the cook reads it the same way.
        const CookableConfig<ApplicationConfig> kApplicationConfig{"ApplicationConfig", true};
        const CookableConfig<LoggerManager> kLoggerConfig{"LoggerConfig"};
        const CookableConfig<AssertHandlerManager> kAssertHandlersConfig{"AssertHandlersConfig"};
        const CookableConfig<Tests::TestList> kTestsConfig{"TestsConfig"};
        const CookableConfig<WidgetList> kGameWidgetsConfig{"GameWidgetsConfig"};
        const CookableConfig<AudioSettings> kAudioConfig{"AudioConfig"};
        const CookableConfigWith kTextureLibrary{ContentCooker{kTextureLibraryStem, kTextureLibraryRootType, &CookTextureLibrary, &DescribeTextureLibrary, false}};

#if BE_WITH_EDITOR
        // Game mode never reads it, and a build without the editor cannot read the editor widgets' types.
        const CookableConfig<WidgetList> kWidgetsConfig{"WidgetsConfig"};
#endif

    }  // namespace

}  // namespace BECore
