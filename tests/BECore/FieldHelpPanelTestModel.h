#pragma once

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <Widgets/SceneEditor/FieldHelpPanelModel.h>

namespace BECore::Tests {

    /// The Field Help panel's model over the file system of a test: the widget makes its models itself and gives a test no way in
    class FieldHelpPanelTestModel : public FieldHelpPanelModel {
        BE_CLASS(FieldHelpPanelTestModel)
    public:
        FieldHelpPanelTestModel() {
            // Null outside the asset test: the type registry's tests make every creatable type
            if (files != nullptr) {
                SetFileSystem(*files);
                made = this;
            }
        }

        inline static const FileSystem* files = nullptr;
        /// The last model made over `files`
        inline static FieldHelpPanelTestModel* made = nullptr;
    };

}  // namespace BECore::Tests
