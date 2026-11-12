#include "FuzzTargets.h"

#include <BECore/Config/XmlNode.h>
#include <BECore/UI/UIStrings.h>
#include <BECore/UI/UIThemes.h>
#include <map>
#include <set>
#include <string>

namespace BECore::Fuzz {

    namespace {

        /// Most entries (a string, a colour) whose lookups are checked in one input: a table can hold thousands in 64 KiB, and the lookups are not what is under test
        constexpr size_t kMaxCheckedEntries = 512;

        constexpr eastl::string_view kStringsRoot = "UIStrings";
        constexpr eastl::string_view kThemesRoot = "UIThemes";

        void CheckComplaints(const eastl::vector<eastl::string>& complaints) {
            for (const eastl::string& complaint : complaints) {
                FUZZ_CHECK(!complaint.empty());
            }
        }

        std::string Attribute(pugi::xml_node node, const char* name) {
            return node.attribute(name).as_string();
        }

        // =========================================================================
        // UIStrings
        // =========================================================================

        /// (language, key) -> text of the first `string` that says it with a key the table accepts, read from the document by pugixml alone
        using ExpectedStrings = std::map<std::pair<std::string, std::string>, std::string>;

        ExpectedStrings ReadExpectedStrings(pugi::xml_node root) {
            ExpectedStrings expected;
            for (const pugi::xml_node language : root.children("language")) {
                const std::string id = Attribute(language, "id");
                if (id.empty()) {
                    continue;
                }
                for (const pugi::xml_node string : language.children("string")) {
                    const std::string key = Attribute(string, "key");
                    if (key.empty() || key.find('}') != std::string::npos || !string.attribute("text")) {
                        continue;
                    }
                    expected.emplace(std::make_pair(id, key), Attribute(string, "text"));
                }
            }
            return expected;
        }

        /// The texts the table gives for the first entries of @p expected, one string, to compare two tables
        std::string DigestStrings(UIStrings& table, const ExpectedStrings& expected) {
            std::string digest;
            size_t checked = 0;
            for (const auto& [id, text] : expected) {
                if (checked++ == kMaxCheckedEntries) {
                    break;
                }
                FUZZ_CHECK(table.SetLanguage(PoolString::Intern(eastl::string_view(id.first.data(), id.first.size()))));
                const auto found = table.Find(eastl::string_view(id.second.data(), id.second.size()));
                digest += found ? std::string(found->data(), found->size()) : std::string("<none>");
                digest += '\n';
            }
            for (const PoolString language : table.GetLanguages()) {
                digest += language.ToStringView().data();
                digest += ',';
            }
            return digest;
        }

        void CheckStrings(pugi::xml_node root) {
            const ExpectedStrings expected = ReadExpectedStrings(root);

            UIStrings table;
            CheckComplaints(table.LoadFromXml(XmlNode{root}));

            // Every string the file says with a good key is in the table, in its language, with the text of its first occurrence
            size_t checked = 0;
            for (const auto& [id, text] : expected) {
                if (checked++ == kMaxCheckedEntries) {
                    break;
                }
                const PoolString language = PoolString::Intern(eastl::string_view(id.first.data(), id.first.size()));
                FUZZ_CHECK(table.HasLanguage(language));
                FUZZ_CHECK(table.SetLanguage(language));
                const auto found = table.Find(eastl::string_view(id.second.data(), id.second.size()));
                FUZZ_CHECK(found.has_value());
                FUZZ_CHECK(eastl::string_view(found->data(), found->size()) == eastl::string_view(text.data(), text.size()));
                // A text with no reference resolves to itself, and a reference to this key gives this text
                if (!UIStrings::HasReferences(*found)) {
                    const eastl::string expectedText(found->data(), found->size());
                    FUZZ_CHECK(table.Resolve(*found) == expectedText);
                    const eastl::string reference = Format("{{#{}}}", id.second.c_str());
                    FUZZ_CHECK(table.Resolve(eastl::string_view(reference.data(), reference.size())) == expectedText);
                }
            }

            // The table has no empty key, no key with a closing brace, and no language twice
            FUZZ_CHECK(!table.Find("").has_value());
            FUZZ_CHECK(!table.Find("a}b").has_value());
            const auto& languages = table.GetLanguages();
            for (size_t i = 0; i < languages.size() && i < kMaxCheckedEntries; ++i) {
                FUZZ_CHECK(!languages[i].Empty());
                FUZZ_CHECK(table.HasLanguage(languages[i]));
                for (size_t j = i + 1; j < languages.size() && j < kMaxCheckedEntries; ++j) {
                    FUZZ_CHECK(languages[i] != languages[j]);
                }
            }
            FUZZ_CHECK(languages.empty() || table.HasLanguage(table.GetDefaultLanguage()));

            // The same file again changes nothing; a fresh table gives the same texts
            const std::string before = DigestStrings(table, expected);
            CheckComplaints(table.LoadFromXml(XmlNode{root}));
            FUZZ_CHECK(DigestStrings(table, expected) == before);
            FUZZ_CHECK(table.GetLanguages().size() == languages.size());

            UIStrings fresh;
            fresh.LoadFromXml(XmlNode{root});
            FUZZ_CHECK(DigestStrings(fresh, expected) == before);
        }

        // =========================================================================
        // UIThemes
        // =========================================================================

        /// The colours of every (theme, token) the table has, in the order of its lists, to compare two tables
        std::string DigestThemes(UIThemes& table) {
            std::string digest;
            size_t checked = 0;
            for (const PoolString theme : table.GetThemes()) {
                for (const PoolString token : table.GetTokens()) {
                    if (checked++ == kMaxCheckedEntries) {
                        return digest;
                    }
                    if (!table.HasToken(theme, token.ToStringView())) {
                        digest += '-';
                        continue;
                    }
                    FUZZ_CHECK(table.SetTheme(theme));
                    const auto color = table.Find(token.ToStringView());
                    FUZZ_CHECK(color.has_value());
                    digest += std::to_string(color->r) + "," + std::to_string(color->g) + "," + std::to_string(color->b) + "," + std::to_string(color->a) + ";";
                }
            }
            return digest;
        }

        void CheckThemes(pugi::xml_node root) {
            UIThemes table;
            CheckComplaints(table.LoadFromXml(XmlNode{root}));

            // The table has no empty or ill-named token, no token or theme twice, and every token is in some theme
            FUZZ_CHECK(!table.Find("").has_value());
            const auto& tokens = table.GetTokens();
            const auto& themes = table.GetThemes();
            for (size_t i = 0; i < tokens.size() && i < kMaxCheckedEntries; ++i) {
                FUZZ_CHECK(!tokens[i].Empty());
                FUZZ_CHECK(UIThemes::IsValidToken(tokens[i].ToStringView()));
                bool inSomeTheme = false;
                for (const PoolString theme : themes) {
                    inSomeTheme = inSomeTheme || table.HasToken(theme, tokens[i].ToStringView());
                }
                FUZZ_CHECK(inSomeTheme);
                for (size_t j = i + 1; j < tokens.size() && j < kMaxCheckedEntries; ++j) {
                    FUZZ_CHECK(tokens[i] != tokens[j]);
                }
            }
            for (size_t i = 0; i < themes.size() && i < kMaxCheckedEntries; ++i) {
                FUZZ_CHECK(!themes[i].Empty());
                FUZZ_CHECK(table.HasTheme(themes[i]));
                for (size_t j = i + 1; j < themes.size() && j < kMaxCheckedEntries; ++j) {
                    FUZZ_CHECK(themes[i] != themes[j]);
                }
            }
            FUZZ_CHECK(themes.empty() || table.HasTheme(table.GetDefaultTheme()));

            // A colour the table has is one the file says: in that theme, under a good name, with a value (the value may still be refused, so not the other way round)
            std::set<std::pair<std::string, std::string>> said;
            for (const pugi::xml_node theme : root.children("theme")) {
                for (const pugi::xml_node color : theme.children("color")) {
                    if (color.attribute("value")) {
                        said.emplace(Attribute(theme, "id"), Attribute(color, "name"));
                    }
                }
            }
            size_t checked = 0;
            for (const PoolString theme : themes) {
                for (const PoolString token : tokens) {
                    if (checked++ == kMaxCheckedEntries) {
                        break;
                    }
                    if (table.HasToken(theme, token.ToStringView())) {
                        FUZZ_CHECK(said.contains({std::string(theme.ToStringView().data(), theme.ToStringView().size()), std::string(token.ToStringView().data(), token.ToStringView().size())}));
                    }
                }
            }

            // The same file again changes nothing; a fresh table gives the same colours
            const std::string before = DigestThemes(table);
            CheckComplaints(table.LoadFromXml(XmlNode{root}));
            FUZZ_CHECK(DigestThemes(table) == before);
            FUZZ_CHECK(table.GetThemes().size() == themes.size());
            FUZZ_CHECK(table.GetTokens().size() == tokens.size());

            UIThemes fresh;
            fresh.LoadFromXml(XmlNode{root});
            FUZZ_CHECK(DigestThemes(fresh) == before);
        }

    }  // namespace

    /**
     * The input is an XML text. A root named `UIStrings` goes to the string table, `UIThemes` to the theme table, anything else to both (the loaders say what
     * they do with a file of the wrong kind). The tables are local instances: they are pure data and need no service.
     *
     * What is checked: a load gives no empty complaint; the table has what the file says (read from the document by
     * pugixml alone: the first occurrence of a good key wins), no empty key, token or language, and no duplicate; a reference to a key resolves to its text; the same
     * file loaded again changes nothing, and a fresh table gives the same answers.
     */
    void FuzzUiTables(const uint8_t* data, size_t size) {
        // The text in a buffer of its own: pugixml reads a text that looks like UTF-16 or UTF-32 as words
        const eastl::string text(reinterpret_cast<const char*>(data), size);
        pugi::xml_document document;
        if (!document.load_buffer(text.data(), text.size())) {
            return;
        }
        const pugi::xml_node root = document.document_element();
        if (!root) {
            return;
        }
        const eastl::string_view rootName(root.name());
        if (rootName != kThemesRoot) {
            CheckStrings(root);
        }
        if (rootName != kStringsRoot) {
            CheckThemes(root);
        }
    }

}  // namespace BECore::Fuzz
