#include <BECore/Reflection/PropertyPath.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Widgets/PanelAssets/PanelAssetLimits.h>
#include <BECore/Widgets/PanelAssets/PanelAssetLoader.h>
#include <BECore/Widgets/PanelAssets/PanelBinding.h>
#include <BECore/Widgets/PanelAssets/PanelElements.h>
#include <Fuzz/FuzzChecks.h>
#include <Fuzz/FuzzTargets.h>

namespace BECore::Fuzz {

    namespace {

        constexpr StringView kMoreIssuesPart = "more issues";

        bool IsIdCharacter(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        }

        bool IsWellFormedId(StringView id) {
            if (id.empty() || id.size() > PanelAssetLimits::kMaxIdBytes) {
                return false;
            }
            for (const char c : id) {
                if (!IsIdCharacter(c)) {
                    return false;
                }
            }
            return true;
        }

        bool IsWellFormedSourceName(StringView name) {
            if (name.empty() || (name[0] >= '0' && name[0] <= '9')) {
                return false;
            }
            for (const char c : name) {
                if (!IsIdCharacter(c) || c == '.' || c == '-') {
                    return false;
                }
            }
            return true;
        }

        /**
         * @brief What a panel asset must satisfy to have no issue, decided from the returned tree and not from the loader's code
         *
         * The limits, the ids, the names a field must have and the syntax of every binding and path. Walks the whole tree (the reader
         * bounds its size and depth) and stops descending past the depth limit, which is a violation by itself.
         */
        class AssetJudge {
        public:
            explicit AssetJudge(const PanelAsset& asset) {
                Text(asset.id);
                Text(asset.title);
                Text(asset.command);
                if (!IsWellFormedId(asset.id) || asset.width < 0.0f || asset.height < 0.0f || asset.sources.size() > PanelAssetLimits::kMaxSources) {
                    Fail();
                }
                HashSet<String> names;
                for (const PanelSource& source : asset.sources) {
                    Text(source.name);
                    Text(source.model);
                    if (!IsWellFormedSourceName(source.name) || !names.insert(source.name).second) {
                        Fail();
                    }
                }
                Elements(asset.elements, 1);
            }

            [[nodiscard]] bool Violated() const {
                return _violated;
            }

        private:
            void Fail() {
                _violated = true;
            }

            void Text(const String& text) {
                if (text.size() > PanelAssetLimits::kMaxTextBytes) {
                    Fail();
                }
            }

            void Binding(const String& text, bool required) {
                if (text.empty()) {
                    if (required) {
                        Fail();
                    }
                } else if (!ParseBinding(text)) {
                    Fail();
                }
            }

            void Condition(const String& text) {
                if (!text.empty() && !ParseCondition(text)) {
                    Fail();
                }
            }

            void RelativePath(const String& text) {
                if (!text.empty() && text.size() <= PanelAssetLimits::kMaxTextBytes && !ParsePropertyPath(text)) {
                    Fail();
                }
            }

            void Elements(const Vector<IntrusivePtr<PanelElement>>& list, size_t depth) {
                if (list.empty()) {
                    return;
                }
                if (depth > PanelAssetLimits::kMaxDepth) {
                    Fail();
                    return;
                }
                for (const IntrusivePtr<PanelElement>& element : list) {
                    if (element == nullptr) {
                        continue;
                    }
                    if (++_elementCount > PanelAssetLimits::kMaxElements) {
                        Fail();
                    }
                    Element(*element);
                    Elements(element->elements, depth + 1);
                }
            }

            void Element(const PanelElement& element) {
                Text(element.id);
                Text(element.visible);
                Text(element.tooltip);
                if (!IsWellFormedId(element.id) || !_ids.insert(element.id).second) {
                    Fail();
                }
                Condition(element.visible);
                if (!element.elements.empty() && !element.Is<PanelSection>() && !element.Is<PanelRow>()) {
                    Fail();
                }

                if (const auto* section = element.Cast<PanelSection>()) {
                    Text(section->title);
                } else if (const auto* text = element.Cast<PanelText>()) {
                    Text(text->text);
                    Text(text->bind);
                    if (text->text.empty() && text->bind.empty()) {
                        Fail();
                    }
                    Binding(text->bind, false);
                } else if (const auto* field = element.Cast<PanelField>()) {
                    Text(field->bind);
                    Text(field->label);
                    Binding(field->bind, true);
                } else if (const auto* fields = element.Cast<PanelFields>()) {
                    Text(fields->bind);
                    Binding(fields->bind, true);
                } else if (const auto* input = element.Cast<PanelInput>()) {
                    Text(input->bind);
                    Text(input->hint);
                    Text(input->icon);
                    Binding(input->bind, true);
                    if (input->width < 0.0f) {
                        Fail();
                    }
                } else if (const auto* button = element.Cast<PanelButton>()) {
                    Text(button->text);
                    Text(button->icon);
                    Text(button->command);
                    Text(button->argument);
                    Text(button->enableWhen);
                    if (button->command.empty()) {
                        Fail();
                    }
                    Condition(button->enableWhen);
                    RelativePath(button->argument);
                } else if (const auto* table = element.Cast<PanelTable>()) {
                    Text(table->source);
                    Binding(table->source, true);
                    Columns(*table);
                } else if (const auto* separator = element.Cast<PanelSeparator>()) {
                    Text(separator->title);
                } else if (const auto* empty = element.Cast<PanelEmptyState>()) {
                    Text(empty->icon);
                    Text(empty->text);
                } else if (const auto* slot = element.Cast<PanelSlot>()) {
                    Text(slot->name);
                    if (slot->name.empty()) {
                        Fail();
                    }
                } else if (!element.Is<PanelRow>()) {
                    Fail();  // the base class: no `type`, or one that is no kind
                }
            }

            void Columns(const PanelTable& table) {
                HashSet<String> ids;
                for (const PanelColumn& column : table.columns) {
                    if (++_columnCount > PanelAssetLimits::kMaxColumns) {
                        Fail();
                        return;
                    }
                    for (const String* text : {&column.id, &column.title, &column.field, &column.styleField, &column.command, &column.argument, &column.icon, &column.tooltip}) {
                        Text(*text);
                    }
                    if (!IsWellFormedId(column.id) || !ids.insert(column.id).second) {
                        Fail();
                    }
                    if (column.type == PanelColumnType::Button) {
                        if (column.command.empty()) {
                            Fail();
                        }
                    } else if (column.field.empty()) {
                        Fail();
                    } else {
                        RelativePath(column.field);
                    }
                    RelativePath(column.styleField);
                    RelativePath(column.argument);
                }
            }

            HashSet<String> _ids;
            size_t _elementCount = 0;
            size_t _columnCount = 0;
            bool _violated = false;
        };

        String Joined(const Vector<PanelIssue>& issues) {
            String text;
            for (const PanelIssue& issue : issues) {
                text += issue.where;
                text += '\n';
                text += issue.message;
                text += '\n';
            }
            return text;
        }

        void CheckIssues(const PanelLoadResult& result) {
            FUZZ_CHECK(result.issues.size() <= PanelAssetLimits::kMaxIssues);
            for (const PanelIssue& issue : result.issues) {
                FUZZ_CHECK(!issue.message.empty());
                FUZZ_CHECK(issue.where.size() <= PanelAssetLimits::kMaxIssueWhereBytes);
                FUZZ_CHECK(issue.message.size() <= PanelAssetLimits::kMaxIssueMessageBytes);
                FUZZ_CHECK(!HasControlCharacter(StringView(issue.where)));
                FUZZ_CHECK(!HasControlCharacter(StringView(issue.message)));
            }
            if (result.issues.size() == PanelAssetLimits::kMaxIssues) {
                FUZZ_CHECK(StringView(result.issues.back().message).find(kMoreIssuesPart) != StringView::npos);
            }
        }

        /// Whether the deserializer alone, under the strict diagnostics the loader sets, has a finding: an unknown attribute or type, a bad number
        bool ReaderFinds(StringView text) {
            XmlDeserializer deserializer;
            if (!deserializer.LoadFromString(text)) {
                return true;
            }
            deserializer.SetDiagnostics(DiagnosticsMode::Error);
            deserializer.SetStrictValues(true);
            const auto asset = New<PanelAsset>();
            asset->Deserialize(deserializer);
            deserializer.FinishLoad();
            return deserializer.HasErrors();
        }

        String Write(const PanelAsset& asset) {
            XmlSerializer serializer;
            asset.Serialize(serializer);
            return serializer.SaveToString();
        }

        void CheckValidAsset(const PanelAsset& asset) {
            const String written = Write(asset);
            const PanelLoadResult again = LoadPanelAsset(StringView(written));
            FUZZ_CHECK(again.asset != nullptr);
            FUZZ_CHECK(again.issues.empty());
            FUZZ_CHECK(Write(*again.asset) == written);
        }

    }  // namespace

    void FuzzPanelAsset(const uint8_t* data, size_t size) {
        // A buffer of its own: pugixml reads a text that looks like UTF-16 or UTF-32 as words
        const String text(reinterpret_cast<const char*>(data), size);
        const PanelLoadResult first = LoadPanelAsset(StringView(text));
        CheckIssues(first);

        const PanelLoadResult second = LoadPanelAsset(StringView(text));
        FUZZ_CHECK(Joined(first.issues) == Joined(second.issues));
        FUZZ_CHECK((first.asset == nullptr) == (second.asset == nullptr));

        if (first.asset == nullptr) {
            FUZZ_CHECK(!first.issues.empty());
            return;
        }

        // No issue exactly when the tree satisfies the format and the reader found nothing to say about the text
        const bool violated = AssetJudge(*first.asset).Violated();
        FUZZ_CHECK(first.issues.empty() == (!violated && !ReaderFinds(StringView(text))));

        if (first.issues.empty()) {
            CheckValidAsset(*first.asset);
        }
    }

}  // namespace BECore::Fuzz
