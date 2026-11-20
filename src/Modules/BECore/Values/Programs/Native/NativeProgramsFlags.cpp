#include "NativeProgramsFlags.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Values/Programs/Native/NativePrograms.h>
#include <cerrno>
#include <cstdio>

namespace BECore {

    namespace {

        constexpr size_t kReadChunk = 4096;
        constexpr int kFailure = 1;

        bool HoldsText(const String& fileName, const String& text) {
            std::FILE* file = std::fopen(fileName.c_str(), "rb");  //-NoStd: the generated source is a build artifact outside the engine's file layer
            if (file == nullptr) {
                return false;
            }
            String current;
            char chunk[kReadChunk];
            for (size_t count = std::fread(chunk, 1, sizeof(chunk), file); count > 0; count = std::fread(chunk, 1, sizeof(chunk), file)) {
                current.append(chunk, count);
            }
            std::fclose(file);
            return current == text;
        }

        Vector<ProgramId> AllPrograms() {
            Vector<ProgramId> programs(ProgramPool::Count());
            for (size_t index = 0; index < programs.size(); ++index) {
                programs[index] = static_cast<ProgramId>(index);
            }
            return programs;
        }

    }  // namespace

    std::expected<size_t, String> WriteNativePrograms(StringView path, Span<const ProgramId> programs, const NativeCodegenOptions& options) {
        const String fileName(path.data(), path.size());
        const String text = GenerateNativeSource(programs, options);
        if (HoldsText(fileName, text)) {
            return programs.size();
        }
        std::FILE* file = std::fopen(fileName.c_str(), "wb");  //-NoStd: see HoldsText
        if (file == nullptr) {
            return std::unexpected(Format("cannot write '{}': {}", fileName, std::strerror(errno)));
        }
        const size_t written = std::fwrite(text.data(), 1, text.size(), file);
        if (std::fclose(file) != 0 || written != text.size()) {
            return std::unexpected(Format("cannot write '{}': {}", fileName, std::strerror(errno)));
        }
        return programs.size();
    }

    Optional<NativeProgramsEmitResult> EmitNativeProgramsIfAsked(const CommandLine& line, const NativeCodegenOptions& options, Span<const ProgramId> programs) {
        const Optional<StringView> path = line.FindValue(kEmitNativeProgramsFlag);
        if (!path.has_value() && !line.HasFlag(kEmitNativeProgramsFlag)) {
            return std::nullopt;
        }
        if (!path.has_value() || path->empty()) {
            return NativeProgramsEmitResult{kFailure, Format("{} needs the file to write: {}=<file>", kEmitNativeProgramsFlag, kEmitNativeProgramsFlag)};
        }
        const Vector<ProgramId> all = programs.empty() ? AllPrograms() : Vector<ProgramId>{};
        const Span<const ProgramId> chosen = programs.empty() ? Span<const ProgramId>(all.data(), all.size()) : programs;
        const auto written = WriteNativePrograms(*path, chosen, options);
        if (!written.has_value()) {
            return NativeProgramsEmitResult{kFailure, written.error()};
        }
        return NativeProgramsEmitResult{0, Format("Native programs: {} programs written to '{}'", *written, *path)};
    }

    Optional<NativeProgramsEmitResult> EmitNativeProgramsIfAsked(const NativeCodegenOptions& options, Span<const ProgramId> programs) {
        return EmitNativeProgramsIfAsked(CoreManager::GetCommandLine(), options, programs);
    }

    void ApplyNativeProgramsFlags(const CommandLine& line) {
        if (line.HasFlag(kInterpretedFlag)) {
            NativePrograms::SetEnabled(false);
        }
        NativePrograms::SetWarnOnMiss(NativePrograms::HasGeneratedCode());
    }

    void ApplyNativeProgramsFlags() {
        ApplyNativeProgramsFlags(CoreManager::GetCommandLine());
    }

}  // namespace BECore
