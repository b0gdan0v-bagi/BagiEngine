#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Resource/TextureLibrary.h>
#include <Widgets/TextureEditor/SpriteLibraryPanelModel.h>

namespace BECore::Tests {

    /// The Sprite Library's model on the library a test names, never the shipped one, and reachable by the test: the widget keeps its models private
    class SpriteLibraryTestModel : public SpriteLibraryPanelModel {
        BE_CLASS(SpriteLibraryTestModel)
    public:
        SpriteLibraryTestModel() {
            UseLibrary(library, [](TextureLibrary&) { return true; });
            instance = this;
        }

        ~SpriteLibraryTestModel() override {
            if (instance == this) {
                instance = nullptr;
            }
        }

        inline static TextureLibrary* library = nullptr;
        /// The model the hosted asset made last; null once it is gone
        inline static SpriteLibraryTestModel* instance = nullptr;
    };

}  // namespace BECore::Tests
