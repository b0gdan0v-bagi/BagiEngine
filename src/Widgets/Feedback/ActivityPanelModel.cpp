#include "ActivityPanelModel.h"

#include <BECore/Control/ControlActivity.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <Generated/ActivityPanelModel.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/Feedback/ControlClientLabel.h>

namespace BECore {

    namespace {

        constexpr StringView kConnectedText = "connected";
        constexpr StringView kDisconnectedText = "disconnected";
        constexpr StringView kNamedText = "named";

        void Assign(String& out, StringView text) {
            out.assign(text.data(), text.size());
        }

        /// What happened: the method of a call, a word for the other kinds
        StringView MethodText(const ControlActivityEntry& entry) {
            switch (entry.kind) {
                case ControlActivityKind::Connected:
                    return kConnectedText;
                case ControlActivityKind::Disconnected:
                    return kDisconnectedText;
                case ControlActivityKind::Named:
                    return kNamedText;
                case ControlActivityKind::Call:
                    break;
            }
            return StringView(entry.method.data(), entry.method.size());
        }

    }  // namespace

    TypedRef ActivityPanelModel::GetObject() {
        return TypedRef::Of(_view);
    }

    ControlService& ActivityPanelModel::Service() const {
        return _service != nullptr ? *_service : CoreManager::GetControlService();
    }

    void ActivityPanelModel::SetService(ControlService& service) {
        _service = &service;
        RebuildView();
    }

    void ActivityPanelModel::Update() {
        if (Service().Activity().GetVersion() != _builtVersion) {
            RebuildView();
        }
    }

    bool ActivityPanelModel::RunCommand(StringView id, StringView /*argument*/) {
        const PoolString clear = EditorCommandIds::kActivityClear;
        if (id != clear.ToStringView()) {
            return false;
        }
        Service().Activity().Clear();
        RebuildView();
        return true;
    }

    void ActivityPanelModel::RebuildView() {
        const ControlActivity& activity = Service().Activity();
        const size_t count = activity.Size();
        _view.hasRows = count > 0;
        _view.rows.resize(count);
        for (size_t i = 0; i < count; ++i) {
            const ControlActivityEntry& entry = activity.At(count - 1 - i);
            ActivityRow& row = _view.rows[i];
            row.time.assign(entry.time);
            Array<char, kControlClientLabelSize> clientLabel;
            Assign(row.client, ControlClientLabel(entry.client.Get(), clientLabel));
            Assign(row.method, MethodText(entry));
            row.params = entry.text;
            row.error = entry.error;
        }
        _builtVersion = activity.GetVersion();
    }

}  // namespace BECore
