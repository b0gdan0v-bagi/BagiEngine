#include "ControlCall.h"

#include <BECore/Control/ControlService.h>

namespace BECore {

    void AppendOkResponse(String& out, const Json& id, FunctionRef<void(JsonWriter&)> writeResult) {
        JsonWriter writer(out);
        writer.BeginObject();
        writer.Key("id");
        WriteJson(writer, id);
        writer.Member("ok", true);
        writer.Key("result");
        writeResult(writer);
        writer.EndObject();
    }

    void AppendErrorResponse(String& out, const Json& id, const ControlError& error) {
        JsonWriter writer(out);
        writer.BeginObject();
        writer.Key("error");
        writer.BeginObject();
        writer.Member("code", StringView(error.code));
        writer.Member("message", StringView(error.message));
        writer.EndObject();
        writer.Key("id");
        WriteJson(writer, id);
        writer.Member("ok", false);
        writer.EndObject();
    }

    String MakeOkResponse(const Json& id, Json result) {
        String text;
        AppendOkResponse(text, id, [&result](JsonWriter& writer) { WriteJson(writer, result); });
        return text;
    }

    String MakeErrorResponse(const Json& id, const ControlError& error) {
        String text;
        AppendErrorResponse(text, id, error);
        return text;
    }

    ControlCall::ControlCall(PassKey<ControlService>, ControlService& service, String method, Json id, Json params, ControlReplyFn reply)
        : _service(&service), _method(std::move(method)), _id(std::move(id)), _params(std::move(params)), _reply(std::move(reply)) {}

    ControlCall::ControlCall(PassKey<ControlService>, String method, Json params) : _method(std::move(method)), _params(std::move(params)), _capturing(true) {}

    void ControlCall::Reply(Json result) {
        EXPECT(!_answered, "ControlCall: the call is already answered");
        if (_answered) {
            return;
        }
        _answered = true;
        if (_capturing) {
            _captured = ControlAnswer(std::move(result));
            return;
        }
        _service->SendOk(_reply, _id, [&result](JsonWriter& writer) { WriteJson(writer, result); });
    }

    void ControlCall::Reply(FunctionRef<void(JsonWriter&)> writeResult) {
        EXPECT(!_answered, "ControlCall: the call is already answered");
        if (_answered) {
            return;
        }
        _answered = true;
        if (!_capturing) {
            _service->SendOk(_reply, _id, writeResult);
            return;
        }

        // A nested call keeps its answer as a value, so the written text is read back; the value is the writer's own output, not a client's
        String text;
        JsonWriter writer(text);
        writeResult(writer);
        auto parsed = ParseJson(text);
        if (writer.IsComplete() && parsed) {
            _captured = ControlAnswer(std::move(*parsed));
        } else {
            _captured = std::unexpected(MakeControlError(ControlErrors::kInternalError, "the handler wrote a result that is not one JSON value"));
        }
    }

    void ControlCall::Fail(StringView code, StringView message) {
        Fail(MakeControlError(code, String(message)));
    }

    void ControlCall::Fail(const ControlError& error) {
        EXPECT(!_answered, "ControlCall: the call is already answered");
        if (_answered) {
            return;
        }
        _answered = true;
        if (_capturing) {
            _captured = std::unexpected(error);
            return;
        }
        _service->SendError(_reply, _id, error);
    }

    void ControlCall::DeferFrames(uint32_t frames, DoneFn done) {
        DeferFrames(frames, kDefaultStepSeconds, std::move(done));
    }

    void ControlCall::DeferFrames(uint32_t frames, double secondsPerFrame, DoneFn done) {
        EXPECT(!_answered, "ControlCall: the call is already answered");
        if (_answered) {
            return;
        }
        _deferFrames = std::max(frames, 1u);
        _deferSeconds = secondsPerFrame > 0.0 ? secondsPerFrame : 0.0;  // not a number counts as zero too
        _done = std::move(done);
    }

}  // namespace BECore
