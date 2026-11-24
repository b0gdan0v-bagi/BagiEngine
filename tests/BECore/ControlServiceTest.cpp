#include <BECore/Clipboard/ClipboardControl.h>
#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/ProfilerControl.h>
#include <BECore/Time/Time.h>
#include <BECore/UI/UIControl.h>
#include <Common/ControlTestSupport.h>
#include <Common/DestroyTally.h>
#include <Common/EngineEnvironment.h>
#include <CoreSDL/SDLScreenshot.h>
#include <Events/ApplicationEvents.h>
#include <Games/GameControl.h>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        /// Counts the QuitEvents that the "quit" method emits
        class QuitCounter : public SubscriptionHolder {
        public:
            QuitCounter() {
                Subscribe<ApplicationEvents::QuitEvent, &QuitCounter::OnQuit>(this);
            }

            int count = 0;

            void OnQuit(const ApplicationEvents::QuitEvent&) {
                ++count;
            }
        };

        void WriteEmptyObject(JsonWriter& writer) {
            writer.BeginObject();
            writer.EndObject();
        }

        /// What a running frame lasts in the loop of a test that has no real clock
        constexpr double kRealFrameSeconds = 1.0 / 64;

        class ControlServiceTest : public ::testing::Test {
        protected:
            void TearDown() override {
                service.SetPause(nullptr);  // a case's pause is a local of the case: the service must not keep its address past it
                CoreManager::GetTime().Reset();
            }

            ControlReplyFn Collect() {
                return [this](String text) { responses.emplace_back(StringView(text.c_str(), text.size())); };
            }

            void Post(const String& text) {
                service.Post(text, Collect());
            }

            /// Registers a method that records its name in `order` and answers with an empty result
            void RegisterRecording(StringView method, bool bypassBarrier = false) {
                const String name(method);
                service.RegisterHandler(
                    method,
                    [this, name](ControlCall& call) {
                        order.push_back(name);
                        call.ReplyEmpty();
                    },
                    bypassBarrier);
            }

            /// Registers a method that defers its answer by `frames` frames, then records `name-done`
            void RegisterDeferring(StringView method, uint32_t frames, bool bypassBarrier = false) {
                const String name(method);
                service.RegisterHandler(
                    method,
                    [this, name, frames](ControlCall& call) {
                        order.push_back(name);
                        call.DeferFrames(frames, [this, name](ControlCall& done) {
                            order.push_back(Format("{}-done", name));
                            done.ReplyEmpty();
                        });
                    },
                    bypassBarrier);
            }

            /// The application loop of the manual clock: a frame runs while a step is due. Returns how many ran
            int RunDueFrames() {
                constexpr int kGuard = 1000;  // a loop that does not end would hang the test run
                int frames = 0;
                while (service.IsStepDue() && frames < kGuard) {
                    CoreManager::GetTime().Advance(service.GetStepSeconds());
                    service.Drain();
                    service.EndFrame();
                    ++frames;
                }
                return frames;
            }

            /// The application loop over a pause: a frame is decided by the pause, the clock follows it, the requests run in it. Ends once @p answers answers are in or the guard
            /// stops it; returns the kind of every frame that ran. Without the manual clock a frame runs every pass, with it only while a step is due
            Vector<FrameKind> RunFramesUntilAnswered(GamePause& pause, size_t answers) {
                constexpr int kGuard = 1000;
                Vector<FrameKind> kinds;
                while (responses.size() < answers && static_cast<int>(kinds.size()) < kGuard) {
                    if (service.IsManualClock() && !pause.IsStepDue()) {
                        service.Drain();  // the idle cycle
                        if (!pause.IsStepDue()) {
                            break;
                        }
                    }
                    const FrameDecision frame = pause.BeginFrame();
                    switch (frame.kind) {
                        case FrameKind::Running:
                            CoreManager::GetTime().Advance(kRealFrameSeconds);
                            break;
                        case FrameKind::Stepped:
                            CoreManager::GetTime().Advance(frame.seconds);
                            break;
                        case FrameKind::Held:
                            CoreManager::GetTime().Hold();
                            break;
                    }
                    kinds.push_back(frame.kind);
                    service.Drain();
                    service.EndFrame();
                }
                return kinds;
            }

            /// The `result` of the answer @p index; not valid (and reads as nothing) when there is no such answer
            JsonView ResultOf(size_t index) const {
                return index < responses.size() ? responses[index].Result() : JsonView();
            }

            ControlService service;
            Vector<ControlReply> responses;
            Vector<String> order;
        };

        static_assert(ControlService::kMaxRequestBytes >= 1024);
        static_assert(!std::is_copy_constructible_v<ControlService>);
        static_assert(!std::is_copy_constructible_v<ControlCall>);

    }  // namespace

    TEST_F(ControlServiceTest, CompileTimeChecks) {
        SUCCEED();
    }

    TEST_F(ControlServiceTest, ServerStatusIsNotListeningUntilPublished) {
        const ControlServerStatus& status = service.GetServerStatus();
        EXPECT_FALSE(status.listening);
        EXPECT_EQ(status.port, 0);
        EXPECT_EQ(status.clients, 0u);
    }

    TEST_F(ControlServiceTest, ServerStatusRoundTrips) {
        service.SetServerStatus({true, 8123, 2});
        EXPECT_TRUE(service.GetServerStatus().listening);
        EXPECT_EQ(service.GetServerStatus().port, 8123);
        EXPECT_EQ(service.GetServerStatus().clients, 2u);

        service.SetServerStatus({});
        EXPECT_FALSE(service.GetServerStatus().listening);
        EXPECT_EQ(service.GetServerStatus().clients, 0u);
    }

    TEST_F(ControlServiceTest, AnswersOnlyWhenDrained) {
        RegisterRecording("hello");

        Post(MakeRequestText(1, "hello"));
        EXPECT_TRUE(responses.empty());

        service.Drain();
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(responses[0].IsOk());
        EXPECT_EQ(responses[0].IdText(), "1");
    }

    TEST_F(ControlServiceTest, EchoesIntegerAndStringIds) {
        service.RegisterHandler("echo", [](ControlCall& call) { call.Reply([&call](JsonWriter& writer) { writer.Raw(call.Params().Raw()); }); });

        Post(MakeRequestText(7, "echo", R"({"value":42})"));
        Post(R"({"id":"req-1","method":"echo"})");

        service.Drain();
        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(responses[0].IdText(), "7");
        ASSERT_TRUE(ResultOf(0).IsObject());
        EXPECT_EQ(IntOf(ResultOf(0), "value").value_or(0), 42);
        EXPECT_EQ(StringOf(responses[1].View(), "id"), "req-1");
    }

    TEST_F(ControlServiceTest, TheIdIsEchoedAsTheClientWroteIt) {
        service.RegisterHandler("ok", [](ControlCall& call) { call.Reply([](JsonWriter& writer) { writer.Value(true); }); });
        Vector<String> texts;
        const auto collect = [&texts] { return [&texts](const String& text) { texts.emplace_back(text.c_str(), text.size()); }; };

        service.Post(R"({"id":"a\"b\u00e9 \u00c3\u00a9 \u00e9","method":"ok"})", collect());
        service.Post(R"({"id":-0,"method":"ok"})", collect());
        service.Post(R"({"id":18446744073709551615,"method":"ok"})", collect());
        service.Drain();

        ASSERT_EQ(texts.size(), 3u);
        EXPECT_EQ(texts[0], String(R"({"id":"a\"b\u00e9 \u00c3\u00a9 \u00e9","ok":true,"result":true})"));
        EXPECT_EQ(texts[1], String(R"({"id":-0,"ok":true,"result":true})"));
        EXPECT_EQ(texts[2], String(R"({"id":18446744073709551615,"ok":true,"result":true})"));
    }

    TEST_F(ControlServiceTest, AnIdThatIsNotAStringOrAnIntegerOf64BitsIsAnInvalidRequestWithNoId) {
        RegisterRecording("ping");
        const char* const texts[] = {
            R"({"id":1.5,"method":"ping"})",
            R"({"id":true,"method":"ping"})",
            R"({"id":null,"method":"ping"})",
            R"({"id":18446744073709551616,"method":"ping"})",
            R"({"id":-9223372036854775809,"method":"ping"})",
        };

        for (const char* text : texts) {
            Post(text);
        }
        service.Drain();

        ASSERT_EQ(responses.size(), std::size(texts));
        for (size_t i = 0; i < responses.size(); ++i) {
            EXPECT_EQ(responses[i].ErrorCode(), "invalid_request") << texts[i];
            EXPECT_EQ(responses[i].IdText(), "null") << texts[i];
        }
        EXPECT_TRUE(order.empty());
    }

    TEST_F(ControlServiceTest, ATextWithAByteOrderMarkIsAParseError) {
        Post("\xEF\xBB\xBF{\"id\":1,\"method\":\"ping\"}");
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(responses[0].ErrorCode(), "parse_error");
        EXPECT_EQ(responses[0].IdText(), "null");
    }

    TEST_F(ControlServiceTest, UnknownMethodIsAnError) {
        Post(MakeRequestText(3, "no_such_method"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_FALSE(responses[0].IsOk());
        EXPECT_EQ(responses[0].ErrorCode(), "unknown_method");
        EXPECT_EQ(responses[0].IdText(), "3");
    }

    TEST_F(ControlServiceTest, UnknownMethodNameIsNotInterned) {
        Post(MakeRequestText(1, "client_only_method_5d02e8"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(responses[0].ErrorCode(), "unknown_method");
        EXPECT_FALSE(PoolString::Find(StringView{"client_only_method_5d02e8"}).has_value());
    }

    TEST_F(ControlServiceTest, LongMethodNameIsCutInTheMessage) {
        Post(MakeRequestText(1, String(5000, 'm')));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(responses[0].ErrorCode(), "unknown_method");
        EXPECT_LT(responses[0].ErrorMessage().size(), 200u);
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
            service.Post(String(testCase.text), Collect());
            service.Drain();

            ASSERT_EQ(responses.size(), 1u) << testCase.text;
            EXPECT_FALSE(responses[0].IsOk()) << testCase.text;
            EXPECT_EQ(responses[0].ErrorCode(), testCase.code) << testCase.text;
            const JsonView id = MemberOf(responses[0].View(), "id");
            ASSERT_TRUE(id.IsValid()) << testCase.text;
            EXPECT_EQ(id.IsInteger(), testCase.hasId) << testCase.text;
            EXPECT_EQ(id.IsNull(), !testCase.hasId) << testCase.text;
        }
        EXPECT_TRUE(order.empty());
    }

    TEST_F(ControlServiceTest, ParamsDefaultToAnEmptyObject) {
        int seen = 0;
        service.RegisterHandler("params", [&](ControlCall& call) {
            EXPECT_TRUE(call.Params().IsObject());
            EXPECT_EQ(call.Params().Size(), 0u);
            ++seen;
            call.ReplyEmpty();
        });

        Post(MakeRequestText(1, "params"));
        service.Post(R"({"id":2,"method":"params","params":null})", Collect());
        service.Drain();

        EXPECT_EQ(seen, 2);
        EXPECT_EQ(responses.size(), 2u);
    }

    TEST_F(ControlServiceTest, TooLargeRequestIsRejected) {
        const String tooLarge(ControlService::kMaxRequestBytes + 1, ' ');
        service.Post(tooLarge, Collect());

        const String atLimit(ControlService::kMaxRequestBytes, ' ');
        service.Post(atLimit, Collect());

        service.Drain();
        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(responses[0].ErrorCode(), "request_too_large");
        EXPECT_EQ(responses[1].ErrorCode(), "parse_error");  // within the limit, so it is parsed (and is not JSON)
    }

    TEST_F(ControlServiceTest, DeeplyNestedRequestIsRefusedBeforeItIsCopied) {
        RegisterRecording("ping");
        // A request is copied while it is handled, and that recurses once per level: 15000 levels overflow the stack of a debug build
        const auto nested = [](size_t levels) {
            String text = R"({"id":7,"method":"ping","params":{"a":)";
            text.append(levels, '[');
            text.append(levels, ']');
            text += "}}";
            return text;
        };
        // The request object and `params` are two levels of their own
        constexpr size_t kOwnLevels = 2;
        Post(nested(ControlService::kMaxRequestDepth - kOwnLevels));
        Post(nested(ControlService::kMaxRequestDepth - kOwnLevels + 1));
        Post(nested(15000));
        service.Drain();

        ASSERT_EQ(responses.size(), 3u);
        EXPECT_TRUE(responses[0].IsOk()) << "a request at the limit is handled";
        EXPECT_EQ(responses[1].ErrorCode(), "invalid_request");
        EXPECT_EQ(responses[2].ErrorCode(), "invalid_request");
        EXPECT_EQ(responses[2].IdText(), "null") << "refused before the id was read: the answer carries a null id";
    }

    TEST_F(ControlServiceTest, GarbageGetsAnAnswerAndNeverAbortsTheProcess) {
        RegisterRecording("ping");
        const String deepArray(200000, '[');
        String allBytes;
        for (int byte = 0; byte < 256; ++byte) {
            allBytes.push_back(static_cast<char>(byte));
        }
        const Vector<String> garbage = {
            allBytes,
            "\xff\xfe\xfd",
            deepArray,
            Format("{}{}", deepArray, String(200000, ']')),
            Format(R"({{"id":1,"method":"ping","params":{})", deepArray),
            String(100000, '"'),
            R"({"id":1,"method":"ping","params":{"a":[[[[[[[[]]]]]]]]}})",
            R"({"id":18446744073709551615,"method":"ping"})",
            R"({"id":-9223372036854775808,"method":"ping"})",
            R"({"id":1e999,"method":"ping"})",
            "{\"id\":1,\"method\":\"p\xff\xfe\"}",
            R"({"id":1,"method":"ping","method":"ping"})",
        };

        for (const String& text : garbage) {
            service.Post(text, Collect());
        }
        service.Drain();

        EXPECT_EQ(responses.size(), garbage.size());
        for (const ControlReply& response : responses) {
            EXPECT_TRUE(response.View().IsObject());
        }
    }

    TEST_F(ControlServiceTest, HandlerThatDoesNotAnswerGetsAnInternalError) {
        service.RegisterHandler("silent", [](ControlCall&) {});

        Post(MakeRequestText(1, "silent"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(responses[0].ErrorCode(), "internal_error");
    }

    TEST_F(ControlServiceTest, HandlerCanFail) {
        service.RegisterHandler("fail", [](ControlCall& call) { call.Fail("illegal_action", "that move is not legal"); });

        Post(MakeRequestText(5, "fail"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_FALSE(responses[0].IsOk());
        EXPECT_EQ(responses[0].ErrorCode(), "illegal_action");
        EXPECT_EQ(responses[0].ErrorMessage(), "that move is not legal");
        EXPECT_EQ(responses[0].IdText(), "5");
    }

    TEST_F(ControlServiceTest, ErrorMessageWithInvalidUtf8DoesNotAbort) {
        service.RegisterHandler("bad", [](ControlCall& call) { call.Fail("bad_text", "broken \xff text"); });

        Post(MakeRequestText(1, "bad"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(responses[0].ErrorCode(), "bad_text");
    }

    TEST_F(ControlServiceTest, RunsRequestsInPostOrder) {
        RegisterRecording("a");
        RegisterRecording("b");
        RegisterRecording("c");

        Post(MakeRequestText(1, "b"));
        Post(MakeRequestText(2, "a"));
        Post(MakeRequestText(3, "c"));
        Post(MakeRequestText(4, "a"));
        service.Drain();

        EXPECT_EQ(order, (Vector<String>{"b", "a", "c", "a"}));
        ASSERT_EQ(responses.size(), 4u);
        for (size_t i = 0; i < responses.size(); ++i) {
            EXPECT_EQ(responses[i].IdText(), Format("{}", i + 1));
        }
    }

    TEST_F(ControlServiceTest, KeepsTheOrderOfEachPostingThread) {
        constexpr int kThreads = 4;
        constexpr int kPerThread = 200;

        Vector<Vector<int64_t>> seen(kThreads);
        service.RegisterHandler("seq", [&](ControlCall& call) {
            const auto thread = GetParamInt(call.Params(), "thread");
            const auto seq = GetParamInt(call.Params(), "seq");
            ASSERT_TRUE(thread && seq);
            seen[static_cast<size_t>(*thread)].push_back(*seq);
            call.ReplyEmpty();
        });

        std::atomic<int> replies = 0;
        Vector<std::thread> threads;
        for (int thread = 0; thread < kThreads; ++thread) {
            threads.emplace_back([&, thread] {
                for (int seq = 0; seq < kPerThread; ++seq) {
                    const String params = Format(R"({{"seq":{},"thread":{}}})", seq, thread);
                    service.Post(MakeRequestText(seq, "seq", StringView(params.c_str(), params.size())), [&replies](String) { ++replies; });
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }

        service.Drain();

        EXPECT_EQ(replies.load(), kThreads * kPerThread);
        for (int thread = 0; thread < kThreads; ++thread) {
            const Vector<int64_t>& sequence = seen[static_cast<size_t>(thread)];
            ASSERT_EQ(sequence.size(), static_cast<size_t>(kPerThread)) << "thread " << thread;
            EXPECT_TRUE(std::is_sorted(sequence.begin(), sequence.end())) << "thread " << thread;
        }
    }

    TEST_F(ControlServiceTest, DeferredCallHoldsBackTheRequestsBehindIt) {
        RegisterDeferring("slow", 2);
        RegisterRecording("fast");

        Post(MakeRequestText(1, "slow"));
        Post(MakeRequestText(2, "fast"));
        service.Drain();
        EXPECT_EQ(order, (Vector<String>{"slow"}));
        EXPECT_TRUE(responses.empty());

        service.EndFrame();
        service.Drain();
        EXPECT_EQ(order, (Vector<String>{"slow"}));
        EXPECT_TRUE(responses.empty());

        service.EndFrame();
        EXPECT_EQ(order, (Vector<String>{"slow", "slow-done"}));
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(responses[0].IdText(), "1");

        service.Drain();
        EXPECT_EQ(order, (Vector<String>{"slow", "slow-done", "fast"}));
        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(responses[1].IdText(), "2");
    }

    TEST_F(ControlServiceTest, ZeroFramesCountsAsOne) {
        RegisterDeferring("now", 0);

        Post(MakeRequestText(1, "now"));
        service.Drain();
        EXPECT_TRUE(responses.empty());

        service.EndFrame();
        EXPECT_EQ(responses.size(), 1u);
    }

    TEST_F(ControlServiceTest, CountsDownOnceForEveryEndFrame) {
        RegisterDeferring("wait", 5);

        Post(MakeRequestText(1, "wait"));
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
                    second.ReplyEmpty();
                });
            });
        });

        Post(MakeRequestText(1, "twice"));
        service.Drain();
        service.EndFrame();
        EXPECT_EQ(order, (Vector<String>{"first"}));
        EXPECT_TRUE(responses.empty());

        service.EndFrame();
        EXPECT_EQ(order, (Vector<String>{"first", "second"}));
        EXPECT_EQ(responses.size(), 1u);
    }

    TEST_F(ControlServiceTest, DoneThatDoesNotAnswerGetsAnInternalError) {
        service.RegisterHandler("lazy", [](ControlCall& call) { call.DeferFrames(1, [](ControlCall&) {}); });

        Post(MakeRequestText(1, "lazy"));
        service.Drain();
        service.EndFrame();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(responses[0].ErrorCode(), "internal_error");
    }

    TEST_F(ControlServiceTest, BypassingMethodRunsWhileACallIsPending) {
        RegisterDeferring("hold", 3);
        RegisterRecording("normal");
        RegisterRecording("urgent", /*bypassBarrier=*/true);

        Post(MakeRequestText(1, "hold"));
        Post(MakeRequestText(2, "normal"));
        Post(MakeRequestText(3, "urgent"));
        service.Drain();

        EXPECT_EQ(order, (Vector<String>{"hold", "urgent"}));
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(responses[0].IdText(), "3");

        service.EndFrame();
        service.EndFrame();
        service.EndFrame();
        service.Drain();

        EXPECT_EQ(order, (Vector<String>{"hold", "urgent", "hold-done", "normal"}));
        ASSERT_EQ(responses.size(), 3u);
        EXPECT_EQ(responses[1].IdText(), "1");
        EXPECT_EQ(responses[2].IdText(), "2");
    }

    TEST_F(ControlServiceTest, BypassingMethodCannotDeferWhileACallIsPending) {
        RegisterDeferring("hold", 2);
        RegisterDeferring("urgent", 1, /*bypassBarrier=*/true);

        Post(MakeRequestText(1, "hold"));
        Post(MakeRequestText(2, "urgent"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(responses[0].IdText(), "2");
        EXPECT_EQ(responses[0].ErrorCode(), "internal_error");

        service.EndFrame();
        service.EndFrame();
        ASSERT_EQ(responses.size(), 2u);  // the pending call was not disturbed
        EXPECT_EQ(responses[1].IdText(), "1");
        EXPECT_TRUE(responses[1].IsOk());
    }

    TEST_F(ControlServiceTest, HeldRequestsKeepTheirOrderAcrossDrains) {
        RegisterDeferring("hold", 2);
        RegisterRecording("a");
        RegisterRecording("b");
        RegisterRecording("c");
        RegisterRecording("d");

        Post(MakeRequestText(1, "hold"));
        Post(MakeRequestText(2, "a"));
        Post(MakeRequestText(3, "b"));
        service.Drain();
        Post(MakeRequestText(4, "c"));
        service.Drain();
        service.EndFrame();
        Post(MakeRequestText(5, "d"));
        service.Drain();
        EXPECT_EQ(order, (Vector<String>{"hold"}));

        service.EndFrame();
        service.Drain();

        EXPECT_EQ(order, (Vector<String>{"hold", "hold-done", "a", "b", "c", "d"}));
    }

    TEST_F(ControlServiceTest, LargeParamsReachTheHandlerIntact) {
        const size_t blobBytes = ControlService::kMaxRequestBytes - 1024;
        String blob(blobBytes, 'x');
        blob[0] = 'a';
        blob[blobBytes - 1] = 'z';
        const String params = Format(R"({{"blob":"{}"}})", blob);  // the blob has nothing to escape

        bool intact = false;
        service.RegisterHandler("big", [&](ControlCall& call) {
            const auto received = GetParamString(call.Params(), "blob");
            intact = received && *received == blob;
            call.ReplyEmpty();
        });

        Post(MakeRequestText(1, "big", StringView(params.c_str(), params.size())));
        service.Drain();

        EXPECT_TRUE(intact);
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(responses[0].IsOk());
    }

    TEST_F(ControlServiceTest, InvalidRequestBehindADeferredCallKeepsItsPlace) {
        RegisterDeferring("hold", 1);

        Post(MakeRequestText(1, "hold"));
        service.Post("not json", Collect());
        service.Drain();
        EXPECT_TRUE(responses.empty());

        service.EndFrame();
        service.Drain();
        ASSERT_EQ(responses.size(), 2u);
        EXPECT_TRUE(responses[0].IsOk());
        EXPECT_EQ(responses[1].ErrorCode(), "parse_error");
    }

    TEST_F(ControlServiceTest, FrameEndHooksRunBeforeADeferredCallCompletes) {
        RegisterDeferring("hold", 1);
        service.SetFrameEndHook("test", [this] { order.push_back("hook"); });

        Post(MakeRequestText(1, "hold"));
        service.Drain();
        service.EndFrame();

        EXPECT_EQ(order, (Vector<String>{"hold", "hook", "hold-done"}));
    }

    TEST_F(ControlServiceTest, HandlerMayRegisterHandlers) {
        service.RegisterHandler("register", [this](ControlCall& call) {
            for (int index = 0; index < 200; ++index) {  // enough to rehash the handler table
                service.RegisterHandler(Format("extra{}", index), [](ControlCall& extra) { extra.ReplyEmpty(); });
            }
            call.ReplyEmpty();
        });

        Post(MakeRequestText(1, "register"));
        Post(MakeRequestText(2, "extra199"));
        service.Drain();

        ASSERT_EQ(responses.size(), 2u);
        EXPECT_TRUE(responses[0].IsOk());
        EXPECT_TRUE(responses[1].IsOk());
    }

    TEST_F(ControlServiceTest, RegisteringAMethodAgainReplacesIt) {
        service.RegisterHandler("m", [this](ControlCall& call) {
            order.push_back("old");
            call.ReplyEmpty();
        });
        service.RegisterHandler("m", [this](ControlCall& call) {
            order.push_back("new");
            call.ReplyEmpty();
        });

        Post(MakeRequestText(1, "m"));
        service.Drain();

        EXPECT_EQ(order, (Vector<String>{"new"}));
    }

    TEST_F(ControlServiceTest, HandlerWithAMoveOnlyCaptureIsCalledAndDestroyedOnceWhenReplaced) {
        int destroyed = 0;
        int calls = 0;
        service.RegisterHandler("m", [owner = DestroyTally(destroyed), &calls](ControlCall& call) {
            ++calls;
            call.ReplyEmpty();
        });

        Post(MakeRequestText(1, "m"));
        Post(MakeRequestText(2, "m"));
        service.Drain();
        EXPECT_EQ(calls, 2);
        EXPECT_EQ(destroyed, 0);

        service.RegisterHandler("m", [](ControlCall& call) { call.ReplyEmpty(); });
        EXPECT_EQ(destroyed, 1);
    }

    TEST_F(ControlServiceTest, HandlerWithAMoveOnlyCaptureIsDestroyedOnceWithTheService) {
        int destroyed = 0;
        {
            ControlService local;
            local.RegisterHandler("m", [owner = DestroyTally(destroyed)](ControlCall& call) { call.ReplyEmpty(); });
            local.Post(MakeRequestText(1, "m"), Collect());
            local.Drain();
            EXPECT_EQ(destroyed, 0);
        }
        EXPECT_EQ(destroyed, 1);
    }

    TEST_F(ControlServiceTest, HandlerThatReplacesItselfKeepsItsCaptureUntilTheCallEnds) {
        int destroyed = 0;
        int destroyedAfterRegistering = -1;
        constexpr int kWitness = 42;
        int witnessAfterRegistering = 0;
        service.RegisterHandler("self", [this, owner = DestroyTally(destroyed), witness = kWitness, &destroyed, &destroyedAfterRegistering, &witnessAfterRegistering](ControlCall& call) {
            service.RegisterHandler("self", [](ControlCall& next) { next.ReplyEmpty(); });
            destroyedAfterRegistering = destroyed;
            witnessAfterRegistering = witness;
            call.ReplyEmpty();
        });

        Post(MakeRequestText(1, "self"));
        service.Drain();

        EXPECT_EQ(destroyedAfterRegistering, 0);
        EXPECT_EQ(witnessAfterRegistering, kWitness);
        EXPECT_EQ(destroyed, 1);
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(responses[0].IsOk());

        Post(MakeRequestText(2, "self"));
        service.Drain();
        ASSERT_EQ(responses.size(), 2u);
        EXPECT_TRUE(responses[1].IsOk());
    }

    TEST_F(ControlServiceTest, HandlerThatRegistersOthersStaysRegistered) {
        int calls = 0;
        service.RegisterHandler("grow", [this, &calls](ControlCall& call) {
            ++calls;
            for (int index = 0; index < 200; ++index) {
                service.RegisterHandler(Format("extra{}", index), [](ControlCall& extra) { extra.ReplyEmpty(); });
            }
            call.ReplyEmpty();
        });

        Post(MakeRequestText(1, "grow"));
        Post(MakeRequestText(2, "grow"));
        service.Drain();

        EXPECT_EQ(calls, 2);
        EXPECT_EQ(responses.size(), 2u);
    }

    TEST_F(ControlServiceTest, FrameEndHookThatReplacesItselfKeepsItsCaptureUntilItReturns) {
        int destroyed = 0;
        int destroyedAfterReplacing = -1;
        service.SetFrameEndHook("a", [this, owner = DestroyTally(destroyed), &destroyed, &destroyedAfterReplacing] {
            service.SetFrameEndHook("a", [this] { order.push_back("second"); });
            destroyedAfterReplacing = destroyed;
        });

        service.EndFrame();
        EXPECT_EQ(destroyedAfterReplacing, 0);
        EXPECT_EQ(destroyed, 1);

        service.EndFrame();
        EXPECT_EQ(order, (Vector<String>{"second"}));
    }

    TEST_F(ControlServiceTest, FrameEndHookThatAddsHooksKeepsRunning) {
        int calls = 0;
        service.SetFrameEndHook("grow", [this, &calls] {
            ++calls;
            for (int index = 0; index < 50; ++index) {  // enough to move the entries of the list
                service.SetFrameEndHook(Format("extra{}", index), [] {});
            }
        });

        service.EndFrame();
        service.EndFrame();

        EXPECT_EQ(calls, 2);
    }

    TEST_F(ControlServiceTest, ShutdownAnswersEverythingWithShuttingDown) {
        RegisterDeferring("hold", 5);
        RegisterRecording("queued");

        Post(MakeRequestText(1, "hold"));
        Post(MakeRequestText(2, "queued"));
        service.Drain();                     // "hold" is pending, "queued" waits in the inbox
        Post(MakeRequestText(3, "queued"));  // still in the queue
        EXPECT_TRUE(responses.empty());

        service.Shutdown();

        ASSERT_EQ(responses.size(), 3u);
        for (const ControlReply& response : responses) {
            EXPECT_FALSE(response.IsOk());
            EXPECT_EQ(response.ErrorCode(), "shutting_down");
        }
        EXPECT_EQ(order, (Vector<String>{"hold"}));
    }

    TEST_F(ControlServiceTest, RequestPostedAfterShutdownIsAnsweredAtOnce) {
        RegisterRecording("late");
        service.Shutdown();

        Post(MakeRequestText(9, "late"));

        ASSERT_EQ(responses.size(), 1u);  // no Drain() needed
        EXPECT_EQ(responses[0].ErrorCode(), "shutting_down");
        EXPECT_EQ(responses[0].IdText(), "9");
        EXPECT_TRUE(order.empty());
    }

    TEST_F(ControlServiceTest, ARequestPostedAfterShutdownIsAnsweredWithItsIdAsWritten) {
        String text;
        service.Shutdown();

        service.Post(R"({"id":"x\u00e9","method":"late"})", [&text](const String& reply) { text = reply; });

        EXPECT_EQ(text, String(R"({"error":{"code":"shutting_down","message":"the application is shutting down"},"id":"x\u00e9","ok":false})"));
    }

    TEST_F(ControlServiceTest, PublishSendsAnEventToTheSink) {
        Vector<String> pushed;
        service.SetEventSink([&](const String& text) { pushed.emplace_back(text.c_str(), text.size()); });

        service.Publish("state_changed", [](JsonWriter& writer) {
            writer.BeginObject();
            writer.Member("version", 3);
            writer.EndObject();
        });

        ASSERT_EQ(pushed.size(), 1u);
        EXPECT_EQ(pushed[0], String(R"({"data":{"version":3},"event":"state_changed"})"));
    }

    TEST_F(ControlServiceTest, TheSinkStaysAfterPublishing) {
        int calls = 0;
        service.SetEventSink([&calls](const String&) { ++calls; });

        service.Publish("a", WriteEmptyObject);
        service.Publish("b", WriteEmptyObject);

        EXPECT_EQ(calls, 2);
    }

    TEST_F(ControlServiceTest, SinkThatReplacesItselfKeepsItsCaptureUntilItReturns) {
        int destroyed = 0;
        int destroyedAfterReplacing = -1;
        service.SetEventSink([this, owner = DestroyTally(destroyed), &destroyed, &destroyedAfterReplacing](const String&) {
            service.SetEventSink([this](const String&) { order.push_back("second"); });
            destroyedAfterReplacing = destroyed;
        });

        service.Publish("a", WriteEmptyObject);
        EXPECT_EQ(destroyedAfterReplacing, 0);
        EXPECT_EQ(destroyed, 1);

        service.Publish("b", WriteEmptyObject);
        EXPECT_EQ(order, (Vector<String>{"second"}));
    }

    TEST_F(ControlServiceTest, SinkThatClearsItselfKeepsItsCaptureUntilItReturns) {
        int destroyed = 0;
        int destroyedAfterClearing = -1;
        int calls = 0;
        service.SetEventSink([this, owner = DestroyTally(destroyed), &destroyed, &destroyedAfterClearing, &calls](const String&) {
            ++calls;
            service.SetEventSink({});
            destroyedAfterClearing = destroyed;
        });

        service.Publish("a", WriteEmptyObject);
        EXPECT_EQ(destroyedAfterClearing, 0);
        EXPECT_EQ(destroyed, 1);

        service.Publish("b", WriteEmptyObject);
        EXPECT_EQ(calls, 1);
    }

    TEST_F(ControlServiceTest, AnEventPublishedWithoutASinkIsDroppedNotKeptForALaterSink) {
        service.Publish("state_changed", WriteEmptyObject);
        Vector<String> pushed;
        service.SetEventSink([&pushed](const String& text) { pushed.push_back(text); });
        EXPECT_TRUE(pushed.empty());

        service.Publish("later", WriteEmptyObject);
        ASSERT_EQ(pushed.size(), 1u);
        EXPECT_EQ(At(pushed, 0).find("state_changed"), String::npos);
    }

    TEST_F(ControlServiceTest, PublishObserverSeesEventsWithoutASink) {
        Vector<String> seen;
        const auto id = service.AddPublishObserver([&seen](StringView event, JsonView data) { seen.push_back(Format("{}:{}", event, GetParamInt(data, "n").value_or(-1))); });

        service.Publish("a", [](JsonWriter& writer) {
            writer.BeginObject();
            writer.Member("n", 4);
            writer.EndObject();
        });

        EXPECT_NE(id, ControlService::kNoPublishObserver);
        EXPECT_EQ(seen, (Vector<String>{"a:4"}));
    }

    TEST_F(ControlServiceTest, PublishObserverRunsBesideTheSink) {
        int sunk = 0;
        int seen = 0;
        service.SetEventSink([&sunk](const String&) { ++sunk; });
        const auto id = service.AddPublishObserver([&seen](StringView, JsonView) { ++seen; });

        service.Publish("a", WriteEmptyObject);
        service.RemovePublishObserver(id);
        service.Publish("b", WriteEmptyObject);

        EXPECT_EQ(seen, 1);
        EXPECT_EQ(sunk, 2);
    }

    TEST_F(ControlServiceTest, PublishObserversAreCalledInTheOrderTheyWereAdded) {
        (void)service.AddPublishObserver([this](StringView, JsonView) { order.push_back("first"); });
        (void)service.AddPublishObserver([this](StringView, JsonView) { order.push_back("second"); });

        service.Publish("a", WriteEmptyObject);

        EXPECT_EQ(order, (Vector<String>{"first", "second"}));
    }

    TEST_F(ControlServiceTest, PublishObserverMayRemoveItselfAndKeepsItsCaptureUntilItReturns) {
        int destroyed = 0;
        int destroyedAfterRemoving = -1;
        int calls = 0;
        ControlService::PublishObserverId id = ControlService::kNoPublishObserver;
        id = service.AddPublishObserver([this, &id, owner = DestroyTally(destroyed), &destroyed, &destroyedAfterRemoving, &calls](StringView, JsonView) {
            ++calls;
            service.RemovePublishObserver(id);
            destroyedAfterRemoving = destroyed;
        });
        (void)service.AddPublishObserver([this](StringView, JsonView) { order.push_back("other"); });

        service.Publish("a", WriteEmptyObject);
        service.Publish("b", WriteEmptyObject);

        EXPECT_EQ(calls, 1);
        EXPECT_EQ(destroyedAfterRemoving, 0);
        EXPECT_EQ(destroyed, 1);
        EXPECT_EQ(order, (Vector<String>{"other", "other"}));
    }

    TEST_F(ControlServiceTest, PublishObserverMayRemoveAnotherThatHasNotRunYet) {
        ControlService::PublishObserverId second = ControlService::kNoPublishObserver;
        (void)service.AddPublishObserver([this, &second](StringView, JsonView) {
            order.push_back("first");
            service.RemovePublishObserver(second);
        });
        second = service.AddPublishObserver([this](StringView, JsonView) { order.push_back("second"); });

        service.Publish("a", WriteEmptyObject);

        EXPECT_EQ(order, (Vector<String>{"first"}));
    }

    TEST_F(ControlServiceTest, PublishObserverAddedByAnObserverSeesTheNextPublish) {
        bool added = false;
        (void)service.AddPublishObserver([this, &added](StringView, JsonView) {
            order.push_back("outer");
            if (!added) {
                added = true;
                (void)service.AddPublishObserver([this](StringView, JsonView) { order.push_back("inner"); });
            }
        });

        service.Publish("a", WriteEmptyObject);
        service.Publish("b", WriteEmptyObject);

        EXPECT_EQ(order, (Vector<String>{"outer", "outer", "inner"}));
    }

    TEST_F(ControlServiceTest, PublishFromInsideAnObserverReachesTheSinkOnly) {
        int sunk = 0;
        int seen = 0;
        service.SetEventSink([&sunk](const String&) { ++sunk; });
        (void)service.AddPublishObserver([this, &seen](StringView, JsonView) {
            ++seen;
            service.Publish("again", WriteEmptyObject);
        });

        service.Publish("a", WriteEmptyObject);

        EXPECT_EQ(seen, 1);
        EXPECT_EQ(sunk, 2);
    }

    TEST_F(ControlServiceTest, TheViewOfAnObserverStaysTheDataOfItsEventWhileThePublishOfAnObserverReachesTheSinkOnly) {
        int pushes = 0;
        service.SetEventSink([&pushes](const String&) { ++pushes; });
        String before;
        String after;
        (void)service.AddPublishObserver([&](StringView event, JsonView data) {
            if (event != "outer") {
                return;
            }
            before = String(data.Raw());
            service.Publish("inner", [](JsonWriter& writer) {
                writer.BeginObject();
                writer.Member("other", true);
                writer.EndObject();
            });
            after = String(data.Raw());
        });

        service.Publish("outer", [](JsonWriter& writer) {
            writer.BeginObject();
            writer.Member("a", 1);
            writer.EndObject();
        });

        EXPECT_EQ(before, String(R"({"a":1})"));
        EXPECT_EQ(after, before);
        EXPECT_EQ(pushes, 2);
    }

    TEST_F(ControlServiceTest, RemovingAnUnknownPublishObserverIsHarmless) {
        int seen = 0;
        const auto id = service.AddPublishObserver([&seen](StringView, JsonView) { ++seen; });

        service.RemovePublishObserver(id + 100);
        service.RemovePublishObserver(ControlService::kNoPublishObserver);
        service.RemovePublishObserver(id);
        service.RemovePublishObserver(id);
        service.Publish("a", WriteEmptyObject);

        EXPECT_EQ(seen, 0);
    }

    TEST_F(ControlServiceTest, PingReportsTheProtocolVersion) {
        RegisterControlBuiltins(service);

        Post(MakeRequestText(1, "ping"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(responses[0].IsOk());
        ASSERT_TRUE(ResultOf(0).IsObject());
        EXPECT_TRUE(BoolOf(ResultOf(0), "pong").value_or(false));
        EXPECT_EQ(IntOf(ResultOf(0), "protocol").value_or(0), ControlService::kProtocolVersion);
    }

    TEST_F(ControlServiceTest, StatusReportsTheFrameClock) {
        RegisterControlBuiltins(service);
        CoreManager::GetTime().Reset();
        CoreManager::GetTime().Advance(0.125);
        CoreManager::GetTime().Advance(0.125);

        Post(MakeRequestText(1, "status"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        const JsonView result = ResultOf(0);
        ASSERT_TRUE(result.IsObject());
        EXPECT_EQ(IntOf(result, "frame").value_or(0), 2);
        EXPECT_DOUBLE_EQ(NumberOf(result, "totalTime"), 0.25);
        EXPECT_DOUBLE_EQ(NumberOf(result, "deltaTime"), 0.125);
        EXPECT_GT(NumberOf(result, "fixedDeltaTime"), 0.0);
    }

    TEST_F(ControlServiceTest, QuitEmitsTheQuitEventAfterAnswering) {
        RegisterControlBuiltins(service);
        QuitCounter counter;

        Post(MakeRequestText(1, "quit"));
        service.Drain();

        EXPECT_EQ(counter.count, 1);
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(responses[0].IsOk());
    }

    TEST_F(ControlServiceTest, QuitBypassesAPendingCall) {
        RegisterControlBuiltins(service);
        RegisterDeferring("long_step", 1000);
        QuitCounter counter;

        Post(MakeRequestText(1, "long_step"));
        Post(MakeRequestText(2, "quit"));
        service.Drain();

        EXPECT_EQ(counter.count, 1);
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(responses[0].IdText(), "2");
    }

    namespace {

        const ControlMethodListing* FindListing(const Vector<ControlMethodListing>& methods, StringView name) {
            for (const ControlMethodListing& listing : methods) {
                if (listing.name == name) {
                    return &listing;
                }
            }
            return nullptr;
        }

        /// The entry of `methods` called @p name; not valid when there is none
        JsonView FindListedMethod(JsonView result, StringView name) {
            for (const JsonView method : MemberOf(result, "methods").Items()) {
                if (StringOf(method, "name") == name) {
                    return method;
                }
            }
            return JsonView();
        }

    }  // namespace

    TEST_F(ControlServiceTest, GetMethodsListsEveryHandlerSortedByName) {
        RegisterRecording("zeta");
        RegisterRecording("alpha");
        RegisterRecording("mid");

        const Vector<ControlMethodListing> methods = service.GetMethods();

        ASSERT_EQ(methods.size(), 3u);
        EXPECT_EQ(methods[0].name, "alpha");
        EXPECT_EQ(methods[1].name, "mid");
        EXPECT_EQ(methods[2].name, "zeta");
        EXPECT_EQ(methods[0].info, nullptr);
    }

    TEST_F(ControlServiceTest, ADescribedMethodIsListedWithItsDescription) {
        RegisterRecording("move");
        service.DescribeMethod("move", {"Moves a piece", {{"from", "string", true, "the square"}, {"to", "string", false, ""}}});

        const Vector<ControlMethodListing> methods = service.GetMethods();

        const ControlMethodListing* listing = FindListing(methods, "move");
        ASSERT_NE(listing, nullptr);
        ASSERT_NE(listing->info, nullptr);
        EXPECT_EQ(listing->info->summary, "Moves a piece");
        ASSERT_EQ(listing->info->params.size(), 2u);
        EXPECT_EQ(listing->info->params[0].name, "from");
        EXPECT_TRUE(listing->info->params[0].required);
        EXPECT_FALSE(listing->info->params[1].required);
    }

    TEST_F(ControlServiceTest, ARegisteredAgainMethodKeepsItsDescription) {
        RegisterRecording("move");
        service.DescribeMethod("move", {"Moves a piece", {}});
        RegisterRecording("move");

        const Vector<ControlMethodListing> methods = service.GetMethods();

        ASSERT_EQ(methods.size(), 1u);
        ASSERT_NE(methods[0].info, nullptr);
        EXPECT_EQ(methods[0].info->summary, "Moves a piece");
    }

    TEST_F(ControlServiceTest, ADescriptionIsReplacedByTheNextOne) {
        RegisterRecording("move");
        service.DescribeMethod("move", {"first", {{"a", "any", false, ""}}});
        service.DescribeMethod("move", {"second", {}});

        const Vector<ControlMethodListing> methods = service.GetMethods();

        ASSERT_EQ(methods.size(), 1u);
        ASSERT_NE(methods[0].info, nullptr);
        EXPECT_EQ(methods[0].info->summary, "second");
        EXPECT_TRUE(methods[0].info->params.empty());
    }

    TEST_F(ControlServiceTest, ADescriptionWithoutAHandlerIsNotListedButWaitsForOne) {
        service.DescribeMethod("later", {"Described first", {}});
        EXPECT_TRUE(service.GetMethods().empty());

        RegisterRecording("later");

        const Vector<ControlMethodListing> methods = service.GetMethods();
        ASSERT_EQ(methods.size(), 1u);
        ASSERT_NE(methods[0].info, nullptr);
        EXPECT_EQ(methods[0].info->summary, "Described first");
    }

    TEST_F(ControlServiceTest, TheMethodsVersionMovesOnANewNameAndOnADescriptionOnly) {
        const uint64_t start = service.GetMethodsVersion();

        RegisterRecording("one");
        const uint64_t afterNewName = service.GetMethodsVersion();
        EXPECT_GT(afterNewName, start);

        RegisterRecording("one");
        EXPECT_EQ(service.GetMethodsVersion(), afterNewName);

        service.DescribeMethod("one", {"text", {}});
        const uint64_t afterDescription = service.GetMethodsVersion();
        EXPECT_GT(afterDescription, afterNewName);

        Post(MakeRequestText(1, "one"));
        service.Drain();
        service.SetFrameEndHook("hook", [] {});
        service.EndFrame();
        EXPECT_EQ(service.GetMethodsVersion(), afterDescription);
    }

    TEST_F(ControlServiceTest, ListMethodsAnswersWithTheDescriptionsOfTheBuiltins) {
        RegisterControlBuiltins(service);
        RegisterRecording("undescribed");

        Post(MakeRequestText(1, "list_methods"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        ASSERT_TRUE(responses[0].IsOk());
        const JsonView result = ResultOf(0);
        ASSERT_TRUE(result.IsObject());

        const JsonView ping = FindListedMethod(result, "ping");
        ASSERT_TRUE(ping.IsObject());
        EXPECT_FALSE(StringOf(ping, "summary").empty());
        const JsonView pingParams = MemberOf(ping, "params");
        EXPECT_TRUE(pingParams.IsArray());
        EXPECT_EQ(pingParams.Size(), 0u);

        const JsonView step = FindListedMethod(result, "step");
        ASSERT_TRUE(step.IsObject());
        const JsonView stepParams = MemberOf(step, "params");
        ASSERT_TRUE(stepParams.IsArray());
        ASSERT_EQ(stepParams.Size(), 2u);
        EXPECT_EQ(StringOf(ItemAt(stepParams, 0), "name"), "frames");
        EXPECT_EQ(StringOf(ItemAt(stepParams, 0), "type"), "integer");
        EXPECT_EQ(BoolOf(ItemAt(stepParams, 0), "required"), false);
        EXPECT_EQ(StringOf(ItemAt(stepParams, 1), "name"), "dt");

        const JsonView undescribed = FindListedMethod(result, "undescribed");
        ASSERT_TRUE(undescribed.IsObject());
        EXPECT_EQ(StringOf(undescribed, "summary"), "");
        const JsonView undescribedParams = MemberOf(undescribed, "params");
        ASSERT_TRUE(undescribedParams.IsArray());
        EXPECT_EQ(undescribedParams.Size(), 0u);
        EXPECT_TRUE(FindListedMethod(result, "list_methods").IsObject());
    }

    TEST_F(ControlServiceTest, AMethodWithoutADescriptionStillRuns) {
        RegisterRecording("plain");

        Post(MakeRequestText(1, "plain"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(responses[0].IsOk());
        ASSERT_EQ(order.size(), 1u);
    }

    TEST_F(ControlServiceTest, ListingMethodsDoesNotInternTheirNames) {
        ASSERT_FALSE(PoolString::Find(StringView{"listed_only_method_7c41b9"}).has_value());
        RegisterRecording("listed_only_method_7c41b9");
        service.DescribeMethod("listed_only_method_7c41b9", {"text", {{"listed_only_param_7c41b9", "any", false, ""}}});

        EXPECT_EQ(service.GetMethods().size(), 1u);

        EXPECT_FALSE(PoolString::Find(StringView{"listed_only_method_7c41b9"}).has_value());
        EXPECT_FALSE(PoolString::Find(StringView{"listed_only_param_7c41b9"}).has_value());
    }

    namespace {

        /// What a BagiEngineTests process can register: the builtins and every module's methods (a game registers its own beside them)
        void RegisterEveryModuleMethod(ControlService& service) {
            RegisterControlBuiltins(service);
            RegisterUIControl(service);
            RegisterProfilerControl(service);
            RegisterClipboardControl(service);
            RegisterGameControl(service);
            RegisterScreenshotControl(service);
        }

        Vector<String> RequiredParams(const Vector<ControlMethodListing>& methods, StringView name) {
            Vector<String> required;
            const ControlMethodListing* listing = FindListing(methods, name);
            if (listing != nullptr && listing->info != nullptr) {
                for (const ControlParamInfo& param : listing->info->params) {
                    if (param.required) {
                        required.push_back(param.name);
                    }
                }
            }
            return required;
        }

    }  // namespace

    TEST_F(ControlServiceTest, EveryMethodOfTheModulesIsDescribed) {
        RegisterEveryModuleMethod(service);

        const Vector<ControlMethodListing> methods = service.GetMethods();

        EXPECT_NE(FindListing(methods, "ui_click"), nullptr);
        EXPECT_NE(FindListing(methods, "perform_action"), nullptr);
        EXPECT_NE(FindListing(methods, "screenshot"), nullptr);
        for (const ControlMethodListing& listing : methods) {
            ASSERT_NE(listing.info, nullptr) << listing.name.c_str() << " has no description";
            EXPECT_FALSE(listing.info->summary.empty()) << listing.name.c_str();
            for (const ControlParamInfo& param : listing.info->params) {
                EXPECT_FALSE(param.name.empty()) << listing.name.c_str();
                EXPECT_FALSE(param.type.empty()) << listing.name.c_str() << "." << param.name.c_str();
            }
        }
    }

    TEST_F(ControlServiceTest, TheRequiredParamsOfADescriptionAreTheOnesTheHandlerRejects) {
        RegisterEveryModuleMethod(service);
        const Vector<ControlMethodListing> methods = service.GetMethods();

        EXPECT_EQ(RequiredParams(methods, "ui_click"), Vector<String>{"name"});
        EXPECT_EQ(RequiredParams(methods, "clipboard_set"), Vector<String>{"text"});
        EXPECT_TRUE(RequiredParams(methods, "step").empty());

        Post(MakeRequestText(1, "ui_click"));
        Post(MakeRequestText(2, "clipboard_set"));
        Post(MakeRequestText(3, "switch_game"));
        service.Drain();

        ASSERT_EQ(responses.size(), 3u);
        EXPECT_EQ(responses[0].ErrorCode(), "invalid_params");
        EXPECT_EQ(responses[1].ErrorCode(), "invalid_params");
        EXPECT_EQ(responses[2].ErrorCode(), "invalid_params");
        EXPECT_EQ(RequiredParams(methods, "switch_game"), Vector<String>{"name"});
    }

    TEST_F(ControlServiceTest, TheTypesOfADescriptionAreTheOnesTheHandlerChecks) {
        RegisterEveryModuleMethod(service);
        const auto typeOf = [&](StringView method, StringView param) -> String {
            const Vector<ControlMethodListing> methods = service.GetMethods();
            const ControlMethodListing* listing = FindListing(methods, method);
            if (listing != nullptr && listing->info != nullptr) {
                for (const ControlParamInfo& info : listing->info->params) {
                    if (info.name == param) {
                        return info.type;
                    }
                }
            }
            return {};
        };
        EXPECT_EQ(typeOf("step", "frames"), "integer");
        EXPECT_EQ(typeOf("step", "dt"), "number");
        EXPECT_EQ(typeOf("ui_click", "canvas"), "string");
        EXPECT_EQ(typeOf("ui_click", "at"), "number");

        Post(MakeRequestText(1, "step", R"({"frames":"three"})"));
        Post(MakeRequestText(2, "step", R"({"dt":"slow"})"));
        Post(MakeRequestText(3, "ui_click", R"({"canvas":7,"name":"Ok"})"));
        Post(MakeRequestText(4, "ui_click", R"({"at":"left","name":"Ok"})"));
        service.Drain();

        ASSERT_EQ(responses.size(), 4u);
        for (const ControlReply& response : responses) {
            EXPECT_EQ(response.ErrorCode(), "invalid_params");
        }
    }

    TEST_F(ControlServiceTest, StatusReportsWhetherTheClockIsManual) {
        RegisterControlBuiltins(service);

        Post(MakeRequestText(1, "status"));
        service.Drain();
        service.SetManualClock(true);
        Post(MakeRequestText(2, "status"));
        service.Drain();

        ASSERT_EQ(responses.size(), 2u);
        EXPECT_FALSE(BoolOf(ResultOf(0), "manualClock").value_or(true));
        EXPECT_TRUE(BoolOf(ResultOf(1), "manualClock").value_or(false));
    }

    TEST_F(ControlServiceTest, TheClockIsRealUnlessTurnedManual) {
        EXPECT_FALSE(service.IsManualClock());
        EXPECT_FALSE(service.IsStepDue());

        service.SetManualClock(true);
        EXPECT_TRUE(service.IsManualClock());
        EXPECT_FALSE(service.IsStepDue());
    }

    TEST_F(ControlServiceTest, RealClockNeverAsksForAFrame) {
        RegisterDeferring("wait", 3);
        Post(MakeRequestText(1, "wait"));
        service.Drain();

        EXPECT_FALSE(service.IsStepDue());  // the loop ticks by itself and the call counts those frames
        service.EndFrame();
        service.EndFrame();
        EXPECT_TRUE(responses.empty());
        service.EndFrame();
        EXPECT_EQ(responses.size(), 1u);
    }

    TEST_F(ControlServiceTest, ManualClockRunsOneFramePerFrameAskedFor) {
        service.SetManualClock(true);
        RegisterDeferring("wait", 3);
        Post(MakeRequestText(1, "wait"));
        service.Drain();

        ASSERT_TRUE(service.IsStepDue());
        EXPECT_EQ(RunDueFrames(), 3);

        EXPECT_FALSE(service.IsStepDue());
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(responses[0].IsOk());
        EXPECT_EQ(order, (Vector<String>{"wait", "wait-done"}));
    }

    TEST_F(ControlServiceTest, ManualClockAnswersOnlyAfterTheLastFrame) {
        service.SetManualClock(true);
        RegisterDeferring("wait", 3);
        Post(MakeRequestText(1, "wait"));
        service.Drain();

        for (int frame = 1; frame <= 2; ++frame) {
            ASSERT_TRUE(service.IsStepDue());
            service.EndFrame();
            EXPECT_TRUE(responses.empty()) << "answered after frame " << frame;
        }
        ASSERT_TRUE(service.IsStepDue());
        service.EndFrame();
        EXPECT_EQ(responses.size(), 1u);
        EXPECT_FALSE(service.IsStepDue());
    }

    TEST_F(ControlServiceTest, ManualClockHoldsTheRequestsBehindAStepUntilItsFramesHaveRun) {
        service.SetManualClock(true);
        RegisterDeferring("wait", 2);
        RegisterRecording("after");
        Post(MakeRequestText(1, "wait"));
        Post(MakeRequestText(2, "after"));
        service.Drain();
        EXPECT_EQ(order, (Vector<String>{"wait"}));

        EXPECT_EQ(RunDueFrames(), 2);  // each frame drains: "after" stays behind the barrier until the second one ends
        EXPECT_EQ(order, (Vector<String>{"wait", "wait-done"}));

        service.Drain();
        EXPECT_EQ(order, (Vector<String>{"wait", "wait-done", "after"}));
        EXPECT_EQ(responses.back().IdText(), "2");
    }

    TEST_F(ControlServiceTest, TwoStepsInARowRunTheirFramesOneAfterTheOther) {
        service.SetManualClock(true);
        RegisterDeferring("wait", 2);
        Post(MakeRequestText(1, "wait"));
        Post(MakeRequestText(2, "wait"));
        service.Drain();

        EXPECT_EQ(RunDueFrames(), 2);
        EXPECT_FALSE(service.IsStepDue());  // the second step is still in the inbox: an idle cycle takes it up
        service.Drain();
        EXPECT_TRUE(service.IsStepDue());
        EXPECT_EQ(RunDueFrames(), 2);
        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(responses[0].IdText(), "1");
        EXPECT_EQ(responses[1].IdText(), "2");
    }

    TEST_F(ControlServiceTest, StepSecondsAreWhatTheCallAskedFor) {
        service.SetManualClock(true);
        service.RegisterHandler("slow", [](ControlCall& call) { call.DeferFrames(1, 0.125, [](ControlCall& done) { done.ReplyEmpty(); }); });
        Post(MakeRequestText(1, "slow"));
        service.Drain();

        ASSERT_TRUE(service.IsStepDue());
        EXPECT_DOUBLE_EQ(service.GetStepSeconds(), 0.125);
    }

    TEST_F(ControlServiceTest, StepSecondsDefaultToASixtiethOfASecond) {
        service.SetManualClock(true);
        RegisterDeferring("wait", 1);
        Post(MakeRequestText(1, "wait"));
        service.Drain();

        EXPECT_DOUBLE_EQ(service.GetStepSeconds(), kDefaultStepSeconds);
    }

    TEST_F(ControlServiceTest, NegativeAndNotANumberStepSecondsAreZero) {
        service.SetManualClock(true);
        const double notANumber = std::numeric_limits<double>::quiet_NaN();
        for (const double seconds : {-1.0, notANumber}) {
            service.RegisterHandler("odd", [seconds](ControlCall& call) { call.DeferFrames(1, seconds, [](ControlCall& done) { done.ReplyEmpty(); }); });
            Post(MakeRequestText(1, "odd"));
            service.Drain();

            ASSERT_TRUE(service.IsStepDue());
            EXPECT_EQ(service.GetStepSeconds(), 0.0);
            RunDueFrames();
        }
    }

    TEST_F(ControlServiceTest, ShutdownEndsAStepThatIsRunning) {
        service.SetManualClock(true);
        RegisterDeferring("wait", 100);
        Post(MakeRequestText(1, "wait"));
        service.Drain();
        ASSERT_TRUE(service.IsStepDue());

        service.Shutdown();

        EXPECT_FALSE(service.IsStepDue());
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(responses[0].ErrorCode(), "shutting_down");
    }

    /// Longer than any stall of a loaded machine, so a wait that ends before it was woken and did not time out
    constexpr std::chrono::minutes kWaitNobodySitsOut{2};

    TEST_F(ControlServiceTest, WaitForRequestsTimesOutWhenNothingIsPosted) {
        const auto start = std::chrono::steady_clock::now();
        service.WaitForRequests(std::chrono::milliseconds(30));
        EXPECT_GE(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(25));
    }

    TEST_F(ControlServiceTest, WaitForRequestsReturnsAtOnceWhenARequestIsWaiting) {
        Post(MakeRequestText(1, "ping"));

        const auto start = std::chrono::steady_clock::now();
        service.WaitForRequests(kWaitNobodySitsOut);
        EXPECT_LT(std::chrono::steady_clock::now() - start, kWaitNobodySitsOut);
    }

    TEST_F(ControlServiceTest, WaitForRequestsWakesWhenAnotherThreadPosts) {
        RegisterRecording("hello");
        std::thread client([this] { service.Post(MakeRequestText(1, "hello"), [](String) {}); });

        const auto start = std::chrono::steady_clock::now();
        service.WaitForRequests(kWaitNobodySitsOut);
        const auto waited = std::chrono::steady_clock::now() - start;
        client.join();

        EXPECT_LT(waited, kWaitNobodySitsOut);
        service.Drain();
        EXPECT_EQ(order, (Vector<String>{"hello"}));
    }

    TEST_F(ControlServiceTest, FrameEndHookOfTheSameNameIsReplaced) {
        service.SetFrameEndHook("a", [this] { order.push_back("first"); });
        service.SetFrameEndHook("a", [this] { order.push_back("second"); });

        service.EndFrame();

        EXPECT_EQ(order, (Vector<String>{"second"}));
    }

    TEST_F(ControlServiceTest, FrameEndHooksOfDifferentNamesRunInTheOrderTheyWereSet) {
        service.SetFrameEndHook("a", [this] { order.push_back("a"); });
        service.SetFrameEndHook("b", [this] { order.push_back("b"); });
        service.SetFrameEndHook("a", [this] { order.push_back("a2"); });  // keeps its place

        service.EndFrame();

        EXPECT_EQ(order, (Vector<String>{"a2", "b"}));
    }

    TEST_F(ControlServiceTest, AFrameEndHookMayAddAnotherHookWhichRunsInTheSameFrame) {
        service.SetFrameEndHook("outer", [this] {
            order.push_back("outer");
            service.SetFrameEndHook("inner", [this] { order.push_back("inner"); });
        });

        service.EndFrame();
        EXPECT_EQ(order, (Vector<String>{"outer", "inner"}));
        service.EndFrame();  // "inner" is set again, not added a second time
        EXPECT_EQ(order, (Vector<String>{"outer", "inner", "outer", "inner"}));
    }

    TEST_F(ControlServiceTest, StepAdvancesTheClockByTheFramesAskedFor) {
        RegisterControlBuiltins(service);
        service.SetManualClock(true);
        CoreManager::GetTime().Reset();

        Post(MakeRequestText(1, "step", R"({"dt":0.1,"frames":3})"));
        Post(MakeRequestText(2, "status"));
        service.Drain();
        EXPECT_TRUE(responses.empty());

        EXPECT_EQ(RunDueFrames(), 3);
        service.Drain();  // the status behind the step

        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(responses[0].IdText(), "1");
        ASSERT_TRUE(ResultOf(0).IsObject());
        EXPECT_EQ(IntOf(ResultOf(0), "frame").value_or(0), 3);
        EXPECT_NEAR(MemberOf(ResultOf(0), "totalTime").AsDouble().value_or(0.0), 0.3, 1e-9);
        EXPECT_NEAR(MemberOf(ResultOf(0), "deltaTime").AsDouble().value_or(0.0), 0.1, 1e-6);

        EXPECT_EQ(responses[1].IdText(), "2");
        EXPECT_EQ(IntOf(ResultOf(1), "frame").value_or(0), 3);
    }

    TEST_F(ControlServiceTest, StepWithoutParamsIsOneFrameOfASixtiethOfASecond) {
        RegisterControlBuiltins(service);
        service.SetManualClock(true);
        CoreManager::GetTime().Reset();

        Post(MakeRequestText(1, "step"));
        service.Drain();

        EXPECT_EQ(RunDueFrames(), 1);
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(IntOf(ResultOf(0), "frame").value_or(0), 1);
        EXPECT_NEAR(MemberOf(ResultOf(0), "totalTime").AsDouble().value_or(0.0), kDefaultStepSeconds, 1e-9);
    }

    TEST_F(ControlServiceTest, StepWithZeroSecondsRunsFramesWithoutMovingTheClock) {
        RegisterControlBuiltins(service);
        service.SetManualClock(true);
        CoreManager::GetTime().Reset();

        Post(MakeRequestText(1, "step", R"({"dt":0,"frames":2})"));
        service.Drain();

        EXPECT_EQ(RunDueFrames(), 2);
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(IntOf(ResultOf(0), "frame").value_or(0), 2);
        EXPECT_EQ(MemberOf(ResultOf(0), "totalTime").AsDouble().value_or(-1.0), 0.0);
    }

    TEST_F(ControlServiceTest, StepOnAPausedRealClockMakesSteppedFramesOfDt) {
        GamePause pause;
        service.SetPause(&pause);
        RegisterControlBuiltins(service);
        CoreManager::GetTime().Reset();
        pause.SetPaused(true);

        Post(MakeRequestText(1, "step", R"({"dt":0.125,"frames":3})"));

        const Vector<FrameKind> kinds = RunFramesUntilAnswered(pause, 1);

        // The frame that drains the request is a held one; the three after it are the steps
        EXPECT_EQ(kinds, (Vector<FrameKind>{FrameKind::Held, FrameKind::Stepped, FrameKind::Stepped, FrameKind::Stepped}));
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(IntOf(ResultOf(0), "frame").value_or(0), 3);
        EXPECT_EQ(MemberOf(ResultOf(0), "totalTime").AsDouble().value_or(0.0), 0.375);
        EXPECT_EQ(pause.GetPendingSteps(), 0u);
        EXPECT_TRUE(pause.IsPaused());
    }

    TEST_F(ControlServiceTest, StepOnAPausedRealClockCountsTheFrameThatDrainsItOnlyOnce) {
        GamePause pause;
        service.SetPause(&pause);
        RegisterControlBuiltins(service);
        CoreManager::GetTime().Reset();
        pause.SetPaused(true);
        Post(MakeRequestText(1, "status"));  // an earlier request in the same frame does not matter
        Post(MakeRequestText(2, "step"));
        Post(MakeRequestText(3, "step"));

        RunFramesUntilAnswered(pause, 3);

        ASSERT_EQ(responses.size(), 3u);
        EXPECT_EQ(IntOf(ResultOf(1), "frame").value_or(0), 1);
        EXPECT_EQ(IntOf(ResultOf(2), "frame").value_or(0), 2);
        EXPECT_EQ(pause.GetPendingSteps(), 0u);
    }

    TEST_F(ControlServiceTest, StepOnARunningGameCountsFramesAndIgnoresDt) {
        GamePause pause;
        service.SetPause(&pause);
        RegisterControlBuiltins(service);
        CoreManager::GetTime().Reset();

        Post(MakeRequestText(1, "step", R"({"dt":0.125,"frames":3})"));

        const Vector<FrameKind> kinds = RunFramesUntilAnswered(pause, 1);

        EXPECT_EQ(kinds, (Vector<FrameKind>{FrameKind::Running, FrameKind::Running, FrameKind::Running}));
        EXPECT_EQ(pause.GetPendingSteps(), 0u);
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(MemberOf(ResultOf(0), "totalTime").AsDouble().value_or(0.0), 3 * kRealFrameSeconds);
    }

    TEST_F(ControlServiceTest, PauseAndResumeToggleTheStatus) {
        GamePause pause;
        service.SetPause(&pause);
        RegisterControlBuiltins(service);

        Post(MakeRequestText(1, "status"));
        Post(MakeRequestText(2, "pause"));
        Post(MakeRequestText(3, "status"));
        Post(MakeRequestText(4, "resume"));
        Post(MakeRequestText(5, "status"));
        service.Drain();

        ASSERT_EQ(responses.size(), 5u);
        EXPECT_FALSE(BoolOf(ResultOf(0), "paused").value_or(true));
        EXPECT_TRUE(BoolOf(ResultOf(1), "paused").value_or(false));
        EXPECT_TRUE(BoolOf(ResultOf(2), "paused").value_or(false));
        EXPECT_FALSE(BoolOf(ResultOf(3), "paused").value_or(true));
        EXPECT_FALSE(BoolOf(ResultOf(4), "paused").value_or(true));
        EXPECT_FALSE(pause.IsPaused());
    }

    TEST_F(ControlServiceTest, StatusOfAServiceWithoutAPauseIsNotPaused) {
        RegisterControlBuiltins(service);
        Post(MakeRequestText(1, "status"));
        Post(MakeRequestText(2, "pause"));
        service.Drain();

        ASSERT_EQ(responses.size(), 2u);
        EXPECT_FALSE(BoolOf(ResultOf(0), "paused").value_or(true));
        EXPECT_EQ(responses[1].ErrorCode(), "internal_error");
    }

    TEST_F(ControlServiceTest, ResumeWithTheManualClockFailsAndPauseAnswersOk) {
        GamePause pause;
        service.SetPause(&pause);
        service.SetManualClock(true);
        pause.SetPaused(true);
        RegisterControlBuiltins(service);

        Post(MakeRequestText(1, "resume"));
        Post(MakeRequestText(2, "pause"));
        Post(MakeRequestText(3, "status"));
        service.Drain();

        ASSERT_EQ(responses.size(), 3u);
        EXPECT_EQ(responses[0].ErrorCode(), "manual_clock");
        EXPECT_TRUE(BoolOf(ResultOf(1), "paused").value_or(false));
        EXPECT_TRUE(BoolOf(ResultOf(2), "paused").value_or(false));
        EXPECT_TRUE(pause.IsPaused());
    }

    TEST_F(ControlServiceTest, StepWhilePausedAdvancesTheFrameByNAndStaysPaused) {
        GamePause pause;
        service.SetPause(&pause);
        RegisterControlBuiltins(service);
        CoreManager::GetTime().Reset();

        Post(MakeRequestText(1, "pause"));
        service.Drain();
        ASSERT_TRUE(pause.IsPaused());

        Post(MakeRequestText(2, "step", R"({"frames":4})"));
        Post(MakeRequestText(3, "status"));
        RunFramesUntilAnswered(pause, 3);

        ASSERT_EQ(responses.size(), 3u);
        EXPECT_EQ(IntOf(ResultOf(1), "frame").value_or(0), 4);
        EXPECT_EQ(IntOf(ResultOf(2), "frame").value_or(0), 4);
        EXPECT_TRUE(BoolOf(ResultOf(2), "paused").value_or(false));
    }

    TEST_F(ControlServiceTest, ManualClockStepsThroughThePause) {
        GamePause pause;
        service.SetPause(&pause);
        service.SetManualClock(true);
        pause.SetPaused(true);
        RegisterControlBuiltins(service);
        CoreManager::GetTime().Reset();

        Post(MakeRequestText(1, "step", R"({"dt":0.125,"frames":2})"));
        service.Drain();

        EXPECT_TRUE(pause.IsStepDue());
        EXPECT_EQ(pause.GetPendingSteps(), 2u);
        EXPECT_EQ(pause.GetStepSeconds(), 0.125);

        const Vector<FrameKind> kinds = RunFramesUntilAnswered(pause, 1);

        EXPECT_EQ(kinds, (Vector<FrameKind>{FrameKind::Stepped, FrameKind::Stepped}));
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(IntOf(ResultOf(0), "frame").value_or(0), 2);
        EXPECT_EQ(MemberOf(ResultOf(0), "totalTime").AsDouble().value_or(0.0), 0.25);
        EXPECT_FALSE(pause.IsStepDue());
    }

    TEST_F(ControlServiceTest, ManualClockDeferredCallOfZeroSecondsStepsTheFrameCounterOnly) {
        GamePause pause;
        service.SetPause(&pause);
        service.SetManualClock(true);
        pause.SetPaused(true);
        CoreManager::GetTime().Reset();
        service.RegisterHandler("capture", [](ControlCall& call) { call.DeferFrames(1, 0.0, [](ControlCall& done) { done.ReplyEmpty(); }); });
        Post(MakeRequestText(1, "capture"));
        service.Drain();

        ASSERT_TRUE(pause.IsStepDue());
        const Vector<FrameKind> kinds = RunFramesUntilAnswered(pause, 1);

        EXPECT_EQ(kinds, (Vector<FrameKind>{FrameKind::Stepped}));
        EXPECT_EQ(CoreManager::GetTime().GetFrameCount(), 1u);
        EXPECT_EQ(CoreManager::GetTime().GetTotalTime(), 0.0);
    }

    TEST_F(ControlServiceTest, ADeferredCallOnAPausedRealClockNeedsNoStep) {
        GamePause pause;
        service.SetPause(&pause);
        pause.SetPaused(true);
        service.RegisterHandler("capture", [](ControlCall& call) { call.DeferFrames(1, 0.0, [](ControlCall& done) { done.ReplyEmpty(); }); });
        Post(MakeRequestText(1, "capture"));

        const Vector<FrameKind> kinds = RunFramesUntilAnswered(pause, 1);

        EXPECT_EQ(kinds, (Vector<FrameKind>{FrameKind::Held}));
        EXPECT_EQ(pause.GetPendingSteps(), 0u);
    }

    TEST_F(ControlServiceTest, StepTakesTheLargestFrameCountAndTheLongestFrame) {
        RegisterControlBuiltins(service);
        service.SetManualClock(true);

        const String params = Format(R"({{"dt":{},"frames":{}}})", Time::kMaxFrameTime, kMaxStepFrames);
        Post(MakeRequestText(1, "step", params));
        service.Drain();

        EXPECT_TRUE(service.IsStepDue());
        EXPECT_DOUBLE_EQ(service.GetStepSeconds(), Time::kMaxFrameTime);
        EXPECT_TRUE(responses.empty());  // accepted: not answered with an error
    }

    TEST_F(ControlServiceTest, StepRefusesParamsThatAreNotInRange) {
        RegisterControlBuiltins(service);
        service.SetManualClock(true);

        const Vector<String> bad = {
            R"({"frames":0})",
            R"({"frames":-3})",
            Format(R"({{"frames":{}}})", static_cast<int64_t>(kMaxStepFrames) + 1),
            R"({"frames":1.5})",
            R"({"frames":"3"})",
            R"({"frames":null})",
            R"({"dt":-0.1})",
            Format(R"({{"dt":{}}})", Time::kMaxFrameTime + 0.01),
            R"({"dt":"fast"})",
            R"({"dt":null})",
        };
        int64_t id = 0;
        for (const String& params : bad) {
            Post(MakeRequestText(++id, "step", params));
        }
        service.Drain();

        ASSERT_EQ(responses.size(), bad.size());
        for (size_t i = 0; i < responses.size(); ++i) {
            EXPECT_FALSE(responses[i].IsOk()) << bad[i].c_str();
            EXPECT_EQ(responses[i].ErrorCode(), "invalid_params") << bad[i].c_str();
            EXPECT_FALSE(responses[i].ErrorMessage().empty());
        }
        EXPECT_FALSE(service.IsStepDue());  // a refused step asks for no frame
    }

    TEST_F(ControlServiceTest, StepOnTheRealClockCountsTheFramesTheLoopRunsAnyway) {
        RegisterControlBuiltins(service);
        CoreManager::GetTime().Reset();

        Post(MakeRequestText(1, "step", R"({"dt":0.2,"frames":2})"));  // ignored: the frames take their own time
        service.Drain();

        EXPECT_FALSE(service.IsStepDue());
        CoreManager::GetTime().Advance(0.01);
        service.EndFrame();
        EXPECT_TRUE(responses.empty());
        CoreManager::GetTime().Advance(0.01);
        service.EndFrame();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(IntOf(ResultOf(0), "frame").value_or(0), 2);
        EXPECT_NEAR(MemberOf(ResultOf(0), "totalTime").AsDouble().value_or(0.0), 0.02, 1e-9);
    }

    TEST_F(ControlServiceTest, QuitEndsAStepOfManyFrames) {
        RegisterControlBuiltins(service);
        service.SetManualClock(true);
        QuitCounter counter;

        const String params = Format(R"({{"frames":{}}})", kMaxStepFrames);
        Post(MakeRequestText(1, "step", params));
        service.Drain();
        Post(MakeRequestText(2, "quit"));
        service.Drain();

        EXPECT_EQ(counter.count, 1);  // a step of ten minutes does not make the application unquittable
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(responses[0].IdText(), "2");
    }

    /// The control service of the CoreManager, whose built-in methods InitializeCoreServices() registers
    class ControlServiceCoreTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }
    };

    TEST_F(ControlServiceCoreTest, CoreManagerHasTheBuiltinMethods) {
        ControlService& core = CoreManager::GetControlService();
        Vector<ControlReply> coreResponses;
        core.Post(MakeRequestText(1, "ping"), [&](String text) { coreResponses.emplace_back(StringView(text.c_str(), text.size())); });
        core.Drain();

        ASSERT_EQ(coreResponses.size(), 1u);
        EXPECT_TRUE(coreResponses[0].IsOk());
    }

}  // namespace BECore::Tests
