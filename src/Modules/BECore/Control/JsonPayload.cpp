#include "JsonPayload.h"

#include <BECore/Control/Json.h>

namespace BECore {

    void JsonPayload::JsonDeleter::operator()(Json* value) const {
        delete value;
    }

    JsonPayload JsonPayload::Owned(Json value) {
        // The owner's deleter is ours, so the pointer is made here rather than by MakeUnique (which carries `default_delete`)
        return JsonPayload(Form{UniquePtr<Json, JsonDeleter>(new Json(std::move(value)))});
    }

    JsonPayload JsonPayload::Borrowed(const Json& value) {
        return JsonPayload(Form{&value});
    }

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
            void operator()(const UniquePtr<Json, JsonDeleter>& owned) const {
                ::BECore::WriteJson(writer, *owned);
            }
            void operator()(const Json* borrowed) const {
                ::BECore::WriteJson(writer, *borrowed);
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
