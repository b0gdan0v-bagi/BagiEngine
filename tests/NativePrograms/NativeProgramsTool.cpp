// The build-time tool of BagiEngineTests: compiles the formulas of the test set and writes the C++ of their native functions
// (`be_add_native_programs`, cmake/BagiEngine.cmake). A game writes its own tool the same way over its content.

#include <BECore/Values/Programs/Native/ProgramCodegen.h>
#include <NativePrograms/NativeProgramSet.h>
#include <cstdio>
#include <cstring>

namespace {

    constexpr const char* kEmitFlag = "--emit-native-programs=";

    /// Writes @p text unless the file already holds it, so a regeneration that changed nothing leaves the modification time alone and the compile of the file is skipped
    bool WriteIfChanged(const BECore::String& path, const BECore::String& text) {
        if (std::FILE* old = std::fopen(path.c_str(), "rb")) {
            BECore::String current;
            char buffer[4096];
            for (size_t count = std::fread(buffer, 1, sizeof(buffer), old); count > 0; count = std::fread(buffer, 1, sizeof(buffer), old)) {
                current.append(buffer, count);
            }
            std::fclose(old);
            if (current == text) {
                return true;
            }
        }
        std::FILE* file = std::fopen(path.c_str(), "wb");
        if (file == nullptr) {
            return false;
        }
        const size_t written = std::fwrite(text.data(), 1, text.size(), file);
        return std::fclose(file) == 0 && written == text.size();
    }

}  // namespace

int main(int argc, char* argv[]) {
    using namespace BECore;

    const size_t flagLength = std::strlen(kEmitFlag);
    String path;
    for (int index = 1; index < argc; ++index) {
        if (std::strncmp(argv[index], kEmitFlag, flagLength) == 0) {
            path = argv[index] + flagLength;
        }
    }
    if (path.empty()) {
        std::fprintf(stderr, "BagiNativeProgramsTool: needs %s<file>\n", kEmitFlag);
        return 1;
    }

    const auto programs = Tests::CompileNativeProgramSet();
    if (!programs.has_value()) {
        std::fprintf(stderr, "BagiNativeProgramsTool: a formula of the set does not compile: %s\n", programs.error().c_str());
        return 1;
    }

    const String text = GenerateNativeSource(Span<const ProgramId>(programs->data(), programs->size()), NativeCodegenOptions{Tests::kNativeProgramSetName});
    if (!WriteIfChanged(path, text)) {
        std::fprintf(stderr, "BagiNativeProgramsTool: cannot write '%s'\n", path.c_str());
        return 1;
    }
    std::printf("BagiNativeProgramsTool: %zu programs in '%s'\n", programs->size(), path.c_str());
    return 0;
}
