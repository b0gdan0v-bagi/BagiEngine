#include <BECore/EditorLayoutHostProbe.h>
#include <Common/EngineEnvironment.h>
#include <Common/TempPath.h>
#include <Widgets/EditorLayout/IEditorLayoutHost.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// The registry the models of the panel assets find the layout widget through
    class EditorLayoutHostTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _directory = TestTempDirectory("EditorLayoutHost");
            MakeEmptyDirectory(_directory);
            _first.emplace(_directory);
        }

        void TearDown() override {
            _first.reset();
            RemoveDirectory(_directory);
        }

        Path _directory;
        Optional<ProbeEditorLayoutHost> _first;
    };

    TEST_F(EditorLayoutHostTest, ARegisteredHostIsFoundAndTheHandleEndsIt) {
        EditorLayoutHostRegistry& registry = EditorLayoutHostRegistry::GetInstance();
        ASSERT_EQ(registry.Get(), nullptr);
        {
            const EditorLayoutHostHandle handle = registry.Register(*_first);
            EXPECT_TRUE(handle);
            EXPECT_EQ(registry.Get(), &*_first);
        }
        EXPECT_EQ(registry.Get(), nullptr);
    }

    TEST_F(EditorLayoutHostTest, ASecondRegistrationReplacesTheFirstAndTheOrphanedHandleLeavesItAlone) {
        EditorLayoutHostRegistry& registry = EditorLayoutHostRegistry::GetInstance();
        ProbeEditorLayoutHost second(_directory);
        EditorLayoutHostHandle firstHandle = registry.Register(*_first);
        const EditorLayoutHostHandle secondHandle = registry.Register(second);
        EXPECT_EQ(registry.Get(), &second);

        firstHandle.Reset();
        EXPECT_EQ(registry.Get(), &second);
    }

    TEST_F(EditorLayoutHostTest, AMovedHandleKeepsTheRegistration) {
        EditorLayoutHostRegistry& registry = EditorLayoutHostRegistry::GetInstance();
        EditorLayoutHostHandle moved;
        {
            EditorLayoutHostHandle handle = registry.Register(*_first);
            moved = std::move(handle);
            EXPECT_FALSE(handle);
        }
        EXPECT_EQ(registry.Get(), &*_first);
        moved.Reset();
        EXPECT_EQ(registry.Get(), nullptr);
    }

}  // namespace BECore::Tests
