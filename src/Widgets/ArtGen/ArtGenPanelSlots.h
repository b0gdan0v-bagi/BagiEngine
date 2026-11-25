#pragma once

#include <BECore/Types/Types.h>
#include <Widgets/PanelAssets/PanelSlots.h>

namespace BECore {

    class ArtGenPanelModel;
    class ArtGenPresetForm;
    struct ArtGenWorkflowInput;

    /**
     * @brief The drawers of `GeneratePanel.xml` that the format cannot say
     *
     * Owned by `ArtGenPanelModel`, which registers them in its `Initialize` and takes them back when it dies. `artgenWorkflowInputs`: a field per input
     * of the form's workflow that the service does not fill itself, typed by the input (a combo of its choices, a check, an integer, a number, a text),
     * with a reset button while the preset sets its own value; it edits the model's form in place. `artgenReferencePicture`: the picture of the
     * session's reference slot. `artgenVariants`: the grid of the run's variants (thumbnails on a checkerboard, the drag source of an image, the
     * verdict mark, the seed with its dice, Use as reference on the picked one); a click is noted in the model and applied in its `EndUpdate`.
     */
    class ArtGenPanelSlots {
    public:
        explicit ArtGenPanelSlots(ArtGenPanelModel& model) : _model(model) {}

        /// Registers the slots in the global `PanelSlots`; the handles end with this object
        void Register();

    private:
        void DrawWorkflowInputs();
        void DrawReferencePicture();
        void DrawVariants();
        static void DrawInputField(ArtGenPresetForm& form, const ArtGenWorkflowInput& input);

        ArtGenPanelModel& _model;
        PanelSlotHandle _workflowInputs;
        PanelSlotHandle _referencePicture;
        PanelSlotHandle _variants;
    };

}  // namespace BECore
