// The build-time tool of BagiEngineTests: compiles the formulas of the test set and writes the C++ of their native functions
// (`be_add_native_programs`, cmake/BagiEngine.cmake). A game writes its own tool the same way over its content.

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Values/Programs/Native/NativeProgramsFlags.h>
#include <NativePrograms/NativeProgramSet.h>
#include <cstdio>

int main(int argc, char* argv[]) {
    using namespace BECore;

    Vector<String> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    CoreManager::GetCommandLine().Set(arguments);

    const auto programs = Tests::CompileNativeProgramSet();
    if (!programs.has_value()) {
        std::fprintf(stderr, "BagiNativeProgramsTool: a formula of the set does not compile: %s\n", programs.error().c_str());
        return 1;
    }

    const NativeCodegenOptions options{Tests::kNativeProgramSetName};
    const auto emitted = EmitNativeProgramsIfAsked(options, Span<const ProgramId>(programs->data(), programs->size()));
    if (!emitted.has_value()) {
        std::fprintf(stderr, "BagiNativeProgramsTool: needs %s=<file>\n", kEmitNativeProgramsFlag.data());
        return 1;
    }
    std::fprintf(emitted->exitCode == 0 ? stdout : stderr, "BagiNativeProgramsTool: %s\n", emitted->message.c_str());
    return emitted->exitCode;
}
