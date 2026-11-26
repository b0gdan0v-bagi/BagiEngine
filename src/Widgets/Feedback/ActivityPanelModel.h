#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Widgets/PanelAssets/PanelElements.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <cstdint>

namespace BECore {

    class ControlService;

    /// An entry of the control journal as the cells of a row. `client`, `params` and a Named entry's name are client text
    struct ActivityRow {
        BE_CLASS(ActivityRow)

        BE_REFLECT_FIELD String time;
        BE_REFLECT_FIELD String client;
        BE_REFLECT_FIELD String method;
        BE_REFLECT_FIELD String params;
        BE_REFLECT_FIELD String error;
        /// A table colours a cell only through a field of its row
        BE_REFLECT_FIELD PanelTextStyle errorStyle = PanelTextStyle::Error;
    };

    /// What `ActivityPanel.xml` binds to
    struct ActivityPanelView {
        BE_CLASS(ActivityPanelView)

        BE_REFLECT_FIELD bool hasRows = false;
        /// Newest first
        BE_REFLECT_FIELD Vector<ActivityRow> rows;
    };

    /**
     * @brief The model of the Activity panel asset: the journal of the control API, who did what.
     *
     * Command: `activity.clear` empties the journal. The rows are made again only when the journal's version moved, so a frame with nothing new
     * allocates nothing.
     */
    class ActivityPanelModel : public IPanelModel {
        BE_CLASS(ActivityPanelModel)
    public:
        ActivityPanelModel() = default;

        [[nodiscard]] TypedRef GetObject() override;
        void Update() override;
        bool RunCommand(StringView id, StringView argument) override;

        /// The service whose journal is shown; the one of `CoreManager` unless a test sets another. It must outlive the model
        void SetService(ControlService& service);

        [[nodiscard]] const ActivityPanelView& GetView() const {
            return _view;
        }

    private:
        [[nodiscard]] ControlService& Service() const;
        void RebuildView();

        ActivityPanelView _view;
        ControlService* _service = nullptr;
        uint64_t _builtVersion = 0;  ///< the journal's version the view was made from
    };

}  // namespace BECore
