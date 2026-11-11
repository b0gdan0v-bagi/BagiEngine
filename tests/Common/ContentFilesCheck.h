#pragma once

#include <BECore/Config/ContentVersion.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /**
     * The content files (`BECore/Config/ContentVersion.h`) under @p folder whose version is not kContentVersion, one
     * message per file: the path, then what CheckContentVersion says. A file that does not parse, or whose root is
     * not a content root, is not a content file and is skipped. Sorted by path.
     */
    inline std::vector<std::string> FindStaleContentFiles(const std::filesystem::path& folder) {
        std::vector<std::string> stale;
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) || it->path().extension() != ".xml") {
                continue;
            }
            pugi::xml_document document;
            if (!document.load_file(it->path().c_str()) || !IsContentRoot(document.document_element().name())) {
                continue;
            }
            const std::string path = it->path().generic_string();
            if (const auto version = CheckContentVersion(document.document_element(), path.c_str()); !version) {
                stale.emplace_back(version.error().c_str());
            }
        }
        std::ranges::sort(stale);
        return stale;
    }

    /**
     * Fails the test once per content file under @p configFolder that is not at the engine's current content version. A
     * game calls it on its own `config/` (`ExpectContentFilesAtCurrentVersion(sourceDir / "config")`), the path taken
     * from `__FILE__` as the game's other tests do; the message says how to migrate the file.
     */
    inline void ExpectContentFilesAtCurrentVersion(const std::filesystem::path& configFolder) {
        EXPECT_TRUE(std::filesystem::is_directory(configFolder)) << "no folder " << configFolder.string();
        for (const std::string& message : FindStaleContentFiles(configFolder)) {
            ADD_FAILURE() << message;
        }
    }

}  // namespace BECore::Tests
