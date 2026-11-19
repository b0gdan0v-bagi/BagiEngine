#pragma once

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Text/Font.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <Common/TextTestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// The shipped font on a RecordingRenderer, with the atlas library of the CoreManager cleared before and after: the base of the
    /// atlas library and the text renderer cases
    class FontAtlasLibraryTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            font = LoadTestFont();
            ASSERT_TRUE(font);
            renderer = New<RecordingRenderer>();
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        void TearDown() override {
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        FontAtlasLibrary& Library() const {
            return CoreManager::GetFontAtlasLibrary();
        }

        IntrusivePtr<Font> font;
        IntrusivePtr<RecordingRenderer> renderer;
    };

}  // namespace BECore::Tests
