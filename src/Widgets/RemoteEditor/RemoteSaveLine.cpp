#include "RemoteSaveLine.h"

namespace BECore {

    namespace {

        String Sentence(const RemoteSaveStatus& status) {
            switch (status.state) {
                case RemoteSaveState::None:
                    break;
                case RemoteSaveState::Pending:
                    return String("Saving in the attached program...");
                case RemoteSaveState::Saved: {
                    String text = Format("The program saved the scene to {}", status.file);
                    if (!status.persistent) {
                        text += ": kept only in the page's memory, lost on reload";
                    }
                    if (status.notes > 0) {
                        text += Format(" (notes: {}, see its log)", status.notes);
                    }
                    return text;
                }
                case RemoteSaveState::Refused:
                    return status.error.code.empty() ? Format("Not saved: {}", status.error.message) : Format("Not saved ({}): {}", status.error.code, status.error.message);
                case RemoteSaveState::Unconfirmed:
                    return String("The link ended before the program answered: the scene may or may not have been saved");
            }
            return {};
        }

    }  // namespace

    bool RemoteSaveLine::Update(const RemoteSaveStatus& status) {
        if (_shown.state == status.state && _shown.persistent == status.persistent && _shown.notes == status.notes && _shown.file == status.file && _shown.error.code == status.error.code &&
            _shown.error.message == status.error.message) {
            return false;
        }
        _shown = status;
        _text = Sentence(_shown);
        return true;
    }

}  // namespace BECore
