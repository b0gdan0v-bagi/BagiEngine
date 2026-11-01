#include "ControlCall.h"

namespace BECore {

    eastl::string MakeOkResponse(const Json& id, Json result) {
        Json response = Json::object();
        response["id"] = id;
        response["ok"] = true;
        response["result"] = std::move(result);
        return DumpJson(response);
    }

    eastl::string MakeErrorResponse(const Json& id, const ControlError& error) {
        Json details = Json::object();
        details["code"] = MakeJsonString(error.code);
        details["message"] = MakeJsonString(error.message);

        Json response = Json::object();
        response["id"] = id;
        response["ok"] = false;
        response["error"] = std::move(details);
        return DumpJson(response);
    }

    ControlCall::ControlCall(PassKey<ControlService>, eastl::string method, Json id, Json params, ControlReplyFn reply)
        : _method(std::move(method)), _id(std::move(id)), _params(std::move(params)), _reply(std::move(reply)) {}

    void ControlCall::Reply(Json result) {
        EXPECT(!_answered, "ControlCall: the call is already answered");
        if (_answered) {
            return;
        }
        _answered = true;
        _reply(MakeOkResponse(_id, std::move(result)));
    }

    void ControlCall::Fail(eastl::string_view code, eastl::string_view message) {
        Fail(MakeControlError(code, eastl::string(message)));
    }

    void ControlCall::Fail(const ControlError& error) {
        EXPECT(!_answered, "ControlCall: the call is already answered");
        if (_answered) {
            return;
        }
        _answered = true;
        _reply(MakeErrorResponse(_id, error));
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
