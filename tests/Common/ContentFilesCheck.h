#pragma once

#include <BECore/Config/ContentVersion.h>
#include <BECore/File/Path.h>
#include <BECore/Types/Types.h>
#include <Common/Printers.h>
#include <Common/TestFiles.h>
#include <algorithm>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /**
     * The content files (`BECore/Config/ContentVersion.h`) under @p folder whose version is not kContentVersion, one
     * message per file: the path, then what CheckContentVersion says. A file that does not parse, or whose root is
     * not a content root, is not a content file and is skipped. Sorted by path.
     */
    inline Vector<String> FindStaleContentFiles(const Path& folder) {
        Vector<String> stale;
        const auto walked = WalkDirectory(folder, WalkMode::Tree, [&](StringView relativePath, FileKind kind) {
            const Path file = folder / relativePath;
            if (kind != FileKind::File || file.Extension().View() != ".xml") {
                return true;
            }
            pugi::xml_document document;
            if (!document.load_file(file.CStr()) || !IsContentRoot(document.document_element().name())) {
                return true;
            }
            if (const auto version = CheckContentVersion(document.document_element(), file.CStr()); !version) {
                stale.emplace_back(version.error().c_str());
            }
            return true;
        });
        EXPECT_TRUE(walked.has_value()) << "could not list " << folder.CStr() << ": " << (walked ? "" : walked.error().c_str());
        std::ranges::sort(stale);
        return stale;
    }

    /**
     * Fails the test once per content file under @p configFolder that is not at the engine's current content version. A
     * game calls it on its own `config/` (`ExpectContentFilesAtCurrentVersion(sourceDir / "config")`), the path taken
     * from `__FILE__` as the game's other tests do; the message says how to migrate the file.
     */
    inline void ExpectContentFilesAtCurrentVersion(const Path& configFolder) {
        EXPECT_TRUE(IsFolder(configFolder)) << "no folder " << configFolder.CStr();
        for (const String& message : FindStaleContentFiles(configFolder)) {
            ADD_FAILURE() << message;
        }
    }

}  // namespace BECore::Tests
