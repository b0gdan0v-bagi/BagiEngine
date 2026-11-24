#include "JsonPayload.h"

#include <BECore/Control/JsonWriter.h>

namespace BECore {

    JsonPayload JsonPayload::Text(StringView ready) {
        return JsonPayload(Form{ready});
    }

    void JsonPayload::WriteJson(JsonWriter& writer) const {
        struct Visitor {
            JsonWriter& writer;

            void operator()(std::monostate /*empty*/) const {
                EXPECT(false, "JsonPayload: an empty payload is written (a member that may be missing is an Optional)");
                writer.Null();
            }
            void operator()(StringView text) const {
                writer.Raw(text);
            }
            void operator()(const WriteResultFn& write) const {
                if (std::expected<void, ControlError> written = write(writer); !written) {
                    writer.Fail(std::move(written.error()));
                }
            }
        };
        if (writer.GetFailure() == nullptr) {  // an earlier payload failed: the text is thrown away, so do not ask a game to write more of it
            std::visit(Visitor{writer}, _form);
        }
    }

}  // namespace BECore
