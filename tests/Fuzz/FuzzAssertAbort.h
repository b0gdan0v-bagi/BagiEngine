#pragma once

namespace BECore::Fuzz {

    /**
     * @brief Makes an engine ASSERT / EXPECT / FATALERROR that an input reached stop the process
     *
     * A libFuzzer process has no gtest, so nobody listens to AssertEvent and the engine would carry on past a failed assertion. The
     * message goes to stderr and the process aborts, so the input that did it is saved as a crash. Safe to call twice.
     */
    void InstallAssertAbort();

}  // namespace BECore::Fuzz
