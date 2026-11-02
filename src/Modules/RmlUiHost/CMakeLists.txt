# RmlUiHost Module CMake Configuration
# The host of the vendored RmlUi (src/Modules/RmlUi): the render, system and file interfaces over the engine's services, and the scene
# component that draws one RmlUi document. Added by cmake/BagiEngine.cmake only when ENGINE_UI_RMLUI is ON.
# The component registers in a static initializer, so executables and test executables link it WHOLE_ARCHIVE (be_link_engine does it).

be_add_module(RmlUiHost LINK
    BECoreModule
    EventsModule
    RmlUi::Core
)
be_add_game_reflection(RmlUiHostModule)
