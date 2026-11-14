#include "FuzzTargets.h"

#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <Games/GameControl.h>

namespace BECore::Fuzz {

    namespace {

        /// The first byte of an input that stands for a request over the size limit (no request starts with it: it is not JSON)
        constexpr uint8_t kOversizeMarker = 0xFF;
        /// Frames the target lets a deferred call (`step`) wait for its answer; the rest is answered by Shutdown()
        constexpr int kFramesToWait = 4;

        /// The id the answer has to carry: the request's own when the request is an object with an integer or a string id, otherwise null (a request nested too deeply is
        /// refused before anything is read from it, the id included)
        Json ExpectedId(eastl::string_view text) {
            if (IsJsonNestedDeeperThan(text, ControlService::kMaxRequestDepth)) {
                return Json();
            }
            const auto request = ParseJson(text);
            if (!request.has_value() || !request->is_object()) {
                return Json();
            }
            const Json* id = FindJsonMember(*request, "id");
            return id != nullptr && (id->is_string() || id->is_number_integer()) ? *id : Json();
        }

        void CheckReply(const eastl::string& reply, const Json& expectedId) {
            const auto parsed = ParseJson(reply);
            FUZZ_CHECK(parsed.has_value() && parsed->is_object());
            const Json* id = FindJsonMember(*parsed, "id");
            FUZZ_CHECK(id != nullptr && *id == expectedId);

            const auto ok = GetJsonBool(*parsed, "ok");
            FUZZ_CHECK(ok.has_value());
            if (*ok) {
                FUZZ_CHECK(FindJsonMember(*parsed, "result") != nullptr);
                FUZZ_CHECK(FindJsonMember(*parsed, "error") == nullptr);
                return;
            }
            const Json* error = FindJsonMember(*parsed, "error");
            FUZZ_CHECK(error != nullptr && error->is_object());
            const auto code = GetJsonString(*error, "code");
            const auto message = GetJsonString(*error, "message");
            FUZZ_CHECK(code.has_value() && !code->empty());
            FUZZ_CHECK(message.has_value());
            FUZZ_CHECK(FindJsonMember(*parsed, "result") == nullptr);
        }

    }  // namespace

    void FuzzControlRequest(const uint8_t* data, size_t size) {
        if (size == 0) {
            return;
        }
        const bool oversize = data[0] == kOversizeMarker;
        eastl::string text(reinterpret_cast<const char*>(data), size);
        if (oversize) {
            text.resize(ControlService::kMaxRequestBytes + 1, ' ');
        }

        ControlService service;
        RegisterControlBuiltins(service);
        RegisterGameControl(service);

        eastl::vector<eastl::string> replies;
        service.Post(text, [&replies](eastl::string reply) { replies.push_back(std::move(reply)); });
        service.Drain();
        for (int frame = 0; frame < kFramesToWait && replies.empty(); ++frame) {
            service.EndFrame();
        }
        service.Shutdown();  // answers what is still deferred

        FUZZ_CHECK(replies.size() == 1);
        CheckReply(replies[0], oversize ? Json() : ExpectedId(text));
        if (oversize) {
            const auto parsed = ParseJson(replies[0]);
            const Json* error = FindJsonMember(*parsed, "error");
            FUZZ_CHECK(error != nullptr && GetJsonString(*error, "code") == Optional<eastl::string>(eastl::string(ControlErrors::kRequestTooLarge)));
        }
    }

}  // namespace BECore::Fuzz
