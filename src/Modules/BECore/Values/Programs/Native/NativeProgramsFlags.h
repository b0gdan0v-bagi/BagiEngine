#pragma once

#include <BECore/Application/CommandLine.h>
#include <BECore/Types/Types.h>
#include <BECore/Values/Programs/Native/ProgramCodegen.h>
#include <BECore/Values/Programs/Program.h>
#include <expected>

namespace BECore {

    /// Writes the generated source of the program's native functions and ends the program (`--emit-native-programs=<file>`)
    inline constexpr StringView kEmitNativeProgramsFlag = "--emit-native-programs";

    /// Runs every program in the interpreter, whatever generated code is linked (`--interpreted`): the reference run of a comparison
    inline constexpr StringView kInterpretedFlag = "--interpreted";

    /// What a tool that was asked to write the generated source says and how it ends
    struct NativeProgramsEmitResult {
        int exitCode = 0;
        /// One line for the console: the count and the file, or why nothing was written
        String message;
    };

    /**
     * @brief Writes the generated source of @p programs to @p path and returns how many programs were given (equal programs are written once)
     *
     * The file is left alone (not even its modification time changes) when it already holds the text, so a build that regenerates it without a change compiles
     * nothing. An error is the text for the user: the file that could not be written.
     */
    [[nodiscard]] std::expected<size_t, String> WriteNativePrograms(StringView path, Span<const ProgramId> programs, const NativeCodegenOptions& options);

    /**
     * @brief The flag `--emit-native-programs=<file>`: writes the source over every program of the pool, or over @p programs when it is not empty
     *
     * A tool calls it after it loaded its content, ends with the result's `exitCode` when it returns a value and goes on otherwise. The flag with no file
     * (`--emit-native-programs`, `--emit-native-programs=`) is an error result with a non-zero code, never a crash and never a file named after nothing.
     */
    [[nodiscard]] Optional<NativeProgramsEmitResult> EmitNativeProgramsIfAsked(const CommandLine& line, const NativeCodegenOptions& options, Span<const ProgramId> programs = {});

    /// `EmitNativeProgramsIfAsked` over `CoreManager::GetCommandLine()`
    [[nodiscard]] Optional<NativeProgramsEmitResult> EmitNativeProgramsIfAsked(const NativeCodegenOptions& options, Span<const ProgramId> programs = {});

    /**
     * @brief `--interpreted` turns the generated functions off; the miss warning turns on when generated code is linked
     *
     * Call it before the content loads: the warning is given when a program is interned and finds no function (`NativePrograms::SetWarnOnMiss`), so a program
     * loaded earlier is counted by `NativePrograms::LogSummary` but not warned about.
     */
    void ApplyNativeProgramsFlags(const CommandLine& line);

    /// `ApplyNativeProgramsFlags` over `CoreManager::GetCommandLine()`
    void ApplyNativeProgramsFlags();

}  // namespace BECore
