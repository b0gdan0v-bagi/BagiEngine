#include "ControlCall.h"

#include <BECore/Control/ControlEnvelope.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Memory/ScratchArena.h>

namespace BECore {

    std::expected<void, ControlError> TryAppendOkResponse(String& out, const Json& id, WriteResultFn writeResult) {
        const size_t start = out.size();
        ScratchScope scratch;  // the envelope may hold arena members; this also runs on a posting thread (a request after Shutdown), and a scope ends where it began
        JsonWriter writer(out);
        ControlOkEnvelope envelope;
        envelope.id = JsonPayload::Borrowed(id);
        envelope.result = JsonPayload::Streamed(writeResult);
        envelope.WriteJson(writer);
        std::expected<void, ControlError> written;
        if (const ControlError* failure = writer.GetFailure()) {
            written = std::unexpected(*failure);
        } else if (!writer.IsComplete()) {
            written = std::unexpected(MakeControlError(ControlErrors::kInternalError, "the handler wrote a result that is not one JSON value"));
        }
        if (!written) {
            out.resize(start);
        }
        return written;
    }

    bool AppendOkResponse(String& out, const Json& id, FunctionRef<void(JsonWriter&)> writeResult) {
        return TryAppendOkResponse(out, id,
                                   [&writeResult](JsonWriter& writer) -> std::expected<void, ControlError> {
                                       writeResult(writer);
                                       return {};
                                   })
            .has_value();
    }

    void AppendErrorResponse(String& out, const Json& id, const ControlError& error) {
        ScratchScope scratch;  // as in TryAppendOkResponse
        JsonWriter writer(out);
        ControlErrorEnvelope envelope;
        envelope.error.code = error.code;
        envelope.error.message = error.message;
        envelope.id = JsonPayload::Borrowed(id);
        envelope.WriteJson(writer);
    }

    String MakeOkResponse(const Json& id, Json result) {
        String text;
        const bool written = AppendOkResponse(text, id, [&result](JsonWriter& writer) { WriteJson(writer, result); });
        EXPECT(written, "MakeOkResponse: the writer of a Json value wrote no complete value");
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
        _service->SendOk(_reply, _id, [&result](JsonWriter& writer) -> std::expected<void, ControlError> {
            WriteJson(writer, result);
            return {};
        });
    }

    void ControlCall::Reply(FunctionRef<void(JsonWriter&)> writeResult) {
        TryReply([&writeResult](JsonWriter& writer) -> std::expected<void, ControlError> {
            writeResult(writer);
            return {};
        });
    }

    void ControlCall::TryReply(WriteResultFn writeResult) {
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
        const std::expected<void, ControlError> written = writeResult(writer);
        if (!written) {
            _captured = std::unexpected(written.error());
            return;
        }
        if (const ControlError* failure = writer.GetFailure()) {
            _captured = std::unexpected(*failure);
            return;
        }
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
