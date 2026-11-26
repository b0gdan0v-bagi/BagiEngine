#include "ControlCall.h"

#include <BECore/Control/ControlEnvelope.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Memory/ScratchArena.h>

namespace BECore {

    namespace {

        /// The answer of a nested call: the text @p writeResult wrote, or the error that answers in its place
        ControlAnswer CaptureAnswer(WriteResultFn writeResult) {
            String text;
            JsonWriter writer(text);
            if (const std::expected<void, ControlError> written = writeResult(writer); !written) {
                return std::unexpected(written.error());
            }
            if (const ControlError* failure = writer.GetFailure()) {
                return std::unexpected(*failure);
            }
            if (!writer.IsComplete()) {
                return std::unexpected(MakeControlError(ControlErrors::kInternalError, "the handler wrote a result that is not one JSON value"));
            }
            return text;
        }

    }  // namespace

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

    ControlCall::ControlCall(PassKey<ControlService>, ControlService& service, String method, RequestBuffersPtr buffers, StringView id, JsonView params, ControlReplyFn reply,
                             IntrusivePtr<ControlClient> client)
        : _service(&service), _buffers(std::move(buffers)), _method(std::move(method)), _id(id), _params(params), _reply(std::move(reply)), _client(std::move(client)) {}

    ControlCall::ControlCall(PassKey<ControlService>, ControlService& service, String method, JsonView params, IntrusivePtr<ControlClient> client)
        : _service(&service), _method(std::move(method)), _params(params), _client(std::move(client)), _capturing(true) {}

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
            _service->SendOk(*this, writeResult);
            return;
        }
        _captured = CaptureAnswer(writeResult);
        _service->RecordCall(*this, _captured->has_value() ? StringView() : StringView(_captured->error().code));
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
        _service->RecordCall(*this, error.code);
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
