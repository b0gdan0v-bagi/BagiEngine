#include <BECore/Reflection/PropertyPath.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Widgets/PanelAssets/PanelAssetLimits.h>
#include <BECore/Widgets/PanelAssets/PanelAssetLoader.h>
#include <BECore/Widgets/PanelAssets/PanelBinding.h>
#include <BECore/Widgets/PanelAssets/PanelCondition.h>
#include <BECore/Widgets/PanelAssets/PanelElements.h>
#include <Fuzz/FuzzChecks.h>
#include <Fuzz/FuzzTargets.h>

namespace BECore::Fuzz {

    namespace {

        constexpr StringView kMoreIssuesPart = "more issues";
        constexpr uint8_t kFirstPrintable = 0x20;
        constexpr uint8_t kDelete = 0x7F;

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
                Text(asset.titleBind);
                Binding(asset.titleBind, false);
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
                if ((asset.window == PanelWindowKind::Dialogs || asset.window == PanelWindowKind::Bar) && !asset.open) {
                    Fail();
                }
                _window = asset.window;
                Elements(asset.elements, 1, nullptr);
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

            void ObjectBinding(const String& text) {
                if (text.empty() || !ParseObjectBinding(text)) {
                    Fail();
                }
            }

            void AtMost(uint32_t value, size_t limit) {
                if (value > limit) {
                    Fail();
                }
            }

            /// Written as a comparison so a NaN fails it
            void Extent(float value) {
                if (!(value >= 0.0f && value <= PanelAssetLimits::kMaxChildExtent)) {
                    Fail();
                }
            }

            /// Written as comparisons so a NaN fails them
            void Number(const PanelNumber& number) {
                constexpr float kLimit = PanelAssetLimits::kMaxNumberExtent;
                if (!(number.min >= -kLimit && number.min <= kLimit && number.max >= -kLimit && number.max <= kLimit && number.min <= number.max)) {
                    Fail();
                }
                if (number.slider && !(number.min < number.max)) {
                    Fail();
                }
                if (!(number.step >= 0.0f && number.step <= kLimit)) {
                    Fail();
                }
                if (!(number.stepFast >= 0.0f && number.stepFast <= kLimit) || (number.stepFast != 0.0f && !number.stepper)) {
                    Fail();
                }
                if (number.stepper && (number.slider || number.input)) {
                    Fail();
                }
            }

            /// A literal as a condition's: within the text limit and read by the condition's own literal reader under `==`
            void RadioValue(const String& value) {
                PanelConditionTerm term;
                term.compare = PanelCompare::Equal;
                if (value.empty() || value.size() > PanelAssetLimits::kMaxTextBytes || !ParsePanelConditionLiteral(value, term)) {
                    Fail();
                }
            }

            void Condition(const String& text) {
                if (!text.empty() && !ParsePanelCondition(text)) {
                    Fail();
                }
            }

            void RelativePath(const String& text) {
                if (!text.empty() && text.size() <= PanelAssetLimits::kMaxTextBytes && !ParsePropertyPath(text)) {
                    Fail();
                }
            }

            void Literal(const String& text) {
                for (const char c : text) {
                    const auto byte = static_cast<uint8_t>(c);
                    if (byte < kFirstPrintable || byte == kDelete) {
                        Fail();
                    }
                }
            }

            void Elements(const Vector<IntrusivePtr<PanelElement>>& list, size_t depth, const PanelElement* parent) {
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
                    Element(*element, parent);
                    const bool isPopup = element->Is<PanelPopup>();
                    const bool isMenu = element->Is<PanelMenu>();
                    const bool isDropdown = element->Is<PanelDropdown>();
                    _popupDepth += isPopup ? 1 : 0;
                    _menuDepth += isMenu ? 1 : 0;
                    _dropdownDepth += isDropdown ? 1 : 0;
                    Elements(element->elements, depth + 1, element.Get());
                    _popupDepth -= isPopup ? 1 : 0;
                    _menuDepth -= isMenu ? 1 : 0;
                    _dropdownDepth -= isDropdown ? 1 : 0;
                }
            }

            void Element(const PanelElement& element, const PanelElement* parent) {
                Text(element.id);
                Text(element.visible);
                Text(element.tooltip);
                if (!IsWellFormedId(element.id) || !_ids.insert(element.id).second) {
                    Fail();
                }
                Condition(element.visible);
                if (!element.elements.empty() && !element.Is<PanelSection>() && !element.Is<PanelRow>() && !element.Is<PanelChild>() && !element.Is<PanelPopup>() && !element.Is<PanelDropdown>() &&
                    !element.Is<PanelTabs>() && !element.Is<PanelTab>() && !element.Is<PanelMenu>()) {
                    Fail();
                }

                if (const auto* section = element.Cast<PanelSection>()) {
                    Text(section->title);
                    Text(section->icon);
                    Text(section->openBind);
                    Binding(section->openBind, false);
                } else if (const auto* text = element.Cast<PanelText>()) {
                    Text(text->text);
                    Text(text->bind);
                    if (text->text.empty() && text->bind.empty()) {
                        Fail();
                    }
                    Binding(text->bind, false);
                    AtMost(text->maxLines, PanelAssetLimits::kMaxScrollLines);
                } else if (const auto* field = element.Cast<PanelField>()) {
                    Text(field->bind);
                    Text(field->label);
                    Binding(field->bind, true);
                } else if (const auto* fields = element.Cast<PanelFields>()) {
                    Text(fields->bind);
                    ObjectBinding(fields->bind);
                } else if (const auto* input = element.Cast<PanelInput>()) {
                    Text(input->bind);
                    Text(input->hint);
                    Text(input->icon);
                    Text(input->submitCommand);
                    Text(input->commitCommand);
                    Binding(input->bind, true);
                    if (input->focus && _popupDepth == 0) {
                        Fail();
                    }
                    if (input->width < 0.0f) {
                        Fail();
                    }
                    AtMost(input->lines, PanelAssetLimits::kMaxInputLines);
                    if (input->lines > 0 && !input->submitCommand.empty()) {
                        Fail();
                    }
                } else if (const auto* checkbox = element.Cast<PanelCheckbox>()) {
                    Text(checkbox->bind);
                    Text(checkbox->text);
                    Text(checkbox->labelBind);
                    Text(checkbox->icon);
                    Text(checkbox->enableWhen);
                    Binding(checkbox->bind, true);
                    Binding(checkbox->labelBind, false);
                    if (checkbox->text.empty() && checkbox->labelBind.empty()) {
                        Fail();
                    }
                    Condition(checkbox->enableWhen);
                } else if (const auto* combo = element.Cast<PanelCombo>()) {
                    Text(combo->bind);
                    Text(combo->items);
                    Text(combo->itemField);
                    Text(combo->separatorField);
                    Text(combo->enableWhen);
                    Binding(combo->bind, true);
                    Binding(combo->items, false);
                    if (combo->items.empty() && (!combo->itemField.empty() || !combo->separatorField.empty())) {
                        Fail();
                    }
                    RelativePath(combo->itemField);
                    RelativePath(combo->separatorField);
                    Extent(combo->width);
                    Condition(combo->enableWhen);
                } else if (const auto* number = element.Cast<PanelNumber>()) {
                    Text(number->bind);
                    Text(number->text);
                    Text(number->commitCommand);
                    Text(number->enableWhen);
                    Binding(number->bind, true);
                    Extent(number->width);
                    Number(*number);
                    Condition(number->enableWhen);
                    if (number->input && number->slider) {
                        Fail();
                    }
                } else if (const auto* radio = element.Cast<PanelRadio>()) {
                    Text(radio->bind);
                    Text(radio->value);
                    Text(radio->text);
                    Text(radio->icon);
                    Text(radio->enableWhen);
                    Binding(radio->bind, true);
                    RadioValue(radio->value);
                    if (radio->text.empty() && radio->icon.empty()) {
                        Fail();
                    }
                    Condition(radio->enableWhen);
                } else if (const auto* button = element.Cast<PanelButton>()) {
                    Text(button->text);
                    Text(button->icon);
                    Text(button->command);
                    Text(button->argument);
                    Text(button->enableWhen);
                    Text(button->fillWhen);
                    if (button->command.empty()) {
                        Fail();
                    }
                    Condition(button->enableWhen);
                    Condition(button->fillWhen);
                    if (!button->fillWhen.empty() && button->fill == PanelButtonFill::None) {
                        Fail();
                    }
                    Literal(button->argument);
                    if (button->width != kPanelFillLineWidth) {
                        Extent(button->width);
                    }
                } else if (const auto* menu = element.Cast<PanelMenu>()) {
                    Menu(*menu);
                } else if (const auto* item = element.Cast<PanelMenuItem>()) {
                    Text(item->text);
                    Text(item->icon);
                    Text(item->command);
                    Text(item->argument);
                    Text(item->enableWhen);
                    if (parent == nullptr || !parent->Is<PanelMenu>() || item->text.empty() || item->command.empty()) {
                        Fail();
                    }
                    Condition(item->enableWhen);
                    Literal(item->argument);
                } else if (const auto* items = element.Cast<PanelMenuItems>()) {
                    Text(items->source);
                    Text(items->itemField);
                    Text(items->command);
                    Text(items->icon);
                    if (parent == nullptr || !parent->Is<PanelMenu>() || items->command.empty()) {
                        Fail();
                    }
                    Binding(items->source, true);
                    RelativePath(items->itemField);
                } else if (const auto* table = element.Cast<PanelTable>()) {
                    Text(table->source);
                    Binding(table->source, true);
                    AtMost(table->scrollRows, PanelAssetLimits::kMaxScrollRows);
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
                } else if (const auto* child = element.Cast<PanelChild>()) {
                    Extent(child->width);
                    Extent(child->height);
                } else if (const auto* popup = element.Cast<PanelPopup>()) {
                    Text(popup->title);
                    Text(popup->open);
                    Text(popup->closeCommand);
                    if (popup->open.empty() || !ParsePanelCondition(popup->open) || _popupDepth > 0 || _dropdownDepth > 0 || ++_popupCount > PanelAssetLimits::kMaxPopups) {
                        Fail();
                    }
                } else if (const auto* dropdown = element.Cast<PanelDropdown>()) {
                    Text(dropdown->text);
                    Text(dropdown->icon);
                    Text(dropdown->enableWhen);
                    if ((dropdown->text.empty() && dropdown->icon.empty()) || _dropdownDepth > 0 || _popupDepth > 0 || ++_dropdownCount > PanelAssetLimits::kMaxDropdowns) {
                        Fail();
                    }
                    Condition(dropdown->enableWhen);
                } else if (const auto* tabs = element.Cast<PanelTabs>()) {
                    Tabs(*tabs);
                } else if (const auto* tab = element.Cast<PanelTab>()) {
                    Text(tab->title);
                    if (parent == nullptr || !parent->Is<PanelTabs>() || tab->title.empty() || ++_tabCount > PanelAssetLimits::kMaxTabs) {
                        Fail();
                    }
                } else if (!element.Is<PanelRow>()) {
                    Fail();  // the base class: no `type`, or one that is no kind
                }
            }

            void Menu(const PanelMenu& menu) {
                Text(menu.title);
                Text(menu.enableWhen);
                Text(menu.openCommand);
                if (_window != PanelWindowKind::Bar || menu.title.empty() || _menuDepth >= PanelAssetLimits::kMaxMenuDepth || ++_menuCount > PanelAssetLimits::kMaxMenus) {
                    Fail();
                }
                Condition(menu.enableWhen);
                for (const IntrusivePtr<PanelElement>& child : menu.elements) {
                    if (child != nullptr && !child->Is<PanelMenu>() && !child->Is<PanelMenuItem>() && !child->Is<PanelMenuItems>() && !child->Is<PanelSeparator>() && !child->Is<PanelText>()) {
                        Fail();
                    }
                }
            }

            void Tabs(const PanelTabs& tabs) {
                if (tabs.elements.empty() || tabs.elements.size() > PanelAssetLimits::kMaxTabsPerBar) {
                    Fail();
                }
                for (const IntrusivePtr<PanelElement>& child : tabs.elements) {
                    if (child != nullptr && !child->Is<PanelTab>()) {
                        Fail();
                    }
                }
            }

            void Columns(const PanelTable& table) {
                HashSet<String> ids;
                for (const PanelColumn& column : table.columns) {
                    if (++_columnCount > PanelAssetLimits::kMaxColumns) {
                        Fail();
                        return;
                    }
                    for (const String* text : {&column.id, &column.title, &column.field, &column.styleField, &column.enableField, &column.selectedField, &column.labelField, &column.hint,
                                               &column.command, &column.argument, &column.icon, &column.tooltip}) {
                        Text(*text);
                    }
                    if (!IsWellFormedId(column.id) || !ids.insert(column.id).second) {
                        Fail();
                    }
                    if (column.type == PanelColumnType::Button) {
                        if (column.command.empty()) {
                            Fail();
                        }
                        RelativePath(column.field);
                    } else if (column.field.empty()) {
                        Fail();
                    } else {
                        RelativePath(column.field);
                    }
                    RelativePath(column.styleField);
                    RelativePath(column.enableField);
                    if (column.type != PanelColumnType::Selectable && !column.selectedField.empty()) {
                        Fail();
                    }
                    RelativePath(column.selectedField);
                    if (column.type != PanelColumnType::Checkbox && !column.labelField.empty()) {
                        Fail();
                    }
                    RelativePath(column.labelField);
                    RelativePath(column.argument);
                }
            }

            HashSet<String> _ids;
            size_t _elementCount = 0;
            size_t _columnCount = 0;
            size_t _popupCount = 0;
            size_t _popupDepth = 0;
            size_t _tabCount = 0;
            size_t _menuCount = 0;
            size_t _menuDepth = 0;
            size_t _dropdownCount = 0;
            size_t _dropdownDepth = 0;
            PanelWindowKind _window = PanelWindowKind::Docked;
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
