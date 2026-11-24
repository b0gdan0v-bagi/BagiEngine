// What reading a message costs by the two paths that exist while the callers move off the DOM: `ParseJson` and a `JsonDeserializer` over the DOM (the
// bridge, which writes the DOM as text and parses it again), and one kept `JsonDeserializer` that loads the text and reads from the tape. Both read
// into the same object, so the container capacities the object keeps are the same on both sides. Prints one line per case and asserts nothing about
// time; the allocations need macos-profile.
//
// Run by hand: `BagiEngineTests --gtest_also_run_disabled_tests --gtest_filter=JsonReadBenchTest.DISABLED_*`

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/JsonReadBenchTypes.h>
#include <BECore/Rooms/RoomMessageReference.h>
#include <Common/AllocCheck.h>
#include <Generated/JsonReadBenchTypes.gen.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>

namespace BECore::Tests {

    namespace {

        constexpr int kIterations = 5000;
        constexpr int kListedRooms = 20;
        constexpr int kLobbySeats = 4;

        struct Measure {
            uint64_t allocs = 0;
            uint64_t bytes = 0;
            double microseconds = 0.0;
        };

        template <typename TRead>
        Measure MeasureRead(TRead&& read) {
            read();  // the first read sizes every buffer and container the next ones reuse
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < kIterations; ++i) {
                read();
            }
            const auto stop = std::chrono::steady_clock::now();
            Measure measure;
            measure.microseconds = std::chrono::duration<double, std::micro>(stop - start).count() / kIterations;
            const AllocCount allocs = CountAllocations(read);
            measure.allocs = allocs.count;
            measure.bytes = allocs.bytes;
            return measure;
        }

        void Print(const char* name, const char* path, const Measure& measure) {
            std::cout << "JsonRead " << name << ' ' << path << " allocs=" << measure.allocs << " bytes=" << measure.bytes << " us=" << measure.microseconds << std::endl;
        }

        /// Reads @p text into one @p T by each path and prints the two lines
        template <typename T>
        void Compare(const char* name, StringView text) {
            T viaDom;
            Print(name, "dom", MeasureRead([&] {
                      auto parsed = ParseJson(text);
                      EXPECT_TRUE(parsed.has_value());
                      JsonDeserializer reader(*parsed);
                      viaDom.Deserialize(reader);
                      EXPECT_FALSE(reader.HasErrors());
                  }));

            T viaTape;
            JsonDeserializer reader;
            Print(name, "tape", MeasureRead([&] {
                      EXPECT_TRUE(reader.Load(text));
                      viaTape.Deserialize(reader);
                      EXPECT_FALSE(reader.HasErrors());
                  }));
        }

        LobbySnapshot MakeBenchLobby() {
            LobbySnapshot lobby;
            lobby.room = "ABCDE";
            lobby.game = "cards";
            lobby.phase = RoomPhase::Lobby;
            lobby.hostSeat = 0;
            lobby.seats = {{0, SeatKind::Human, "Ann", true}, {1, SeatKind::Human, "Bob", false}, {2, SeatKind::Computer, {}, false}, {3, SeatKind::Empty, {}, false}};
            lobby.options = R"({"fast":true,"rounds":3})";
            return lobby;
        }

        RoomListMessage MakeBenchRoomList() {
            RoomListMessage list;
            list.game = "cards";
            for (int i = 0; i < kListedRooms; ++i) {
                RoomListing listing;
                listing.room = Format("ROOM{}", i % 10);
                listing.host = "Host name";
                listing.taken = i % kLobbySeats;
                listing.seats = kLobbySeats;
                listing.started = i % 2 == 0;
                listing.options = R"({"fast":true,"rounds":3})";
                list.rooms.push_back(listing);
            }
            return list;
        }

    }  // namespace

    TEST(JsonReadBenchTest, DISABLED_ControlRequest) {
        constexpr StringView kRequest =
            R"({"id":41,"method":"perform_action","params":{"action":"click","target":"play_button","x":412.5,"y":96.25,"button":1,"pressed":true,"note":"from the qa charter"}})";
        Compare<ReadBenchControlRequest>("control-request", kRequest);
    }

    TEST(JsonReadBenchTest, DISABLED_RoomMessages) {
        const String lobby = RoomMessageReference::MakeLobbyMessage(MakeBenchLobby());
        const String rooms = RoomMessageReference::MakeRoomListMessage(MakeBenchRoomList());
        Compare<ReadBenchLobby>("room-lobby", lobby);
        Compare<ReadBenchRoomList>("room-list", rooms);
    }

}  // namespace BECore::Tests
