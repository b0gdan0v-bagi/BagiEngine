#include "PanelAssetLoader.h"

#include <BECore/Reflection/PropertyAccess.h>
#include <BECore/Reflection/PropertyPath.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Reflection/TypedRef.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Widgets/PanelAssets/PanelAssetLimits.h>
#include <BECore/Widgets/PanelAssets/PanelBinding.h>
#include <BECore/Widgets/PanelAssets/PanelCondition.h>
#include <BECore/Widgets/PanelAssets/PanelElements.h>
#include <cmath>

namespace BECore {

    namespace {

        constexpr uint8_t kFirstPrintable = 0x20;
        constexpr uint8_t kDelete = 0x7F;
        constexpr uint8_t kUtf8ContinuationMask = 0xC0;
        constexpr uint8_t kUtf8ContinuationBits = 0x80;
        constexpr char kReplacementChar = '?';
        constexpr StringView kFileWhere = "file";
        constexpr StringView kBaseElementType = "PanelElement";
        constexpr StringView kNoParseMessage = "the text is not a valid XML document";
        constexpr StringView kMoreIssuesMessage = "more issues were found and are not listed";

        /// An issue quotes the file and reaches the panel and the log: at most @p maxBytes, no UTF-8 sequence split, no control character
        String Clean(StringView text, size_t maxBytes) {
            size_t length = std::min(text.size(), maxBytes);
            while (length > 0 && length < text.size() && (static_cast<uint8_t>(text[length]) & kUtf8ContinuationMask) == kUtf8ContinuationBits) {
                --length;
            }
            String cleaned(text.data(), length);
            for (char& c : cleaned) {
                const auto byte = static_cast<uint8_t>(c);
                if (byte < kFirstPrintable || byte == kDelete) {
                    c = kReplacementChar;
                }
            }
            return cleaned;
        }

        /// Collects the issues of one load up to the limit
        class IssueList {
        public:
            explicit IssueList(Vector<PanelIssue>& issues) : _issues(issues) {}

            void Add(StringView where, StringView message) {
                if (_issues.size() >= PanelAssetLimits::kMaxIssues) {
                    return;
                }
                if (_issues.size() + 1 == PanelAssetLimits::kMaxIssues) {
                    _issues.push_back({String(kFileWhere), String(kMoreIssuesMessage)});
                    return;
                }
                _issues.push_back({Clean(where, PanelAssetLimits::kMaxIssueWhereBytes), Clean(message, PanelAssetLimits::kMaxIssueMessageBytes)});
            }

            [[nodiscard]] bool Full() const {
                return _issues.size() >= PanelAssetLimits::kMaxIssues;
            }

        private:
            Vector<PanelIssue>& _issues;
        };

        bool IsIdChar(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        }

        bool IsValidId(StringView id) {
            if (id.size() < PanelAssetLimits::kMinIdBytes || id.size() > PanelAssetLimits::kMaxIdBytes) {
                return false;
            }
            for (const char c : id) {
                if (!IsIdChar(c)) {
                    return false;
                }
            }
            return true;
        }

        /// The tree and the lists of an asset against the limits, the ids and the syntax of every name the file gives
        class Validator {
        public:
            Validator(const PanelValidationContext& context, Vector<PanelIssue>& issues) : _context(context), _issues(issues) {}

            void Run(const PanelAsset& asset) {
                _window = asset.window;
                CheckAsset(asset);
                WalkElements(asset.elements, "elements", 1, nullptr);
            }

        private:
            void CheckAsset(const PanelAsset& asset) {
                const StringView where = asset.id.empty() ? StringView(kFileWhere) : StringView(asset.id);
                CheckTexts(asset, where);
                if (!IsValidId(asset.id)) {
                    _issues.Add(kFileWhere, Format("id must be {} to {} characters of A-Z a-z 0-9 _ . -", PanelAssetLimits::kMinIdBytes, PanelAssetLimits::kMaxIdBytes));
                }
                if (asset.width < 0.0f || asset.height < 0.0f) {
                    _issues.Add(where, "width and height must not be negative");
                }
                Binding(where, "titleBind", asset.titleBind, false);
                if ((asset.window == PanelWindowKind::Dialogs || asset.window == PanelWindowKind::Bar) && !asset.open) {
                    _issues.Add(where, Format("a {} window is always open: it needs open=\"true\"", EnumUtils<PanelWindowKind>::ToString(asset.window)));
                }
                if (asset.sources.size() > PanelAssetLimits::kMaxSources) {
                    _issues.Add(where, Format("more than {} sources", PanelAssetLimits::kMaxSources));
                }

                HashSet<String> names;
                for (const PanelSource& source : asset.sources) {
                    CheckTexts(source, where);
                    if (!IsValidSourceName(source.name)) {
                        _issues.Add(where, Format("source '{}': the name must be letters, digits and '_', not starting with a digit", source.name));
                    } else if (!names.insert(source.name).second) {
                        _issues.Add(where, Format("source '{}' is declared twice", source.name));
                    }
                    if (!source.model.empty() && _context.isKnownModel && !(*_context.isKnownModel)(source.model)) {
                        _issues.Add(where, Format("source '{}': unknown model '{}'", source.name, source.model));
                    }
                }
            }

            void WalkElements(const Vector<IntrusivePtr<PanelElement>>& list, StringView parentPath, size_t depth, const PanelElement* parent) {
                if (list.empty()) {
                    return;
                }
                if (depth > PanelAssetLimits::kMaxDepth) {
                    _issues.Add(parentPath, Format("nested deeper than {} levels, the rest is not checked", PanelAssetLimits::kMaxDepth));
                    return;
                }
                for (size_t i = 0; i < list.size() && !_stopped && !_issues.Full(); ++i) {
                    const PanelElement* element = list[i].Get();
                    if (element == nullptr) {
                        continue;
                    }
                    if (++_elementCount > PanelAssetLimits::kMaxElements) {
                        _issues.Add(kFileWhere, Format("more than {} elements, the rest is not checked", PanelAssetLimits::kMaxElements));
                        _stopped = true;
                        return;
                    }
                    const String path = Format("{}[{}]", parentPath, i);
                    const String where = IsValidId(element->id) ? element->id : path;
                    CheckElement(*element, where, parent);
                    const bool isPopup = element->Is<PanelPopup>();
                    const bool isMenu = element->Is<PanelMenu>();
                    const bool isDropdown = element->Is<PanelDropdown>();
                    _popupDepth += isPopup ? 1 : 0;
                    _menuDepth += isMenu ? 1 : 0;
                    _dropdownDepth += isDropdown ? 1 : 0;
                    WalkElements(element->elements, Format("{}.elements", path), depth + 1, element);
                    _popupDepth -= isPopup ? 1 : 0;
                    _menuDepth -= isMenu ? 1 : 0;
                    _dropdownDepth -= isDropdown ? 1 : 0;
                }
            }

            void CheckElement(const PanelElement& element, StringView where, const PanelElement* parent) {
                CheckTexts(element, where);
                if (!IsValidId(element.id)) {
                    _issues.Add(where, Format("id must be {} to {} characters of A-Z a-z 0-9 _ . -", PanelAssetLimits::kMinIdBytes, PanelAssetLimits::kMaxIdBytes));
                } else if (!_ids.insert(element.id).second) {
                    _issues.Add(where, Format("id '{}' is used twice", element.id));
                }
                Condition(where, "visible", element.visible);
                Binding(where, "tooltipBind", element.tooltipBind, false);

                // An element with no `type` loads as the base class without a reader finding, and writes `type="PanelElement"`, which is no kind
                if (element.GetTypeMeta().typeName == kBaseElementType) {
                    _issues.Add(where, "type is required");
                }

                const bool isContainer = element.Is<PanelSection>() || element.Is<PanelRow>() || element.Is<PanelChild>() || element.Is<PanelPopup>() || element.Is<PanelDropdown>() ||
                                         element.Is<PanelTabs>() || element.Is<PanelTab>() || element.Is<PanelMenu>();
                if (!isContainer && !element.elements.empty()) {
                    _issues.Add(where, "only a section, a row, a child, a popup, a dropdown, a tab bar, a tab or a menu holds elements");
                }

                if (const auto* section = element.Cast<PanelSection>()) {
                    Binding(where, "openBind", section->openBind, false);
                } else if (const auto* text = element.Cast<PanelText>()) {
                    if (text->text.empty() && text->bind.empty()) {
                        _issues.Add(where, "a text needs 'text' or 'bind'");
                    }
                    Binding(where, "bind", text->bind, false);
                    ScrollLimit(where, "maxLines", text->maxLines, PanelAssetLimits::kMaxScrollLines);
                } else if (const auto* field = element.Cast<PanelField>()) {
                    Binding(where, "bind", field->bind, true);
                } else if (const auto* fields = element.Cast<PanelFields>()) {
                    ObjectBinding(where, "bind", fields->bind);
                } else if (const auto* input = element.Cast<PanelInput>()) {
                    Binding(where, "bind", input->bind, true);
                    if (input->width < 0.0f) {
                        _issues.Add(where, "width must not be negative");
                    }
                    OptionalCommand(where, "submitCommand", input->submitCommand);
                    OptionalCommand(where, "commitCommand", input->commitCommand);
                    Condition(where, "enableWhen", input->enableWhen);
                    if (input->focus && _popupDepth == 0) {
                        _issues.Add(where, "focus is for an input inside a popup");
                    }
                    ScrollLimit(where, "lines", input->lines, PanelAssetLimits::kMaxInputLines);
                    if (input->lines > 0 && !input->submitCommand.empty()) {
                        _issues.Add(where, "a multiline input has no submitCommand: Enter is a new line there");
                    }
                } else if (const auto* checkbox = element.Cast<PanelCheckbox>()) {
                    Binding(where, "bind", checkbox->bind, true);
                    Binding(where, "labelBind", checkbox->labelBind, false);
                    if (checkbox->text.empty() && checkbox->labelBind.empty()) {
                        _issues.Add(where, "a checkbox needs 'text' or 'labelBind'");
                    }
                    Condition(where, "enableWhen", checkbox->enableWhen);
                } else if (const auto* combo = element.Cast<PanelCombo>()) {
                    CheckCombo(*combo, where);
                } else if (const auto* number = element.Cast<PanelNumber>()) {
                    CheckNumber(*number, where);
                } else if (const auto* radio = element.Cast<PanelRadio>()) {
                    CheckRadio(*radio, where);
                } else if (const auto* button = element.Cast<PanelButton>()) {
                    Command(where, "command", button->command);
                    Condition(where, "enableWhen", button->enableWhen);
                    Condition(where, "fillWhen", button->fillWhen);
                    if (!button->fillWhen.empty() && button->fill == PanelButtonFill::None) {
                        _issues.Add(where, "fillWhen needs a fill");
                    }
                    Literal(where, "argument", button->argument);
                    if (button->width != kPanelFillLineWidth && !IsExtent(button->width)) {
                        _issues.Add(where, Format("width must be {} (the rest of the line) or from 0 to {}", kPanelFillLineWidth, PanelAssetLimits::kMaxChildExtent));
                    }
                    if (button->closePopup && _dropdownDepth == 0) {
                        _issues.Add(where, "closePopup is for a button inside a dropdown");
                    }
                } else if (const auto* progress = element.Cast<PanelProgress>()) {
                    Binding(where, "bind", progress->bind, true);
                    Binding(where, "textBind", progress->textBind, false);
                    Extent(where, "width", progress->width);
                } else if (const auto* menu = element.Cast<PanelMenu>()) {
                    CheckMenu(*menu, where);
                } else if (const auto* item = element.Cast<PanelMenuItem>()) {
                    CheckMenuItem(*item, where, parent);
                } else if (const auto* items = element.Cast<PanelMenuItems>()) {
                    CheckMenuItems(*items, where, parent);
                } else if (const auto* table = element.Cast<PanelTable>()) {
                    Binding(where, "source", table->source, true);
                    ScrollLimit(where, "scrollRows", table->scrollRows, PanelAssetLimits::kMaxScrollRows);
                    CheckColumns(*table, where);
                } else if (const auto* slot = element.Cast<PanelSlot>()) {
                    if (slot->name.empty()) {
                        _issues.Add(where, "name is required");
                    }
                } else if (const auto* child = element.Cast<PanelChild>()) {
                    Extent(where, "width", child->width);
                    Extent(where, "height", child->height);
                } else if (const auto* popup = element.Cast<PanelPopup>()) {
                    CheckPopup(*popup, where);
                } else if (const auto* dropdown = element.Cast<PanelDropdown>()) {
                    CheckDropdown(*dropdown, where);
                } else if (const auto* tabs = element.Cast<PanelTabs>()) {
                    CheckTabs(*tabs, where);
                } else if (const auto* tab = element.Cast<PanelTab>()) {
                    CheckTab(*tab, where, parent);
                }
            }

            void CheckTabs(const PanelTabs& tabs, StringView where) {
                if (tabs.elements.empty()) {
                    _issues.Add(where, "a tab bar needs tabs");
                }
                if (tabs.elements.size() > PanelAssetLimits::kMaxTabsPerBar) {
                    _issues.Add(where, Format("more than {} tabs in a bar", PanelAssetLimits::kMaxTabsPerBar));
                }
                for (const IntrusivePtr<PanelElement>& child : tabs.elements) {
                    if (child != nullptr && !child->Is<PanelTab>()) {
                        _issues.Add(where, "a tab bar holds only tabs");
                        return;
                    }
                }
            }

            void CheckTab(const PanelTab& tab, StringView where, const PanelElement* parent) {
                if (parent == nullptr || !parent->Is<PanelTabs>()) {
                    _issues.Add(where, "a tab belongs in a tab bar");
                }
                if (tab.title.empty()) {
                    _issues.Add(where, "title is required");
                }
                if (++_tabCount == PanelAssetLimits::kMaxTabs + 1) {
                    _issues.Add(where, Format("more than {} tabs in all", PanelAssetLimits::kMaxTabs));
                }
            }

            void CheckMenu(const PanelMenu& menu, StringView where) {
                if (_window != PanelWindowKind::Bar) {
                    _issues.Add(where, "a menu belongs in a Bar window");
                }
                if (menu.title.empty()) {
                    _issues.Add(where, "title is required");
                }
                Condition(where, "enableWhen", menu.enableWhen);
                OptionalCommand(where, "openCommand", menu.openCommand);
                // Reported once, at the first level over the limit: a deeper menu is over it too
                if (_menuDepth == PanelAssetLimits::kMaxMenuDepth) {
                    _issues.Add(where, Format("menus nested deeper than {} levels", PanelAssetLimits::kMaxMenuDepth));
                }
                if (++_menuCount == PanelAssetLimits::kMaxMenus + 1) {
                    _issues.Add(where, Format("more than {} menus", PanelAssetLimits::kMaxMenus));
                }
                for (const IntrusivePtr<PanelElement>& child : menu.elements) {
                    if (child != nullptr && !IsMenuChild(*child)) {
                        _issues.Add(where, "a menu holds only menus, items, separators and texts");
                        return;
                    }
                }
            }

            static bool IsMenuChild(const PanelElement& element) {
                return element.Is<PanelMenu>() || element.Is<PanelMenuItem>() || element.Is<PanelMenuItems>() || element.Is<PanelSeparator>() || element.Is<PanelText>();
            }

            void CheckMenuItem(const PanelMenuItem& item, StringView where, const PanelElement* parent) {
                if (parent == nullptr || !parent->Is<PanelMenu>()) {
                    _issues.Add(where, "a menu item belongs in a menu");
                }
                if (item.text.empty()) {
                    _issues.Add(where, "text is required");
                }
                Command(where, "command", item.command);
                Condition(where, "enableWhen", item.enableWhen);
                Literal(where, "argument", item.argument);
            }

            void CheckMenuItems(const PanelMenuItems& items, StringView where, const PanelElement* parent) {
                if (parent == nullptr || !parent->Is<PanelMenu>()) {
                    _issues.Add(where, "menu items belong in a menu");
                }
                Binding(where, "source", items.source, true);
                RelativePath(where, "itemField", items.itemField);
                Command(where, "command", items.command);
            }

            void CheckCombo(const PanelCombo& combo, StringView where) {
                Binding(where, "bind", combo.bind, true);
                Binding(where, "items", combo.items, false);
                if (combo.items.empty() && !combo.itemField.empty()) {
                    _issues.Add(where, "itemField needs items");
                }
                RelativePath(where, "itemField", combo.itemField);
                if (combo.items.empty() && !combo.separatorField.empty()) {
                    _issues.Add(where, "separatorField needs items");
                }
                RelativePath(where, "separatorField", combo.separatorField);
                if (combo.items.empty() && !combo.enableField.empty()) {
                    _issues.Add(where, "enableField needs items");
                }
                RelativePath(where, "enableField", combo.enableField);
                if (combo.items.empty() && !combo.tooltipField.empty()) {
                    _issues.Add(where, "tooltipField needs items");
                }
                RelativePath(where, "tooltipField", combo.tooltipField);
                Binding(where, "previewBind", combo.previewBind, false);
                Extent(where, "width", combo.width);
                Condition(where, "enableWhen", combo.enableWhen);
            }

            void CheckNumber(const PanelNumber& number, StringView where) {
                Binding(where, "bind", number.bind, true);
                Extent(where, "width", number.width);
                OptionalCommand(where, "commitCommand", number.commitCommand);
                Condition(where, "enableWhen", number.enableWhen);
                if (number.input && number.slider) {
                    _issues.Add(where, "a number is a slider or an input, not both");
                }
                if (number.stepper && (number.slider || number.input)) {
                    _issues.Add(where, "a stepper is neither a slider nor an input");
                }
                // Every limit is written as a comparison that a NaN fails
                const auto inRange = [](float value, float lowest) { return value >= lowest && value <= PanelAssetLimits::kMaxNumberExtent; };
                const bool minOk = inRange(number.min, -PanelAssetLimits::kMaxNumberExtent);
                const bool maxOk = inRange(number.max, -PanelAssetLimits::kMaxNumberExtent);
                if (!minOk || !maxOk) {
                    _issues.Add(where, Format("min and max must be from -{} to {}", PanelAssetLimits::kMaxNumberExtent, PanelAssetLimits::kMaxNumberExtent));
                } else if (number.min > number.max) {
                    _issues.Add(where, "min must not be above max");
                } else if (number.slider && !(number.min < number.max)) {
                    _issues.Add(where, "a slider needs min below max");
                }
                if (!inRange(number.step, 0.0f)) {
                    _issues.Add(where, Format("step must be from 0 to {}", PanelAssetLimits::kMaxNumberExtent));
                }
                if (!inRange(number.stepFast, 0.0f)) {
                    _issues.Add(where, Format("stepFast must be from 0 to {}", PanelAssetLimits::kMaxNumberExtent));
                } else if (number.stepFast != 0.0f && !number.stepper) {
                    _issues.Add(where, "stepFast needs stepper");
                }
                if (number.decimals > PanelAssetLimits::kMaxNumberDecimals) {
                    _issues.Add(where, Format("decimals must be at most {}", PanelAssetLimits::kMaxNumberDecimals));
                }
                Literal(where, "unit", number.unit);
            }

            void CheckRadio(const PanelRadio& radio, StringView where) {
                Binding(where, "bind", radio.bind, true);
                if (radio.value.empty()) {
                    _issues.Add(where, "value is required");
                } else if (radio.value.size() <= PanelAssetLimits::kMaxTextBytes) {  // past the limit it is reported by CheckTexts
                    PanelConditionTerm term;
                    term.compare = PanelCompare::Equal;
                    if (const auto literal = ParsePanelConditionLiteral(radio.value, term); !literal) {
                        _issues.Add(where, Format("value: {}", literal.error()));
                    }
                }
                if (radio.text.empty() && radio.icon.empty()) {
                    _issues.Add(where, "a radio needs 'text' or 'icon'");
                }
                Condition(where, "enableWhen", radio.enableWhen);
            }

            void CheckPopup(const PanelPopup& popup, StringView where) {
                if (popup.open.empty()) {
                    _issues.Add(where, "open is required");
                } else {
                    Condition(where, "open", popup.open);
                }
                OptionalCommand(where, "closeCommand", popup.closeCommand);
                if (_popupDepth > 0) {
                    _issues.Add(where, "a popup is not nested in a popup");
                }
                if (_dropdownDepth > 0) {
                    _issues.Add(where, "a popup is not nested in a dropdown");
                }
                if (++_popupCount == PanelAssetLimits::kMaxPopups + 1) {
                    _issues.Add(where, Format("more than {} popups", PanelAssetLimits::kMaxPopups));
                }
            }

            void CheckDropdown(const PanelDropdown& dropdown, StringView where) {
                if (dropdown.text.empty() && dropdown.icon.empty()) {
                    _issues.Add(where, "a dropdown needs 'text' or 'icon'");
                }
                Condition(where, "enableWhen", dropdown.enableWhen);
                OptionalCommand(where, "openCommand", dropdown.openCommand);
                if (_dropdownDepth > 0) {
                    _issues.Add(where, "a dropdown is not nested in a dropdown");
                }
                if (_popupDepth > 0) {
                    _issues.Add(where, "a dropdown is not nested in a popup");
                }
                if (++_dropdownCount == PanelAssetLimits::kMaxDropdowns + 1) {
                    _issues.Add(where, Format("more than {} dropdowns", PanelAssetLimits::kMaxDropdowns));
                }
            }

            void CheckColumns(const PanelTable& table, StringView tableWhere) {
                HashSet<String> ids;
                for (size_t i = 0; i < table.columns.size() && !_issues.Full(); ++i) {
                    const PanelColumn& column = table.columns[i];
                    if (++_columnCount > PanelAssetLimits::kMaxColumns) {
                        if (!_columnsReported) {
                            _issues.Add(tableWhere, Format("more than {} columns in all, the rest is not checked", PanelAssetLimits::kMaxColumns));
                            _columnsReported = true;
                        }
                        return;
                    }
                    const String columnName = IsValidId(column.id) ? column.id : Format("columns[{}]", i);
                    const String where = Format("{}.{}", tableWhere, columnName);
                    CheckTexts(column, where);
                    if (!IsValidId(column.id)) {
                        _issues.Add(where, Format("id must be {} to {} characters of A-Z a-z 0-9 _ . -", PanelAssetLimits::kMinIdBytes, PanelAssetLimits::kMaxIdBytes));
                    } else if (!ids.insert(column.id).second) {
                        _issues.Add(where, Format("column id '{}' is used twice in the table", column.id));
                    }
                    if (column.type == PanelColumnType::Button) {
                        Command(where, "command", column.command);
                        RelativePath(where, "field", column.field);
                    } else if (column.field.empty()) {
                        _issues.Add(where, "field is required");
                    } else {
                        RelativePath(where, "field", column.field);
                    }
                    RelativePath(where, "styleField", column.styleField);
                    RelativePath(where, "enableField", column.enableField);
                    if (column.type != PanelColumnType::Selectable && !column.selectedField.empty()) {
                        _issues.Add(where, "selectedField is for a Selectable column");
                    }
                    RelativePath(where, "selectedField", column.selectedField);
                    if (column.type != PanelColumnType::Checkbox && !column.labelField.empty()) {
                        _issues.Add(where, "labelField is for a Checkbox column");
                    }
                    RelativePath(where, "labelField", column.labelField);
                    RelativePath(where, "argument", column.argument);
                    RelativePath(where, "tooltipField", column.tooltipField);
                }
            }

            /// Every text field of the object is at most `kMaxTextBytes`; found through the reflection so a kind added later is covered
            template <typename T>
            void CheckTexts(const T& object, StringView where) {
                const TypedRef ref = TypedRef::Of(object);
                if (!ref) {
                    return;
                }
                for (const FieldRef& fieldRef : TypeRegistry::GetInstance().AllFields(*ref.type)) {
                    if (fieldRef.field->type == nullptr || fieldRef.field->type->kind != TypeKind::String) {
                        continue;
                    }
                    const auto value = GetProperty(ref, fieldRef.field->name);
                    if (value && std::holds_alternative<String>(*value) && std::get<String>(*value).size() > PanelAssetLimits::kMaxTextBytes) {
                        _issues.Add(where, Format("{} is longer than {} bytes", fieldRef.field->name, PanelAssetLimits::kMaxTextBytes));
                    }
                }
            }

            void Binding(StringView where, StringView field, const String& text, bool required) {
                if (text.empty()) {
                    if (required) {
                        _issues.Add(where, Format("{} is required", field));
                    }
                    return;
                }
                if (const auto parsed = ParseBinding(text); !parsed) {
                    _issues.Add(where, Format("{}: {}", field, parsed.error()));
                }
            }

            void ObjectBinding(StringView where, StringView field, const String& text) {
                if (text.empty()) {
                    _issues.Add(where, Format("{} is required", field));
                    return;
                }
                if (const auto parsed = ParseObjectBinding(text); !parsed) {
                    _issues.Add(where, Format("{}: {}", field, parsed.error()));
                }
            }

            void ScrollLimit(StringView where, StringView field, uint32_t value, size_t limit) {
                if (value > limit) {
                    _issues.Add(where, Format("{} must be at most {}", field, limit));
                }
            }

            static bool IsExtent(float value) {
                return std::isfinite(value) && value >= 0.0f && value <= PanelAssetLimits::kMaxChildExtent;
            }

            void Extent(StringView where, StringView field, float value) {
                if (!IsExtent(value)) {
                    _issues.Add(where, Format("{} must be from 0 to {}", field, PanelAssetLimits::kMaxChildExtent));
                }
            }

            void Condition(StringView where, StringView field, const String& text) {
                if (text.empty()) {
                    return;
                }
                if (const auto parsed = ParsePanelCondition(text); !parsed) {
                    _issues.Add(where, Format("{}: {}", field, parsed.error()));
                }
            }

            void RelativePath(StringView where, StringView field, const String& text) {
                if (text.empty()) {
                    return;
                }
                if (text.size() > PanelAssetLimits::kMaxTextBytes) {
                    return;  // reported by CheckTexts; a path is not parsed past the limit
                }
                if (const auto parsed = ParsePropertyPath(text); !parsed) {
                    _issues.Add(where, Format("{}: bad path: {}", field, parsed.error().message));
                }
            }

            /// A text that goes to the model as it is: `CheckTexts` checks its length, this only what would break a log line or a command
            void Literal(StringView where, StringView field, const String& text) {
                for (const char c : text) {
                    const auto byte = static_cast<uint8_t>(c);
                    if (byte < kFirstPrintable || byte == kDelete) {
                        _issues.Add(where, Format("{} must not hold a control character", field));
                        return;
                    }
                }
            }

            void Command(StringView where, StringView field, const String& command) {
                if (command.empty()) {
                    _issues.Add(where, Format("{} is required", field));
                } else if (_context.isKnownCommand && !(*_context.isKnownCommand)(command)) {
                    _issues.Add(where, Format("{}: unknown command '{}'", field, command));
                }
            }

            void OptionalCommand(StringView where, StringView field, const String& command) {
                if (!command.empty()) {
                    Command(where, field, command);
                }
            }

            const PanelValidationContext& _context;
            IssueList _issues;
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
            bool _columnsReported = false;
            bool _stopped = false;
        };

        /// The errors of a deserializer are issues; its paths and messages are cut and printable already
        void AddReaderErrors(const IDeserializer& deserializer, Vector<PanelIssue>& issues) {
            IssueList list(issues);
            for (const DeserializeError& error : deserializer.GetErrors()) {
                const String message = error.line > 0 ? Format("{} (line {})", error.errorMessage, error.line) : error.errorMessage;
                list.Add(error.path.empty() ? kFileWhere : StringView(error.path), message);
            }
        }

    }  // namespace

    void ValidatePanelAsset(const PanelAsset& asset, const PanelValidationContext& context, Vector<PanelIssue>& issues) {
        Validator(context, issues).Run(asset);
    }

    PanelLoadResult LoadPanelAsset(IDeserializer& deserializer, const PanelValidationContext& context) {
        deserializer.SetDiagnostics(DiagnosticsMode::Error);
        deserializer.SetStrictValues(true);

        PanelLoadResult result;
        result.asset = New<PanelAsset>();
        result.asset->Deserialize(deserializer);
        deserializer.FinishLoad();

        AddReaderErrors(deserializer, result.issues);
        ValidatePanelAsset(*result.asset, context, result.issues);
        return result;
    }

    PanelLoadResult LoadPanelAsset(StringView xmlText, const PanelValidationContext& context) {
        XmlDeserializer deserializer;
        if (!deserializer.LoadFromString(xmlText)) {
            PanelLoadResult result;
            AddReaderErrors(deserializer, result.issues);
            if (result.issues.empty()) {
                IssueList(result.issues).Add(kFileWhere, kNoParseMessage);
            }
            return result;
        }
        return LoadPanelAsset(deserializer, context);
    }

}  // namespace BECore
