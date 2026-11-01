#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <Common/EngineEnvironment.h>
#include <Events/ApplicationEvents.h>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        /// Counts the QuitEvents that the "quit" method emits
        class QuitCounter : public SubscriptionHolder {
        public:
            using SubscriptionHolder::Subscribe;

            int count = 0;

            void OnQuit(const ApplicationEvents::QuitEvent&) {
                ++count;
            }
        };

        eastl::string_view View(std::string_view text) {
            return eastl::string_view(text.data(), text.size());
        }

        std::string ErrorCode(const Json& response) {
            const Json* error = FindJsonMember(response, "error");
            if (error == nullptr) {
                return {};
            }
            const auto code = GetJsonString(*error, "code");
            return code ? std::string(code->c_str(), code->size()) : std::string();
        }

        std::string ErrorMessage(const Json& response) {
            const Json* error = FindJsonMember(response, "error");
            if (error == nullptr) {
                return {};
            }
            const auto message = GetJsonString(*error, "message");
            return message ? std::string(message->c_str(), message->size()) : std::string();
        }

        bool IsOk(const Json& response) {
            return GetJsonBool(response, "ok").value_or(false);
        }

        int64_t IdOf(const Json& response) {
            return GetJsonInt(response, "id").value_or(-1);
        }

        /// A request text built through Json, so that the test data is always valid
        eastl::string MakeRequest(int64_t id, std::string_view method, const Json& params = Json()) {
            Json request = Json::object();
            request["id"] = id;
            request["method"] = std::string(method);
            if (!params.is_null()) {
                request["params"] = params;
            }
            return DumpJson(request);
        }

        class ControlServiceTest : public ::testing::Test {
        protected:
            void TearDown() override {
                CoreManager::GetTime().Reset();
            }

            ControlReplyFn Collect() {
                return [this](eastl::string text) {
                    auto parsed = ParseJson(text);
                    responses.push_back(parsed ? std::move(*parsed) : Json());
                };
            }

            void Post(const eastl::string& text) {
                service.Post(text, Collect());
            }

            /// Registers a method that records its name in `order` and answers with an empty result
            void RegisterRecording(std::string_view method, bool bypassBarrier = false) {
                const std::string name(method);
                service.RegisterHandler(
                    View(method),
                    [this, name](ControlCall& call) {
                        order.push_back(name);
                        call.Reply(Json::object());
                    },
                    bypassBarrier);
            }

            /// Registers a method that defers its answer by `frames` frames, then records `name-done`
            void RegisterDeferring(std::string_view method, uint32_t frames, bool bypassBarrier = false) {
                const std::string name(method);
                service.RegisterHandler(
                    View(method),
                    [this, name, frames](ControlCall& call) {
                        order.push_back(name);
                        call.DeferFrames(frames, [this, name](ControlCall& done) {
                            order.push_back(name + "-done");
                            done.Reply(Json::object());
                        });
                    },
                    bypassBarrier);
            }

            ControlService service;
            std::vector<Json> responses;
            std::vector<std::string> order;
        };

        static_assert(ControlService::kMaxRequestBytes >= 1024);
        static_assert(!std::is_copy_constructible_v<ControlService>);
        static_assert(!std::is_copy_constructible_v<ControlCall>);

    }  // namespace

    TEST_F(ControlServiceTest, CompileTimeChecks) {
        SUCCEED();
    }

    TEST_F(ControlServiceTest, AnswersOnlyWhenDrained) {
        RegisterRecording("hello");

        Post(MakeRequest(1, "hello"));
        EXPECT_TRUE(responses.empty());

        service.Drain();
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(IsOk(responses[0]));
        EXPECT_EQ(IdOf(responses[0]), 1);
    }

    TEST_F(ControlServiceTest, EchoesIntegerAndStringIds) {
        service.RegisterHandler("echo", [](ControlCall& call) { call.Reply(call.Params()); });

        Json params = Json::object();
        params["value"] = 42;
        Post(MakeRequest(7, "echo", params));

        Json stringId = Json::object();
        stringId["id"] = "req-1";
        stringId["method"] = "echo";
        Post(DumpJson(stringId));

        service.Drain();
        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(IdOf(responses[0]), 7);
        const Json* result = FindJsonMember(responses[0], "result");
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(GetJsonInt(*result, "value").value_or(0), 42);
        EXPECT_EQ(GetJsonString(responses[1], "id").value_or("").c_str(), std::string("req-1"));
    }

    TEST_F(ControlServiceTest, UnknownMethodIsAnError) {
        Post(MakeRequest(3, "no_such_method"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_FALSE(IsOk(responses[0]));
        EXPECT_EQ(ErrorCode(responses[0]), "unknown_method");
        EXPECT_EQ(IdOf(responses[0]), 3);
    }

    TEST_F(ControlServiceTest, LongMethodNameIsCutInTheMessage) {
        Post(MakeRequest(1, std::string(5000, 'm')));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(ErrorCode(responses[0]), "unknown_method");
        EXPECT_LT(ErrorMessage(responses[0]).size(), 200u);
    }

    TEST_F(ControlServiceTest, InvalidRequestsGetAnErrorWithTheRightCode) {
        struct Case {
            const char* text;
            const char* code;
            bool hasId;  // the response can echo the id of the request
        };
        const Case cases[] = {
            {"", "parse_error", false},
            {"{", "parse_error", false},
            {"not json", "parse_error", false},
            {"[1,2]", "invalid_request", false},
            {"5", "invalid_request", false},
            {R"({"method":"ping"})", "invalid_request", false},
            {R"({"id":1.5,"method":"ping"})", "invalid_request", false},
            {R"({"id":{},"method":"ping"})", "invalid_request", false},
            {R"({"id":1})", "invalid_request", true},
            {R"({"id":1,"method":5})", "invalid_request", true},
            {R"({"id":1,"method":""})", "invalid_request", true},
            {R"({"id":1,"method":"ping","params":[1]})", "invalid_params", true},
            {R"({"id":1,"method":"ping","params":"x"})", "invalid_params", true},
        };
        RegisterRecording("ping");

        for (const Case& testCase : cases) {
            responses.clear();
            service.Post(eastl::string(testCase.text), Collect());
            service.Drain();

            ASSERT_EQ(responses.size(), 1u) << testCase.text;
            EXPECT_FALSE(IsOk(responses[0])) << testCase.text;
            EXPECT_EQ(ErrorCode(responses[0]), testCase.code) << testCase.text;
            const Json* id = FindJsonMember(responses[0], "id");
            ASSERT_NE(id, nullptr) << testCase.text;
            EXPECT_EQ(id->is_number_integer(), testCase.hasId) << testCase.text;
            EXPECT_EQ(id->is_null(), !testCase.hasId) << testCase.text;
        }
        EXPECT_TRUE(order.empty());
    }

    TEST_F(ControlServiceTest, ParamsDefaultToAnEmptyObject) {
        int seen = 0;
        service.RegisterHandler("params", [&](ControlCall& call) {
            EXPECT_TRUE(call.Params().is_object());
            EXPECT_TRUE(call.Params().empty());
            ++seen;
            call.Reply(Json::object());
        });

        Post(MakeRequest(1, "params"));
        service.Post(R"({"id":2,"method":"params","params":null})", Collect());
        service.Drain();

        EXPECT_EQ(seen, 2);
        EXPECT_EQ(responses.size(), 2u);
    }

    TEST_F(ControlServiceTest, TooLargeRequestIsRejected) {
        const eastl::string tooLarge(ControlService::kMaxRequestBytes + 1, ' ');
        service.Post(tooLarge, Collect());

        const eastl::string atLimit(ControlService::kMaxRequestBytes, ' ');
        service.Post(atLimit, Collect());

        service.Drain();
        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(ErrorCode(responses[0]), "request_too_large");
        EXPECT_EQ(ErrorCode(responses[1]), "parse_error");  // within the limit, so it is parsed (and is not JSON)
    }

    TEST_F(ControlServiceTest, GarbageGetsAnAnswerAndNeverAbortsTheProcess) {
        RegisterRecording("ping");
        const std::string deepArray(200000, '[');
        std::string allBytes;
        for (int byte = 0; byte < 256; ++byte) {
            allBytes.push_back(static_cast<char>(byte));
        }
        const std::vector<std::string> garbage = {
            allBytes,
            "\xff\xfe\xfd",
            deepArray,
            deepArray + std::string(200000, ']'),
            R"({"id":1,"method":"ping","params":)" + deepArray,
            std::string(100000, '"'),
            R"({"id":1,"method":"ping","params":{"a":[[[[[[[[]]]]]]]]}})",
            R"({"id":18446744073709551615,"method":"ping"})",
            R"({"id":-9223372036854775808,"method":"ping"})",
            R"({"id":1e999,"method":"ping"})",
            "{\"id\":1,\"method\":\"p\xff\xfe\"}",
            R"({"id":1,"method":"ping","method":"ping"})",
        };

        for (const std::string& text : garbage) {
            service.Post(View(text), Collect());
        }
        service.Drain();

        EXPECT_EQ(responses.size(), garbage.size());
        for (const Json& response : responses) {
            EXPECT_TRUE(response.is_object());
        }
    }

    TEST_F(ControlServiceTest, HandlerThatDoesNotAnswerGetsAnInternalError) {
        service.RegisterHandler("silent", [](ControlCall&) {});

        Post(MakeRequest(1, "silent"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(ErrorCode(responses[0]), "internal_error");
    }

    TEST_F(ControlServiceTest, HandlerCanFail) {
        service.RegisterHandler("fail", [](ControlCall& call) { call.Fail("illegal_action", "that move is not legal"); });

        Post(MakeRequest(5, "fail"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_FALSE(IsOk(responses[0]));
        EXPECT_EQ(ErrorCode(responses[0]), "illegal_action");
        EXPECT_EQ(ErrorMessage(responses[0]), "that move is not legal");
        EXPECT_EQ(IdOf(responses[0]), 5);
    }

    TEST_F(ControlServiceTest, ErrorMessageWithInvalidUtf8DoesNotAbort) {
        service.RegisterHandler("bad", [](ControlCall& call) { call.Fail("bad_text", "broken \xff text"); });

        Post(MakeRequest(1, "bad"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(ErrorCode(responses[0]), "bad_text");
    }

    TEST_F(ControlServiceTest, RunsRequestsInPostOrder) {
        RegisterRecording("a");
        RegisterRecording("b");
        RegisterRecording("c");

        Post(MakeRequest(1, "b"));
        Post(MakeRequest(2, "a"));
        Post(MakeRequest(3, "c"));
        Post(MakeRequest(4, "a"));
        service.Drain();

        EXPECT_EQ(order, (std::vector<std::string>{"b", "a", "c", "a"}));
        ASSERT_EQ(responses.size(), 4u);
        for (size_t i = 0; i < responses.size(); ++i) {
            EXPECT_EQ(IdOf(responses[i]), static_cast<int64_t>(i + 1));
        }
    }

    TEST_F(ControlServiceTest, KeepsTheOrderOfEachPostingThread) {
        constexpr int kThreads = 4;
        constexpr int kPerThread = 200;

        std::vector<std::vector<int64_t>> seen(kThreads);
        service.RegisterHandler("seq", [&](ControlCall& call) {
            const auto thread = GetJsonInt(call.Params(), "thread");
            const auto seq = GetJsonInt(call.Params(), "seq");
            ASSERT_TRUE(thread && seq);
            seen[static_cast<size_t>(*thread)].push_back(*seq);
            call.Reply(Json::object());
        });

        std::atomic<int> replies = 0;
        std::vector<std::thread> threads;
        for (int thread = 0; thread < kThreads; ++thread) {
            threads.emplace_back([&, thread] {
                for (int seq = 0; seq < kPerThread; ++seq) {
                    Json params = Json::object();
                    params["thread"] = thread;
                    params["seq"] = seq;
                    service.Post(MakeRequest(seq, "seq", params), [&replies](eastl::string) { ++replies; });
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }

        service.Drain();

        EXPECT_EQ(replies.load(), kThreads * kPerThread);
        for (int thread = 0; thread < kThreads; ++thread) {
            const std::vector<int64_t>& sequence = seen[static_cast<size_t>(thread)];
            ASSERT_EQ(sequence.size(), static_cast<size_t>(kPerThread)) << "thread " << thread;
            EXPECT_TRUE(std::is_sorted(sequence.begin(), sequence.end())) << "thread " << thread;
        }
    }

    TEST_F(ControlServiceTest, DeferredCallHoldsBackTheRequestsBehindIt) {
        RegisterDeferring("slow", 2);
        RegisterRecording("fast");

        Post(MakeRequest(1, "slow"));
        Post(MakeRequest(2, "fast"));
        service.Drain();
        EXPECT_EQ(order, (std::vector<std::string>{"slow"}));
        EXPECT_TRUE(responses.empty());

        service.EndFrame();  // one frame left
        service.Drain();
        EXPECT_EQ(order, (std::vector<std::string>{"slow"}));
        EXPECT_TRUE(responses.empty());

        service.EndFrame();  // done
        EXPECT_EQ(order, (std::vector<std::string>{"slow", "slow-done"}));
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(IdOf(responses[0]), 1);

        service.Drain();
        EXPECT_EQ(order, (std::vector<std::string>{"slow", "slow-done", "fast"}));
        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(IdOf(responses[1]), 2);
    }

    TEST_F(ControlServiceTest, ZeroFramesCountsAsOne) {
        RegisterDeferring("now", 0);

        Post(MakeRequest(1, "now"));
        service.Drain();
        EXPECT_TRUE(responses.empty());

        service.EndFrame();
        EXPECT_EQ(responses.size(), 1u);
    }

    TEST_F(ControlServiceTest, CountsDownOnceForEveryEndFrame) {
        RegisterDeferring("wait", 5);

        Post(MakeRequest(1, "wait"));
        service.Drain();
        for (int frame = 1; frame < 5; ++frame) {
            service.EndFrame();
            EXPECT_TRUE(responses.empty()) << "frame " << frame;
        }
        service.EndFrame();
        EXPECT_EQ(responses.size(), 1u);
    }

    TEST_F(ControlServiceTest, ADeferredCallCanDeferAgain) {
        service.RegisterHandler("twice", [this](ControlCall& call) {
            call.DeferFrames(1, [this](ControlCall& first) {
                order.push_back("first");
                first.DeferFrames(1, [this](ControlCall& second) {
                    order.push_back("second");
                    second.Reply(Json::object());
                });
            });
        });

        Post(MakeRequest(1, "twice"));
        service.Drain();
        service.EndFrame();
        EXPECT_EQ(order, (std::vector<std::string>{"first"}));
        EXPECT_TRUE(responses.empty());

        service.EndFrame();
        EXPECT_EQ(order, (std::vector<std::string>{"first", "second"}));
        EXPECT_EQ(responses.size(), 1u);
    }

    TEST_F(ControlServiceTest, DoneThatDoesNotAnswerGetsAnInternalError) {
        service.RegisterHandler("lazy", [](ControlCall& call) { call.DeferFrames(1, [](ControlCall&) {}); });

        Post(MakeRequest(1, "lazy"));
        service.Drain();
        service.EndFrame();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(ErrorCode(responses[0]), "internal_error");
    }

    TEST_F(ControlServiceTest, BypassingMethodRunsWhileACallIsPending) {
        RegisterDeferring("hold", 3);
        RegisterRecording("normal");
        RegisterRecording("urgent", /*bypassBarrier=*/true);

        Post(MakeRequest(1, "hold"));
        Post(MakeRequest(2, "normal"));
        Post(MakeRequest(3, "urgent"));
        service.Drain();

        EXPECT_EQ(order, (std::vector<std::string>{"hold", "urgent"}));
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(IdOf(responses[0]), 3);

        service.EndFrame();
        service.EndFrame();
        service.EndFrame();
        service.Drain();

        EXPECT_EQ(order, (std::vector<std::string>{"hold", "urgent", "hold-done", "normal"}));
        ASSERT_EQ(responses.size(), 3u);
        EXPECT_EQ(IdOf(responses[1]), 1);
        EXPECT_EQ(IdOf(responses[2]), 2);
    }

    TEST_F(ControlServiceTest, BypassingMethodCannotDeferWhileACallIsPending) {
        RegisterDeferring("hold", 2);
        RegisterDeferring("urgent", 1, /*bypassBarrier=*/true);

        Post(MakeRequest(1, "hold"));
        Post(MakeRequest(2, "urgent"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(IdOf(responses[0]), 2);
        EXPECT_EQ(ErrorCode(responses[0]), "internal_error");

        service.EndFrame();
        service.EndFrame();
        ASSERT_EQ(responses.size(), 2u);  // the pending call was not disturbed
        EXPECT_EQ(IdOf(responses[1]), 1);
        EXPECT_TRUE(IsOk(responses[1]));
    }

    TEST_F(ControlServiceTest, InvalidRequestBehindADeferredCallKeepsItsPlace) {
        RegisterDeferring("hold", 1);

        Post(MakeRequest(1, "hold"));
        service.Post("not json", Collect());
        service.Drain();
        EXPECT_TRUE(responses.empty());

        service.EndFrame();
        service.Drain();
        ASSERT_EQ(responses.size(), 2u);
        EXPECT_TRUE(IsOk(responses[0]));
        EXPECT_EQ(ErrorCode(responses[1]), "parse_error");
    }

    TEST_F(ControlServiceTest, FrameEndHooksRunBeforeADeferredCallCompletes) {
        RegisterDeferring("hold", 1);
        service.AddFrameEndHook([this] { order.push_back("hook"); });

        Post(MakeRequest(1, "hold"));
        service.Drain();
        service.EndFrame();

        EXPECT_EQ(order, (std::vector<std::string>{"hold", "hook", "hold-done"}));
    }

    TEST_F(ControlServiceTest, HandlerMayRegisterHandlers) {
        service.RegisterHandler("register", [this](ControlCall& call) {
            for (int index = 0; index < 200; ++index) {  // enough to rehash the handler table
                service.RegisterHandler(View("extra" + std::to_string(index)), [](ControlCall& extra) { extra.Reply(Json::object()); });
            }
            call.Reply(Json::object());
        });

        Post(MakeRequest(1, "register"));
        Post(MakeRequest(2, "extra199"));
        service.Drain();

        ASSERT_EQ(responses.size(), 2u);
        EXPECT_TRUE(IsOk(responses[0]));
        EXPECT_TRUE(IsOk(responses[1]));
    }

    TEST_F(ControlServiceTest, RegisteringAMethodAgainReplacesIt) {
        service.RegisterHandler("m", [this](ControlCall& call) {
            order.push_back("old");
            call.Reply(Json::object());
        });
        service.RegisterHandler("m", [this](ControlCall& call) {
            order.push_back("new");
            call.Reply(Json::object());
        });

        Post(MakeRequest(1, "m"));
        service.Drain();

        EXPECT_EQ(order, (std::vector<std::string>{"new"}));
    }

    TEST_F(ControlServiceTest, ShutdownAnswersEverythingWithShuttingDown) {
        RegisterDeferring("hold", 5);
        RegisterRecording("queued");

        Post(MakeRequest(1, "hold"));
        Post(MakeRequest(2, "queued"));
        service.Drain();                 // "hold" is pending, "queued" waits in the inbox
        Post(MakeRequest(3, "queued"));  // still in the queue
        EXPECT_TRUE(responses.empty());

        service.Shutdown();

        ASSERT_EQ(responses.size(), 3u);
        for (const Json& response : responses) {
            EXPECT_FALSE(IsOk(response));
            EXPECT_EQ(ErrorCode(response), "shutting_down");
        }
        EXPECT_EQ(order, (std::vector<std::string>{"hold"}));
    }

    TEST_F(ControlServiceTest, RequestPostedAfterShutdownIsAnsweredAtOnce) {
        RegisterRecording("late");
        service.Shutdown();

        Post(MakeRequest(9, "late"));

        ASSERT_EQ(responses.size(), 1u);  // no Drain() needed
        EXPECT_EQ(ErrorCode(responses[0]), "shutting_down");
        EXPECT_EQ(IdOf(responses[0]), 9);
        EXPECT_TRUE(order.empty());
    }

    TEST_F(ControlServiceTest, PublishSendsAnEventToTheSink) {
        std::vector<std::string> pushed;
        service.SetEventSink([&](const eastl::string& text) { pushed.emplace_back(text.c_str(), text.size()); });

        Json data = Json::object();
        data["version"] = 3;
        service.Publish("state_changed", data);

        ASSERT_EQ(pushed.size(), 1u);
        const auto parsed = ParseJson(View(pushed[0]));
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(GetJsonString(*parsed, "event").value_or("").c_str(), std::string("state_changed"));
        const Json* eventData = FindJsonMember(*parsed, "data");
        ASSERT_NE(eventData, nullptr);
        EXPECT_EQ(GetJsonInt(*eventData, "version").value_or(0), 3);
    }

    TEST_F(ControlServiceTest, PublishWithoutASinkDoesNothing) {
        service.Publish("state_changed", Json::object());
        SUCCEED();
    }

    TEST_F(ControlServiceTest, PingReportsTheProtocolVersion) {
        RegisterControlBuiltins(service);

        Post(MakeRequest(1, "ping"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(IsOk(responses[0]));
        const Json* result = FindJsonMember(responses[0], "result");
        ASSERT_NE(result, nullptr);
        EXPECT_TRUE(GetJsonBool(*result, "pong").value_or(false));
        EXPECT_EQ(GetJsonInt(*result, "protocol").value_or(0), ControlService::kProtocolVersion);
    }

    TEST_F(ControlServiceTest, StatusReportsTheFrameClock) {
        RegisterControlBuiltins(service);
        CoreManager::GetTime().Reset();
        CoreManager::GetTime().Advance(0.125);
        CoreManager::GetTime().Advance(0.125);

        Post(MakeRequest(1, "status"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        const Json* result = FindJsonMember(responses[0], "result");
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(GetJsonInt(*result, "frame").value_or(0), 2);
        EXPECT_DOUBLE_EQ(GetJsonNumber(*result, "totalTime").value_or(0.0), 0.25);
        EXPECT_DOUBLE_EQ(GetJsonNumber(*result, "deltaTime").value_or(0.0), 0.125);
        EXPECT_GT(GetJsonNumber(*result, "fixedDeltaTime").value_or(0.0), 0.0);
    }

    TEST_F(ControlServiceTest, QuitEmitsTheQuitEventAfterAnswering) {
        RegisterControlBuiltins(service);
        QuitCounter counter;
        counter.Subscribe<ApplicationEvents::QuitEvent, &QuitCounter::OnQuit>(&counter);

        Post(MakeRequest(1, "quit"));
        service.Drain();

        EXPECT_EQ(counter.count, 1);
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(IsOk(responses[0]));
    }

    TEST_F(ControlServiceTest, QuitBypassesAPendingCall) {
        RegisterControlBuiltins(service);
        RegisterDeferring("long_step", 1000);
        QuitCounter counter;
        counter.Subscribe<ApplicationEvents::QuitEvent, &QuitCounter::OnQuit>(&counter);

        Post(MakeRequest(1, "long_step"));
        Post(MakeRequest(2, "quit"));
        service.Drain();

        EXPECT_EQ(counter.count, 1);
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(IdOf(responses[0]), 2);
    }

    TEST_F(ControlServiceTest, CoreManagerHasTheBuiltinMethods) {
        // InitializeCoreServices() registers them; the test environment runs it once per process
        EngineEnvironment::EnsureCoreServices();

        ControlService& core = CoreManager::GetControlService();
        std::vector<Json> coreResponses;
        core.Post(MakeRequest(1, "ping"), [&](eastl::string text) {
            auto parsed = ParseJson(text);
            coreResponses.push_back(parsed ? std::move(*parsed) : Json());
        });
        core.Drain();

        ASSERT_EQ(coreResponses.size(), 1u);
        EXPECT_TRUE(IsOk(coreResponses[0]));
    }

}  // namespace BECore::Tests
