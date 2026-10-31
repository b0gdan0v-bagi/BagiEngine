# Games Module CMake Configuration
# Infrastructure for self-contained games (IGame, GameManager, registry). Concrete games live outside the engine.

be_add_module(Games LINK
    BECoreModule
    EventsModule
)
