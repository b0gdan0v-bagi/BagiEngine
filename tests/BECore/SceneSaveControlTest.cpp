// The control method that saves the active scene, `scene_save`, over a ConfigManager of its own on a temporary folder: it never writes a shipped config.

#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/ConfigNames.h>
#include <BECore/Config/ContentVersion.h>
#include <BECore/Config/XmlDocument.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneControl.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Scene/SceneNodeControl.h>
#include <BECore/Scene/SceneSaveControl.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/ContentBinaryFiles.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/SceneTestSupport.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Events/SceneEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kSceneConfig = R"(<?xml version="1.0" encoding="UTF-8"?>
<SceneManager>
  <scenes>
    <scene type="Scene" name="Main">
      <nodes>
        <node name="Box">
          <components>
            <component type="TransformComponent" x="10" y="20" width="30" height="40" />
          </components>
        </node>
        <node ref="nodes/Panel" />
      </nodes>
    </scene>
    <!-- the scene nobody saves -->
    <scene type="Scene" name="Other">
      <nodes>
        <node name="Names">
          <components>
            <component type="TransformComponent" x="1" />
          </components>
        </node>
      </nodes>
    </scene>
  </scenes>
</SceneManager>
)";

        constexpr const char* kPanelFile = R"(<node name="Panel" contentVersion="{}"><components>
  <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
    <elements>
      <element type="UIElement" name="Title" x="8" y="8" width="184" height="30"/>
    </elements>
  </component>
</components></node>)";

        constexpr float kEditedX = 77.0f;
        constexpr int32_t kEditedLayer = 3;
        constexpr float kEditedTitleX = 33.0f;

        class SceneSaveControlTest : public ::testing::Test, public SubscriptionHolder {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                _folder = TestTempDirectory("BagiEngineSceneSaveControlTest");
                RemoveDirectory(_folder);
                WriteFile(ConfigFile(), kSceneConfig);
                WriteFile(PanelFile(), PanelText());
                RegisterSceneControl(_service, [this] { return _active; });
                RegisterSceneNodeControl(_service, [this] { return _active; });
                RegisterSceneSaveControl(_service, [this] { return _active; }, [this] { return _provided; });
                Subscribe<SceneEvents::SceneSavedEvent, &SceneSaveControlTest::OnSaved>(this);
            }

            void TearDown() override {
                _manager.Reset();
                RemoveDirectory(_folder);
            }

            Path ConfigFile() const {
                return _folder / "root" / "config" / "SceneConfig.xml";
            }

            Path PanelFile() const {
                return _folder / "root" / "config" / "nodes" / "Panel.xml";
            }

            static String PanelText() {
                return Format(kPanelFile, kContentVersion);
            }

            Path OverlayFile() const {
                return _folder / "overlay" / "config" / "SceneConfig.xml";
            }

            /// A config manager over the temporary root, with the content overlay when @p overlay, and the scenes of its SceneConfig; Main is active. Use through ASSERT_NO_FATAL_FAILURE
            void Start(bool overlay = false) {
                _fileSystem.AddContentRoot(_folder / "root");
                if (overlay) {
                    ASSERT_TRUE(_fileSystem.SetContentOverlay(_folder / "overlay"));
                }
                _fileSystem.Initialize();
                _configs.Initialize();
                const NodeFileLoader loader = MakeFileSystemNodeLoader(_fileSystem);
                _manager = LoadSceneManager(_configs.GetConfig(ConfigNames::scene), loader);
                _manager->Initialize();
                _active = _manager->GetSceneByName("Main"_intern);
                ASSERT_NE(_active, nullptr);
                _provided = &_configs;
            }

            ControlReply Call(StringView method, StringView params = {}) {
                return CallControlText(_service, 1, method, params);
            }

            ControlService _service;
            Path _folder;
            FileSystem _fileSystem;
            ConfigManager _configs{_fileSystem};
            IntrusivePtr<SceneManager> _manager;
            Scene* _active = nullptr;            // what the providers give
            ConfigManager* _provided = nullptr;  // the same
            Vector<const Scene*> _saved;

        private:
            void OnSaved(const SceneEvents::SceneSavedEvent& event) {
                _saved.push_back(event.scene);
            }
        };

    }  // namespace

    TEST_F(SceneSaveControlTest, TheSavedFileLoadsBackWithTheEditsAndKeepsTheRest) {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_TRUE(Call("scene_set", R"({"node":"Box","component":"TransformComponent","path":"x","value":77})").IsOk());
        ASSERT_TRUE(Call("scene_set_node", R"({"node":"Box","layer":3})").IsOk());
        SceneNode& panel = Require(FindNodeBelow(_active->GetRootNode(), "Panel"));
        Require(Require(panel.GetComponent<UICanvasComponent>()).GetRoot().FindByName("Title"_intern)).style.rect.x = UILength::Px(kEditedTitleX);

        const ControlReply reply = Call("scene_save");

        ASSERT_TRUE(reply.IsOk()) << reply.ErrorMessage().c_str();
        const JsonView result = reply.Result();
        EXPECT_EQ(StringOf(result, "file"), String(_configs.GetConfigFilePath(ConfigNames::scene).View()));
        EXPECT_EQ(RawOf(result, "notes"), "[]");
        EXPECT_EQ(BoolOf(result, "persistent"), Optional<bool>(true));
        EXPECT_EQ(StringOf(result, "scene"), "Main");
        EXPECT_EQ(_saved, Vector<const Scene*>{_active});

        // The edit of the element is an override in the scene file; the node file is as it was
        EXPECT_EQ(ReadFile(PanelFile()), PanelText());
        const String saved = ReadFile(ConfigFile());
        EXPECT_NE(saved.find("<node ref=\"nodes/Panel\">"), String::npos) << saved.c_str();
        EXPECT_NE(saved.find("the scene nobody saves"), String::npos) << saved.c_str();
        EXPECT_NE(saved.find(String(kSceneConfig).substr(String(kSceneConfig).find("<scene type=\"Scene\" name=\"Other\">"))), String::npos) << saved.c_str();

        XmlDocument document;
        ASSERT_TRUE(document.LoadFromFile(ConfigFile()));
        const NodeFileLoader loader = MakeFileSystemNodeLoader(_fileSystem);
        const IntrusivePtr<SceneManager> reloaded = LoadSceneManager(document.GetRoot(), loader);
        reloaded->Initialize();
        Scene& main = Require(reloaded->GetSceneByName("Main"_intern));
        SceneNode& box = Require(FindNodeBelow(main.GetRootNode(), "Box"));
        EXPECT_EQ(box.GetLayer(), kEditedLayer);
        EXPECT_EQ(Require(box.GetComponent<TransformComponent>())._x, kEditedX);
        SceneNode& reloadedPanel = Require(FindNodeBelow(main.GetRootNode(), "Panel"));
        EXPECT_EQ(Require(Require(reloadedPanel.GetComponent<UICanvasComponent>()).GetRoot().FindByName("Title"_intern)).style.rect.x, UILength::Px(kEditedTitleX));
    }

    TEST_F(SceneSaveControlTest, WithAContentOverlayTheOriginalStaysAndTheAnswerNamesTheCopy) {
        ASSERT_NO_FATAL_FAILURE(Start(true));
        ASSERT_TRUE(Call("scene_set_node", R"({"node":"Box","layer":3})").IsOk());

        const ControlReply reply = Call("scene_save");

        ASSERT_TRUE(reply.IsOk()) << reply.ErrorMessage().c_str();
        EXPECT_EQ(StringOf(reply.Result(), "file"), String(OverlayFile().LexicallyNormal().View()));
        EXPECT_EQ(ReadFile(ConfigFile()), String(kSceneConfig));
        EXPECT_NE(ReadFile(OverlayFile()).find("layer=\"3\""), String::npos);
    }

    TEST_F(SceneSaveControlTest, WithoutASceneOrAConfigManagerNothingIsWritten) {
        ASSERT_NO_FATAL_FAILURE(Start());
        Scene* const main = _active;

        _active = nullptr;
        EXPECT_EQ(Call("scene_save").ErrorCode(), "no_scene");
        _active = main;
        _provided = nullptr;
        EXPECT_EQ(Call("scene_save").ErrorCode(), "save_failed");

        EXPECT_EQ(ReadFile(ConfigFile()), String(kSceneConfig));
        EXPECT_TRUE(_saved.empty());
    }

    TEST_F(SceneSaveControlTest, ACookedOrAMissingSceneConfigIsRefused) {
        ASSERT_NO_FATAL_FAILURE(Start());
        const Path cooked = _folder / "cooked" / "config" / "SceneConfig.bin";
        WriteContentBin(cooked, "SceneManager", *_manager);
        const Vector<uint8_t> written = ReadFileBytes(cooked);
        FileSystem cookedFiles;
        cookedFiles.AddContentRoot(_folder / "cooked");
        cookedFiles.Initialize();
        ConfigManager cookedConfigs(cookedFiles);
        cookedConfigs.Initialize();
        ASSERT_TRUE(cookedConfigs.IsBinaryConfig(ConfigNames::scene));

        _provided = &cookedConfigs;
        EXPECT_EQ(Call("scene_save").ErrorCode(), "save_failed");
        EXPECT_EQ(ReadFileBytes(cooked), written);

        MakeDirectory(_folder / "empty" / "config");
        FileSystem emptyFiles;
        emptyFiles.AddContentRoot(_folder / "empty");
        emptyFiles.Initialize();
        ConfigManager emptyConfigs(emptyFiles);
        emptyConfigs.Initialize();

        _provided = &emptyConfigs;
        EXPECT_EQ(Call("scene_save").ErrorCode(), "save_failed");
        EXPECT_FALSE(PathExists(_folder / "empty" / "config" / "SceneConfig.xml"));
        EXPECT_TRUE(_saved.empty());
    }

    TEST_F(SceneSaveControlTest, AFailedWriteIsRefusedAndTheOriginalStays) {
        ASSERT_NO_FATAL_FAILURE(Start(true));
        MakeDirectory(OverlayFile());  // no file can be written where a folder is

        EXPECT_EQ(Call("scene_save").ErrorCode(), "save_failed");

        EXPECT_EQ(ReadFile(ConfigFile()), String(kSceneConfig));
        EXPECT_TRUE(IsFolder(OverlayFile()));
        EXPECT_TRUE(_saved.empty());
    }

}  // namespace BECore::Tests
