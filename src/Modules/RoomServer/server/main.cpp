// The room server executable: a registry of the demo games and RunRoomServer(). A product game writes its own main the same way,
// with its own ISeatedGame in the registry instead (RoomServer/CLAUDE.md, "A server main")

#include <Games/SeatedGameRegistry.h>
#include <RoomServer/DemoCardGame.h>
#include <RoomServer/RoomServerMain.h>
#include <cstdio>

int main(int argc, char* argv[]) {
    using namespace BECore;

    const auto args = ParseRoomServerArgs(argc, argv);
    if (!args) {
        std::fprintf(stderr, "BagiRoomServer: %s\n", args.error().c_str());
        return 1;
    }
    if (args->help) {
        std::printf("%s", GetRoomServerUsage().c_str());
        return 0;
    }

    SeatedGameRegistry registry;
    if (!args->testGames) {
        std::fprintf(stderr, "BagiRoomServer: this executable has no product game; start it with --test-games (a game brings its own server main)\n");
        return 1;
    }
    registry.Register(SeatedGameInfo{"cards", DemoCardGame::kSeatCount}, [] { return eastl::make_unique<DemoCardGame>(); });
    registry.Register(SeatedGameInfo{"cards-leaky", DemoCardGame::kSeatCount}, [] { return eastl::make_unique<LeakyDemoCardGame>(); });

    return RunRoomServer(registry, *args);
}
