#pragma once

#include "FuzzMutator.h"

namespace BECore::Fuzz {

    /// The environment variable that makes FuzzCorpusTest.WriteSeeds write the generated seeds into the folder it names
    inline constexpr const char* kWriteSeedsVariable = "BAGI_FUZZ_WRITE_SEEDS";

    /// A seed file: its name inside the folder of its target and its bytes
    struct FuzzSeedFile {
        std::filesystem::path folder;  ///< Relative to the corpus root (the corpusFolder of the target)
        std::string name;
        FuzzBytes bytes;
    };

    /**
     * @brief The seeds that are made by the engine's own writers (binary streams, later saves and protocol messages)
     *
     * Real inputs, not hand-typed bytes: when a format changes the seeds are made again, and the files under
     * tests/Fuzz/corpus change with it. A seed that came from a finding (fix-<what>) is never made here.
     */
    Vector<FuzzSeedFile> MakeGeneratedSeeds();

    /// The seeds of room_protocol, room_hub and control_request (FuzzRoomSeeds.cpp); MakeGeneratedSeeds adds them to its own
    void AddRoomAndControlSeeds(Vector<FuzzSeedFile>& seeds);

    /// The seeds of web_ui and ui_tables (FuzzUiSeeds.cpp); MakeGeneratedSeeds adds them to its own
    void AddUiSeeds(Vector<FuzzSeedFile>& seeds);

    /// Writes every generated seed under @p corpusRoot (<corpusRoot>/<folder>/<name>); returns false if a file cannot be written
    bool WriteGeneratedSeeds(const std::filesystem::path& corpusRoot);

}  // namespace BECore::Fuzz
