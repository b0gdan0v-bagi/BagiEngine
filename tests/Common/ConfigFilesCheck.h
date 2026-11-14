#pragma once

#include <BECore/Config/ContentVersion.h>
#include <BECore/Config/XmlNode.h>
#include <BECore/Format/Format.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <BECore/Types/Types.h>
#include <Common/Printers.h>
#include <algorithm>
#include <filesystem>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// The reflected type of one config file: @p read fills it from the deserializer (`Type().Deserialize(d)`)
    struct ConfigReader {
        StringView stem;                             // the file `<stem>.xml`
        eastl::function<void(IDeserializer&)> read;  //-NoStd: no alias for a callable yet (entry `callables-move-only`)
    };

    /// A config file that no reflected type reads (a hand-written reader, another format in an XML file), and why
    struct HandReadConfig {
        StringView stem;
        StringView reason;
    };

    namespace ConfigFilesCheckDetail {

        inline String Describe(StringView file, const DeserializeError& message) {
            const StringView path = message.path.ToStringView();
            return Format("{}: {}{}{}", file, path, path.empty() ? "" : ": ", message.errorMessage.ToStringView());
        }

        /// Loads `<folder>/<stem>.xml` through @p reader under the mode Error and appends one message per error and per warning.
        /// A SceneManager root has its node references expanded from the folder first, as the scene loader does.
        inline void ReadOne(const std::filesystem::path& folder, const ConfigReader& reader, Vector<String>& messages) {
            const String file = Format("{}.xml", reader.stem);
            const std::filesystem::path path = folder / ToStd(file);
            pugi::xml_document document;
            if (!document.load_file(path.c_str())) {
                messages.push_back(Format("{}: the file does not parse", file));
                return;
            }
            if (StringView(document.document_element().name()) == "SceneManager") {
                const NodeExpansion expansion = LoadSceneDocument(path, document);
                for (const String& error : expansion.errors) {
                    messages.push_back(Format("{}: {}", file, error));
                }
            }

            XmlDeserializer deserializer;
            deserializer.SetDiagnostics(DiagnosticsMode::Error);
            if (!deserializer.LoadFromXmlNode(XmlNode{document.document_element()})) {
                messages.push_back(Format("{}: the document cannot be read", file));
                return;
            }
            if (IsContentRoot(document.document_element().name())) {
                deserializer.NoteKnownKey(kContentVersionAttribute);
            }
            reader.read(deserializer);
            deserializer.FinishLoad();
            for (const DeserializeError& error : deserializer.GetErrors()) {
                messages.push_back(Describe(file, error));
            }
            for (const DeserializeError& warning : deserializer.GetWarnings()) {
                messages.push_back(Describe(file, warning));
            }
        }

    }  // namespace ConfigFilesCheckDetail

    /**
     * The findings of the `*.xml` files directly in @p folder (not its subfolders), one message per line `<file>: <path>: <message>`.
     *
     * Each file named in @p readers is read through its type with an XmlDeserializer under the mode Error (unknown keys, a
     * missing `required` field, a value outside `range`, a malformed value), then `FinishLoad()`. A file in neither list is a
     * message too (a new config must say how it is read), and so is a reader or an entry of @p byHand with no file.
     * A `SceneManager` file has its node references expanded from the folder first; a content file's `contentVersion` is known.
     * No gtest macros here, so a test can assert the output on a folder it seeded.
     */
    inline Vector<String> FindConfigFindings(const std::filesystem::path& folder, Span<const ConfigReader> readers, Span<const HandReadConfig> byHand) {
        Vector<String> messages;
        Vector<String> stems;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {  //-NoStd: a test lists a folder
            if (it->is_regular_file(ec) && it->path().extension() == ".xml") {
                stems.push_back(FromStdString(it->path().stem().string()));
            }
        }
        std::ranges::sort(stems);

        const auto isReader = [&](StringView stem) { return std::ranges::any_of(readers, [&](const ConfigReader& reader) { return reader.stem == stem; }); };
        const auto isByHand = [&](StringView stem) { return std::ranges::any_of(byHand, [&](const HandReadConfig& entry) { return entry.stem == stem; }); };
        const auto hasFile = [&](StringView stem) { return std::ranges::find(stems, stem) != stems.end(); };

        for (const String& stem : stems) {
            if (!isReader(stem) && !isByHand(stem)) {
                messages.push_back(Format("{}.xml: no reader and not on the list of files read by hand", stem));
            }
        }
        for (const ConfigReader& reader : readers) {
            if (hasFile(reader.stem)) {
                ConfigFilesCheckDetail::ReadOne(folder, reader, messages);
            } else {
                messages.push_back(Format("{}.xml: a reader is listed but the file does not exist", reader.stem));
            }
        }
        for (const HandReadConfig& entry : byHand) {
            if (!hasFile(entry.stem)) {
                messages.push_back(Format("{}.xml: listed as read by hand ({}) but the file does not exist", entry.stem, entry.reason));
            }
        }
        return messages;
    }

    /**
     * Fails the test once per finding of FindConfigFindings. A game calls it on its own `config/` with its own lists
     * (`ExpectConfigFilesClean(sourceDir / "config", readers, byHand)`, the path taken from `__FILE__`), and
     * again on a subfolder it reads through other files. The reader of a file whose keys another reader also takes
     * calls `NoteKnownKey` on the deserializer for them: the mode is never turned off.
     */
    inline void ExpectConfigFilesClean(const std::filesystem::path& folder, Span<const ConfigReader> readers, Span<const HandReadConfig> byHand) {
        EXPECT_TRUE(std::filesystem::is_directory(folder)) << "no folder " << folder.string();
        for (const String& message : FindConfigFindings(folder, readers, byHand)) {
            ADD_FAILURE() << message;
        }
    }

}  // namespace BECore::Tests
