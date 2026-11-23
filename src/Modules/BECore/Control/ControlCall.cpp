#include "ControlCall.h"

#include <BECore/Control/ControlEnvelope.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Memory/ScratchArena.h>

namespace BECore {

    std::expected<void, ControlError> TryAppendOkResponse(String& out, StringView id, WriteResultFn writeResult) {
        const size_t start = out.size();
        ScratchScope scratch;  // the envelope may hold arena members; this also runs on a posting thread (a request after Shutdown), and a scope ends where it began
        JsonWriter writer(out);
        ControlOkEnvelope envelope;
        envelope.id = JsonPayload::Text(id);
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

    bool AppendOkResponse(String& out, StringView id, FunctionRef<void(JsonWriter&)> writeResult) {
        return TryAppendOkResponse(out, id,
                                   [&writeResult](JsonWriter& writer) -> std::expected<void, ControlError> {
                                       writeResult(writer);
                                       return {};
                                   })
            .has_value();
    }

    void AppendErrorResponse(String& out, StringView id, const ControlError& error) {
        ScratchScope scratch;  // as in TryAppendOkResponse
        JsonWriter writer(out);
        ControlErrorEnvelope envelope;
        envelope.error.code = TextView(error.code);
        envelope.error.message = TextView(error.message);
        envelope.id = JsonPayload::Text(id);
        envelope.WriteJson(writer);
    }

    String MakeOkResponse(StringView id, Json result) {
        String text;
        const bool written = AppendOkResponse(text, id, [&result](JsonWriter& writer) { WriteJson(writer, result); });
        EXPECT(written, "MakeOkResponse: the writer of a Json value wrote no complete value");
        return text;
    }

    String MakeErrorResponse(StringView id, const ControlError& error) {
        String text;
        AppendErrorResponse(text, id, error);
        return text;
    }

    JsonView EmptyParamsView() {
        static const JsonTape tape = [] {
            JsonTape made;
            const auto parsed = made.Parse("{}");
            EXPECT(parsed.has_value(), "ControlCall: the empty params did not tokenize");
            return made;
        }();
        return JsonView(tape, 0);
    }

    ControlCall::ControlCall(PassKey<ControlService>, ControlService& service, String method, RequestBuffersPtr buffers, StringView id, JsonView params, ControlReplyFn reply)
        : _service(&service), _buffers(std::move(buffers)), _method(std::move(method)), _id(id), _params(params), _reply(std::move(reply)) {}

    ControlCall::ControlCall(PassKey<ControlService>, String method, JsonView params) : _method(std::move(method)), _params(params), _capturing(true) {}

    const Json& ControlCall::ParamsJson() const {
        if (!_paramsJson) {
            auto parsed = ParseJson(_params.Raw());
            _paramsJson = parsed ? std::move(*parsed) : Json::object();  // the DOM refuses a number past a double that the tape took
        }
        return *_paramsJson;
    }

    void ControlCall::Reply(Json result) {
        EXPECT(!_answered, "ControlCall: the call is already answered");
        if (_answered) {
            return;
        }
        _answered = true;
        if (_capturing) {
            _captured = ControlAnswer(DumpJson(result));
            return;
        }
        _service->SendOk(_reply, _id, [&result](JsonWriter& writer) -> std::expected<void, ControlError> {
            WriteJson(writer, result);
            return {};
        });
    }

    void ControlCall::ReplyEmpty() {
        Reply([](JsonWriter& writer) {
            writer.BeginObject();
            writer.EndObject();
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

        // A nested call keeps the text the handler wrote as its answer
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
        if (writer.IsComplete()) {
            _captured = ControlAnswer(std::move(text));
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
