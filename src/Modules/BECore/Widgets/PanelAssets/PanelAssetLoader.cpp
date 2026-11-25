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
                CheckAsset(asset);
                WalkElements(asset.elements, "elements", 1);
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

            void WalkElements(const Vector<IntrusivePtr<PanelElement>>& list, StringView parentPath, size_t depth) {
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
                    CheckElement(*element, where);
                    const bool isPopup = element->Is<PanelPopup>();
                    _popupDepth += isPopup ? 1 : 0;
                    WalkElements(element->elements, Format("{}.elements", path), depth + 1);
                    _popupDepth -= isPopup ? 1 : 0;
                }
            }

            void CheckElement(const PanelElement& element, StringView where) {
                CheckTexts(element, where);
                if (!IsValidId(element.id)) {
                    _issues.Add(where, Format("id must be {} to {} characters of A-Z a-z 0-9 _ . -", PanelAssetLimits::kMinIdBytes, PanelAssetLimits::kMaxIdBytes));
                } else if (!_ids.insert(element.id).second) {
                    _issues.Add(where, Format("id '{}' is used twice", element.id));
                }
                Condition(where, "visible", element.visible);

                // An element with no `type` loads as the base class without a reader finding, and writes `type="PanelElement"`, which is no kind
                if (element.GetTypeMeta().typeName == kBaseElementType) {
                    _issues.Add(where, "type is required");
                }

                const bool isContainer = element.Is<PanelSection>() || element.Is<PanelRow>() || element.Is<PanelChild>() || element.Is<PanelPopup>();
                if (!isContainer && !element.elements.empty()) {
                    _issues.Add(where, "only a section, a row, a child or a popup holds elements");
                }

                if (const auto* text = element.Cast<PanelText>()) {
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
                } else if (const auto* checkbox = element.Cast<PanelCheckbox>()) {
                    Binding(where, "bind", checkbox->bind, true);
                    Binding(where, "labelBind", checkbox->labelBind, false);
                    if (checkbox->text.empty() && checkbox->labelBind.empty()) {
                        _issues.Add(where, "a checkbox needs 'text' or 'labelBind'");
                    }
                } else if (const auto* combo = element.Cast<PanelCombo>()) {
                    CheckCombo(*combo, where);
                } else if (const auto* number = element.Cast<PanelNumber>()) {
                    CheckNumber(*number, where);
                } else if (const auto* button = element.Cast<PanelButton>()) {
                    Command(where, "command", button->command);
                    Condition(where, "enableWhen", button->enableWhen);
                    Literal(where, "argument", button->argument);
                    Extent(where, "width", button->width);
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
                }
            }

            void CheckCombo(const PanelCombo& combo, StringView where) {
                Binding(where, "bind", combo.bind, true);
                Binding(where, "items", combo.items, false);
                if (combo.items.empty() && !combo.itemField.empty()) {
                    _issues.Add(where, "itemField needs items");
                }
                RelativePath(where, "itemField", combo.itemField);
                Extent(where, "width", combo.width);
            }

            void CheckNumber(const PanelNumber& number, StringView where) {
                Binding(where, "bind", number.bind, true);
                Extent(where, "width", number.width);
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
            }

            void CheckPopup(const PanelPopup& popup, StringView where) {
                if (popup.open.empty()) {
                    _issues.Add(where, "open is required");
                } else {
                    Condition(where, "open", popup.open);
                }
                if (_popupDepth > 0) {
                    _issues.Add(where, "a popup is not nested in a popup");
                }
                if (++_popupCount == PanelAssetLimits::kMaxPopups + 1) {
                    _issues.Add(where, Format("more than {} popups", PanelAssetLimits::kMaxPopups));
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
                    RelativePath(where, "argument", column.argument);
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

            void Extent(StringView where, StringView field, float value) {
                if (!std::isfinite(value) || value < 0.0f || value > PanelAssetLimits::kMaxChildExtent) {
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

            /// A text that goes to the model as it is: the length is `CheckTexts`', here only what would break a log line or a command
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

            const PanelValidationContext& _context;
            IssueList _issues;
            HashSet<String> _ids;
            size_t _elementCount = 0;
            size_t _columnCount = 0;
            size_t _popupCount = 0;
            size_t _popupDepth = 0;
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
