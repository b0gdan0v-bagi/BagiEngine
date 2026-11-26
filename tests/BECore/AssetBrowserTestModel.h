#pragma once

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <Widgets/AssetBrowser/AssetBrowserPanelModel.h>

namespace BECore::Tests {

    /// The Asset Browser's model over the content a test names, never the shipped one, and reachable by the test: the widget keeps its models private
    class AssetBrowserTestModel : public AssetBrowserPanelModel {
        BE_CLASS(AssetBrowserTestModel)
    public:
        AssetBrowserTestModel() {
            SetFileSystem(*fileSystem);
            instance = this;
        }

        ~AssetBrowserTestModel() override {
            if (instance == this) {
                instance = nullptr;
            }
        }

        inline static const FileSystem* fileSystem = nullptr;
        /// The model the hosted asset made last; null once it is gone
        inline static AssetBrowserTestModel* instance = nullptr;
    };

}  // namespace BECore::Tests
