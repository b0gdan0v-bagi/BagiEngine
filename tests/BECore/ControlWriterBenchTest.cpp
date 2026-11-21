#include <BECore/Control/ControlCall.h>
#include <BECore/Control/Json.h>
#include <BECore/Control/JsonWriter.h>
#include <Common/AllocCheck.h>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>

namespace BECore::Tests {

    namespace {

        constexpr int kIterations = 2000;
        constexpr int kLargeMembers = 40;
        constexpr int kLargeLevels = 3;

        Json MakeStatusResult() {
            Json result = Json::object();
            result["frame"] = 120;
            result["totalTime"] = 2.0;
            result["deltaTime"] = 0.016666666666666666;
            result["fixedDeltaTime"] = 0.016666666666666666;
            result["manualClock"] = false;
            result["paused"] = false;
            result["log"] = Json{{"errors", 0}, {"warnings", 3}};
            result["window"] = Json{{"width", 1280}, {"height", 720}, {"density", 2.0}};
            return result;
        }

        void WriteStatusResult(JsonWriter& writer) {
            writer.BeginObject();
            writer.Member("deltaTime", 0.016666666666666666);
            writer.Member("fixedDeltaTime", 0.016666666666666666);
            writer.Member("frame", 120);
            writer.Key("log");
            writer.BeginObject();
            writer.Member("errors", 0);
            writer.Member("warnings", 3);
            writer.EndObject();
            writer.Member("manualClock", false);
            writer.Member("paused", false);
            writer.Member("totalTime", 2.0);
            writer.Key("window");
            writer.BeginObject();
            writer.Member("density", 2.0);
            writer.Member("height", 720);
            writer.Member("width", 1280);
            writer.EndObject();
            writer.EndObject();
        }

        /// Member names that sort the same as numbers, so the writer path can write them in the DOM's order
        String LargeKey(int index) {
            return Format("member_{:02}", index);
        }

        Json MakeLargeResult(int level) {
            Json result = Json::object();
            for (int i = 0; i < kLargeMembers; ++i) {
                const String key = LargeKey(i);
                if (level + 1 < kLargeLevels && i % 10 == 0) {
                    result[key.c_str()] = MakeLargeResult(level + 1);
                } else if (i % 3 == 0) {
                    result[key.c_str()] = i * 1000 + level;
                } else if (i % 3 == 1) {
                    result[key.c_str()] = i * 0.37;
                } else {
                    result[key.c_str()] = "text value of a member";
                }
            }
            return result;
        }

        void WriteLargeResult(JsonWriter& writer, int level) {
            writer.BeginObject();
            for (int i = 0; i < kLargeMembers; ++i) {
                const String key = LargeKey(i);
                if (level + 1 < kLargeLevels && i % 10 == 0) {
                    writer.Key(key);
                    WriteLargeResult(writer, level + 1);
                } else if (i % 3 == 0) {
                    writer.Member(key, i * 1000 + level);
                } else if (i % 3 == 1) {
                    writer.Member(key, i * 0.37);
                } else {
                    writer.Member(key, StringView("text value of a member"));
                }
            }
            writer.EndObject();
        }

        struct Measure {
            uint64_t allocs = 0;
            uint64_t bytes = 0;
            double ns = 0.0;
        };

        template <typename TReply>
        Measure MeasureReply(TReply&& reply) {
            reply();  // the first message sizes every buffer the second one reuses
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < kIterations; ++i) {
                reply();
            }
            const auto stop = std::chrono::steady_clock::now();
            Measure measure;
            measure.ns = std::chrono::duration<double, std::nano>(stop - start).count() / kIterations;
            const AllocCount allocs = CountAllocations(reply);
            measure.allocs = allocs.count;
            measure.bytes = allocs.bytes;
            return measure;
        }

        void Print(const char* name, const Measure& measure) {
            std::cout << "ControlReply " << name << " allocs=" << measure.allocs << " bytes=" << measure.bytes << " ns=" << static_cast<uint64_t>(measure.ns) << std::endl;
        }

        /// The reference: the DOM path of before the writer (a `Json` envelope, `dump`, a copy into the `String` the transport gets)
        template <typename TMakeResult>
        Measure MeasureDomPath(TMakeResult&& makeResult) {
            String sent;
            const Measure measure = MeasureReply([&] {
                Json response = Json::object();
                response["id"] = 7;
                response["ok"] = true;
                response["result"] = makeResult();
                const std::string text = response.dump();  //-NoStd: the reference measures nlohmann's own dump
                sent.assign(text.data(), text.size());
            });
            EXPECT_FALSE(sent.empty());
            return measure;
        }

        template <typename TWriteResult>
        Measure MeasureWriterPath(TWriteResult&& writeResult) {
            String sent;
            const Json id = 7;
            const Measure measure = MeasureReply([&] {
                sent.clear();
                AppendOkResponse(sent, id, writeResult);
            });
            EXPECT_FALSE(sent.empty());
            return measure;
        }

    }  // namespace

    /// Run by hand: `BagiEngineTests --gtest_also_run_disabled_tests --gtest_filter=ControlWriterBenchTest.DISABLED_*`; the counts need macos-profile
    TEST(ControlWriterBenchTest, DISABLED_ControlReply) {
        Print("status-dom", MeasureDomPath([] { return MakeStatusResult(); }));
        Print("status-writer", MeasureWriterPath([](JsonWriter& writer) { WriteStatusResult(writer); }));
        Print("large-dom", MeasureDomPath([] { return MakeLargeResult(0); }));
        Print("large-writer", MeasureWriterPath([](JsonWriter& writer) { WriteLargeResult(writer, 0); }));
    }

}  // namespace BECore::Tests
