#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/JsonTape.h>
#include <BECore/Control/JsonView.h>
#include <Fuzz/FuzzProbeAccess.h>
#include <Fuzz/FuzzTargets.h>
#include <Games/GameControl.h>

namespace BECore::Fuzz {

    namespace {

        /// The first byte of an input that stands for a request over the size limit (no request starts with it: it is not JSON)
        constexpr uint8_t kOversizeMarker = 0xFF;
        /// Frames the target lets a deferred call (`step`) wait for its answer; the rest is answered by Shutdown()
        constexpr int kFramesToWait = 4;

        constexpr StringView kNullText = "null";

        /// The id the answer has to carry, as the text the request wrote it: the request's own when the request is an object with an integer or a string
        /// id, otherwise `null` (a request nested too deeply is refused before anything is read from it, the id included)
        String ExpectedId(StringView text) {
            if (IsJsonNestedDeeperThan(text, ControlService::kMaxRequestDepth)) {
                return String(kNullText);
            }
            JsonTape tape;
            if (!tape.Parse(text).has_value() || !JsonView(tape, 0).IsObject()) {
                return String(kNullText);
            }
            const Optional<JsonView> id = FindParam(JsonView(tape, 0), "id");
            const bool usable = id && (id->IsString() || (id->IsInteger() && (id->AsInt64() || id->AsUint64())));
            return usable ? String(id->Raw()) : String(kNullText);
        }

        void CheckReply(StringView reply, StringView expectedId) {
            JsonTape tape;
            FUZZ_CHECK(tape.Parse(reply).has_value());
            const JsonView parsed(tape, 0);
            FUZZ_CHECK(parsed.IsObject());
            const Optional<JsonView> id = FindParam(parsed, "id");
            FUZZ_CHECK(id.has_value() && id->Raw() == expectedId);

            const auto ok = GetParamBool(parsed, "ok");
            FUZZ_CHECK(ok.has_value());
            if (*ok) {
                FUZZ_CHECK(FindParam(parsed, "result").has_value());
                FUZZ_CHECK(!FindParam(parsed, "error").has_value());
                return;
            }
            const Optional<JsonView> error = FindParam(parsed, "error");
            FUZZ_CHECK(error && error->IsObject());
            const auto code = GetParamString(*error, "code");
            const auto message = GetParamString(*error, "message");
            FUZZ_CHECK(code.has_value() && !code->empty());
            FUZZ_CHECK(message.has_value());
            FUZZ_CHECK(!FindParam(parsed, "result").has_value());
        }

    }  // namespace

    void FuzzControlRequest(const uint8_t* data, size_t size) {
        if (size == 0) {
            return;
        }
        const bool oversize = data[0] == kOversizeMarker;
        String text(reinterpret_cast<const char*>(data), size);
        if (oversize) {
            text.resize(ControlService::kMaxRequestBytes + 1, ' ');
        }

        ControlService service;
        RegisterControlBuiltins(service);
        RegisterGameControl(service);

        // Real data for `get_property`, `set_property` and `call_method`; neither root allows interning, so a request cannot grow the pool
        ProbeState state = MakeFilledProbe();
        ProbeCalls calls;
        service.Roots().Register("state", ControlRoot{[&state] { return MakeProbeRef(state); }});
        service.Roots().Register("probe", ControlRoot{[&calls] { return MakeProbeRef(calls); }});

        Vector<String> replies;
        service.Post(text, [&replies](String reply) { replies.push_back(std::move(reply)); });
        service.Drain();
        for (int frame = 0; frame < kFramesToWait && replies.empty(); ++frame) {
            service.EndFrame();
        }
        service.Shutdown();  // answers what is still deferred

        FUZZ_CHECK(replies.size() == 1);
        CheckReply(replies[0], oversize ? kNullText : StringView(ExpectedId(text)));
        if (oversize) {
            JsonTape tape;
            FUZZ_CHECK(tape.Parse(replies[0]).has_value());
            const Optional<JsonView> error = FindParam(JsonView(tape, 0), "error");
            FUZZ_CHECK(error.has_value() && GetParamString(*error, "code") == Optional<String>(String(ControlErrors::kRequestTooLarge)));
        }
    }

}  // namespace BECore::Fuzz
