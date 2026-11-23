#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/Control/JsonTape.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Reflection/MethodCall.h>
#include <BECore/Reflection/TypedRef.h>
#include <Fuzz/FuzzChecks.h>
#include <Fuzz/FuzzProbeAccess.h>
#include <Fuzz/FuzzTargets.h>
#include <cmath>

namespace BECore::Fuzz {

    namespace {

        using FuzzProbeData::ProbeNode;
        using FuzzProbeData::ProbeState;

        /// A name no type has: the call must be refused whatever the document says
        constexpr StringView kUnknownMethod = "FuzzNoSuchMethod";

        /// What one load of a type gave: the state it reached (as a digest), and what it reported
        struct Reading {
            uint64_t digest = 0;
            size_t errors = 0;
            size_t warnings = 0;
            String text;
            /// The path of every error and finding, sorted: the two readers agree on which fields complain, not on the order of a scope's findings
            Vector<String> paths;
        };

        void AddList(Reading& reading, const Vector<DeserializeError>& list) {
            // The list is capped, and every path and message is cut and free of control characters (they reach a log and a reply)
            FUZZ_CHECK(list.size() <= ReaderLimits::kMaxRecordedErrors);
            for (const DeserializeError& entry : list) {
                const StringView path = entry.path;
                const StringView message = entry.errorMessage;
                FUZZ_CHECK(path.size() <= ReaderLimits::kMaxErrorTextBytes);
                FUZZ_CHECK(message.size() <= ReaderLimits::kMaxErrorTextBytes);
                FUZZ_CHECK(!HasControlCharacter(path));
                FUZZ_CHECK(!HasControlCharacter(message));
                reading.text.append(path.data(), path.size());
                reading.text += ": ";
                reading.text.append(message.data(), message.size());
                reading.text += '\n';
                reading.paths.push_back(String(path));
            }
        }

        /// What @p state and the findings of @p reader look like after the load
        template <typename T>
        Reading Summarize(JsonDeserializer& reader, T& state, DiagnosticsMode mode) {
            reader.SetDiagnostics(mode);
            state.Deserialize(reader);
            reader.FinishLoad();
            FUZZ_CHECK(reader.IsStrictValues());

            Reading reading;
            reading.errors = reader.GetErrors().size();
            reading.warnings = reader.GetWarnings().size();
            AddList(reading, reader.GetErrors());
            AddList(reading, reader.GetWarnings());
            reading.digest = DigestProbe(state);
            eastl::sort(reading.paths.begin(), reading.paths.end());
            return reading;
        }

        /// Through the bridge: the DOM written as text and loaded
        template <typename T>
        Reading ReadInto(const Json& document, T& state, DiagnosticsMode mode) {
            JsonDeserializer reader(document);
            return Summarize(reader, state, mode);
        }

        template <typename T>
        Reading Read(const Json& document, DiagnosticsMode mode) {
            T state;
            return ReadInto(document, state, mode);
        }

        /// From the text as it is, with the reader's own tape
        template <typename T>
        Reading ReadText(StringView text, DiagnosticsMode mode) {
            T state;
            JsonDeserializer reader;
            FUZZ_CHECK(reader.Load(text));
            return Summarize(reader, state, mode);
        }

        /// The two readers load the same document alike: the same state, and the same fields complain
        void ExpectSameReading(const Reading& fromText, const Reading& fromDom) {
            FUZZ_CHECK(fromText.digest == fromDom.digest);
            FUZZ_CHECK(fromText.errors == fromDom.errors);
            FUZZ_CHECK(fromText.warnings == fromDom.warnings);
            // Past the cap the lists keep the first findings, and those are not the same ones when the order of the findings is another
            if (fromText.errors < ReaderLimits::kMaxRecordedErrors && fromText.warnings < ReaderLimits::kMaxRecordedErrors) {
                FUZZ_CHECK(fromText.paths == fromDom.paths);
            }
        }

        /// The state saved by the serializer and read again: the same state, no complaint, and the text it writes is JSON that the parser takes back
        template <typename T>
        void CheckRoundTrip(const Json& document) {
            T loaded;
            const Reading first = ReadInto(document, loaded, DiagnosticsMode::Off);

            JsonSerializer writer;
            loaded.Serialize(writer);
            const Json saved = writer.Take();
            const String text = DumpJson(saved);
            FUZZ_CHECK(!IsJsonNestedDeeperThan(text, ControlService::kMaxRequestDepth * 2));

            const auto parsed = ParseJson(text);
            FUZZ_CHECK(parsed.has_value());
            FUZZ_CHECK(DumpJson(*parsed) == text);

            // Strict: a bad value in the written document would be an error, an unknown key a finding
            const Reading again = Read<T>(saved, DiagnosticsMode::Error);
            FUZZ_CHECK(again.errors == 0);
            FUZZ_CHECK(again.digest == first.digest);

            // The text goes through the parser (numbers become doubles, strings are unescaped) and gives the same state
            const Reading reparsed = Read<T>(*parsed, DiagnosticsMode::Error);
            FUZZ_CHECK(reparsed.errors == 0);
            FUZZ_CHECK(reparsed.digest == first.digest);

            // The tape reads the written text itself, not a copy through a DOM
            const Reading fromText = ReadText<T>(text, DiagnosticsMode::Error);
            FUZZ_CHECK(fromText.errors == 0);
            FUZZ_CHECK(fromText.digest == first.digest);
        }

        /// The same document read under every mode of diagnostics: the findings change nothing that was loaded
        void CheckModes(const Reading& off, const Reading& warn, const Reading& error) {
            FUZZ_CHECK(off.warnings == 0);
            FUZZ_CHECK(warn.digest == off.digest);
            FUZZ_CHECK(warn.errors == off.errors);
            FUZZ_CHECK(error.digest == off.digest);
            FUZZ_CHECK(error.errors >= off.errors);
            FUZZ_CHECK(error.warnings == 0);
        }

        /// One type read from the DOM (through the bridge) under every mode, twice, and saved and read again
        template <typename T>
        void CheckType(const Json& document) {
            const Reading off = Read<T>(document, DiagnosticsMode::Off);
            CheckModes(off, Read<T>(document, DiagnosticsMode::Warn), Read<T>(document, DiagnosticsMode::Error));

            // Same input, fresh objects: the same state and the same complaints
            const Reading twice = Read<T>(document, DiagnosticsMode::Off);
            FUZZ_CHECK(twice.digest == off.digest);
            FUZZ_CHECK(twice.text == off.text);

            CheckRoundTrip<T>(document);
        }

        /// One type read from the text under every mode, and by one reader that loads the text twice; when the text is also the DOM @p document that
        /// has no repeated key, the DOM's reading is the same in each mode
        template <typename T>
        void CheckTextType(StringView text, const Json* document) {
            const Reading off = ReadText<T>(text, DiagnosticsMode::Off);
            const Reading warn = ReadText<T>(text, DiagnosticsMode::Warn);
            const Reading error = ReadText<T>(text, DiagnosticsMode::Error);
            CheckModes(off, warn, error);

            // A reader that served another document (here the same one) starts clean: Load resets the errors and findings and keeps the mode
            T first;
            T second;
            JsonDeserializer reader;
            FUZZ_CHECK(reader.Load(text));
            const Reading once = Summarize(reader, first, DiagnosticsMode::Warn);
            FUZZ_CHECK(reader.Load(text));
            const Reading again = Summarize(reader, second, DiagnosticsMode::Warn);
            FUZZ_CHECK(once.digest == again.digest);
            FUZZ_CHECK(once.text == again.text);
            FUZZ_CHECK(once.digest == warn.digest);

            if (document != nullptr) {
                ExpectSameReading(off, Read<T>(*document, DiagnosticsMode::Off));
                ExpectSameReading(warn, Read<T>(*document, DiagnosticsMode::Warn));
                ExpectSameReading(error, Read<T>(*document, DiagnosticsMode::Error));
            }
        }

        /// Whether a number of the tape is beyond the range of a double: the DOM refuses the document ("number overflow"), the tape keeps the text
        /// and the reader refuses a read of it into a float
        bool HasOverflowingNumber(const JsonTape& tape) {
            for (uint32_t index = 0; index < tape.Size(); ++index) {
                const JsonView value(tape, index);
                if (value.IsNumber() && !std::isfinite(value.AsDouble().value_or(0.0))) {
                    return true;
                }
            }
            return false;
        }

        /// The text is a document that the tape accepts: the DOM that the parser made of it has the same kinds, sizes, strings and integers. Two things
        /// the readers answer differently on purpose set @p readsApart, and the readings are then not compared: a repeated key (an object that keeps
        /// fewer members than the tape counts; the DOM keeps the last, the tape finds the first, and the members of that object are not walked) and
        /// `-0`, which the DOM keeps as the integer 0 and the tape reads into a double as -0.0
        void CompareDocuments(const Json& dom, const JsonView view, bool& readsApart) {
            if (dom.is_null()) {
                FUZZ_CHECK(view.IsNull());
            } else if (dom.is_boolean()) {
                FUZZ_CHECK(view.IsBool());
                FUZZ_CHECK(view.AsBool() == dom.get<bool>());
            } else if (dom.is_number()) {
                FUZZ_CHECK(view.IsNumber());
                readsApart = readsApart || view.Raw() == "-0";
                // A number the DOM keeps as a float may be an integer of the tape (one past 64 bits): only what both call an integer is compared
                if (dom.is_number_unsigned()) {
                    FUZZ_CHECK(view.AsUint64() == dom.get<uint64_t>());
                } else if (dom.is_number_integer()) {
                    FUZZ_CHECK(view.AsInt64() == dom.get<int64_t>());
                }
            } else if (dom.is_string()) {
                FUZZ_CHECK(view.IsString());
                String decoded;
                FUZZ_CHECK(view.ReadText(decoded));
                const Optional<StringView> expected = JsonStringView(dom);
                FUZZ_CHECK(expected.has_value() && StringView(decoded) == *expected);
            } else if (dom.is_array()) {
                FUZZ_CHECK(view.IsArray());
                FUZZ_CHECK(view.Size() == dom.size());
                auto domItem = dom.begin();
                for (const JsonView item : view.Items()) {
                    CompareDocuments(*domItem, item, readsApart);
                    ++domItem;
                }
            } else {
                FUZZ_CHECK(dom.is_object());
                FUZZ_CHECK(view.IsObject());
                if (view.Size() != dom.size()) {
                    FUZZ_CHECK(view.Size() > dom.size());
                    readsApart = true;
                    return;
                }
                for (const JsonMember member : view.Members()) {
                    String key;
                    FUZZ_CHECK(member.key.ReadText(key));
                    const Json* value = FindJsonMember(dom, key);
                    FUZZ_CHECK(value != nullptr);
                    CompareDocuments(*value, member.value, readsApart);
                }
            }
        }

        /// What every method of the probe did with the arguments: whether it ran, and the state it left
        using CallOutcomes = Vector<uint64_t>;

        /// The document is the arguments of every method of the probe: refused (and the object as it was) or run once with a result that is JSON.
        /// @p makeArguments fills the reader a call takes (the reader is not movable): from the DOM through the bridge, or from the text
        template <typename TMakeArguments>
        CallOutcomes CheckCalls(TMakeArguments&& makeArguments) {
            CallOutcomes outcomes;
            ProbeCalls probe;
            const TypedRef ref = TypedRef::Of(probe);
            FUZZ_CHECK(ref);

            for (const MethodDesc& method : ref.type->methods) {
                const int32_t callsBefore = probe.calls;
                const uint64_t before = DigestProbe(probe);

                Optional<JsonDeserializer> reader;
                makeArguments(reader);
                JsonDeserializer& arguments = *reader;
                JsonSerializer result;
                const auto outcome = CallMethod(ref, method.name, arguments, result);
                const Json written = result.Take();
                outcomes.push_back(DigestProbe(probe) * 2 + (outcome.has_value() ? 1 : 0));

                FUZZ_CHECK(arguments.GetDiagnostics() == DiagnosticsMode::Off);
                if (outcome.has_value()) {
                    FUZZ_CHECK(method.callable);
                    FUZZ_CHECK(!arguments.HasErrors());
                    FUZZ_CHECK(probe.calls == callsBefore + 1);
                    FUZZ_CHECK(written.is_object() && written.size() <= 1);
                    FUZZ_CHECK(ParseJson(DumpJson(written)).has_value());
                } else {
                    FUZZ_CHECK(outcome.error().status == (method.callable ? CallStatus::bad_arguments : CallStatus::not_callable));
                    FUZZ_CHECK(!outcome.error().message.empty());
                    FUZZ_CHECK(!HasControlCharacter(outcome.error().message));
                    FUZZ_CHECK(DigestProbe(probe) == before);
                    FUZZ_CHECK(written.is_object() && written.empty());
                }
            }

            const uint64_t before = DigestProbe(probe);
            Optional<JsonDeserializer> reader;
            makeArguments(reader);
            JsonSerializer result;
            const auto unknown = CallMethod(ref, kUnknownMethod, *reader, result);
            FUZZ_CHECK(!unknown.has_value() && unknown.error().status == CallStatus::unknown_method);
            FUZZ_CHECK(DigestProbe(probe) == before);
            return outcomes;
        }

    }  // namespace

    void FuzzJsonDeserializer(const uint8_t* data, size_t size) {
        const StringView text(reinterpret_cast<const char*>(data), size);

        // The tape takes any text as it comes, with no check of its own before it: a refusal is an error of the reader, never a crash
        JsonTape tape;
        const bool accepted = tape.Parse(text).has_value();
        {
            JsonDeserializer reader;
            FUZZ_CHECK(reader.Load(text) == accepted);
            FUZZ_CHECK(reader.HasErrors() == !accepted);
            if (!accepted) {
                FUZZ_CHECK(reader.GetErrors().size() == 1);
                FUZZ_CHECK(reader.GetErrors()[0].path.empty());
                FUZZ_CHECK(IsTidyMessage(reader.GetErrors()[0].errorMessage));
                ProbeState state;
                state.Deserialize(reader);
                FUZZ_CHECK(DigestProbe(state) == DigestProbe(ProbeState{}));
                FUZZ_CHECK(reader.HasErrors());
            }
        }

        // As ControlService does: a value that is kept, copied or dumped by the DOM recurses once per level. A text deeper than that is read by the
        // tape alone; the nesting the tape refuses (kMaxJsonNesting) is more than that, so the two never disagree on it here
        Optional<Json> dom;
        if (!IsJsonNestedDeeperThan(text, ControlService::kMaxRequestDepth)) {
            auto parsed = ParseJson(text);
            // The known differences of acceptance: the DOM skips a UTF-8 byte order mark, its lexer takes a NUL byte outside a string for the end
            // of the input (`{}`, a NUL and anything is a document to it), and it refuses a number beyond the range of a double, which the tape keeps
            constexpr StringView kByteOrderMark = "\xEF\xBB\xBF";
            const bool domLenient = text.starts_with(kByteOrderMark) || text.find('\0') != StringView::npos;
            if (!domLenient) {
                FUZZ_CHECK(parsed.has_value() == accepted || (accepted && HasOverflowingNumber(tape)));
            } else if (accepted) {
                FUZZ_CHECK(parsed.has_value() || HasOverflowingNumber(tape));
            }
            if (parsed.has_value()) {
                dom = std::move(*parsed);
            }
        }

        if (accepted) {
            const Json* document = dom.has_value() ? &*dom : nullptr;
            bool readsApart = false;
            if (document != nullptr) {
                CompareDocuments(*document, JsonView(tape, 0), readsApart);
            }
            const Json* compared = readsApart ? nullptr : document;
            CheckTextType<ProbeState>(text, compared);
            CheckTextType<ProbeNode>(text, compared);
            CheckTextType<ProbeShapes>(text, compared);
            const CallOutcomes fromText = CheckCalls([&](Optional<JsonDeserializer>& reader) {
                reader.emplace();
                FUZZ_CHECK(reader->Load(text));
            });
            if (document != nullptr && !readsApart) {
                const CallOutcomes fromDom = CheckCalls([&](Optional<JsonDeserializer>& reader) { reader.emplace(*document); });
                FUZZ_CHECK(fromText == fromDom);
            }
        }

        if (dom.has_value()) {
            CheckType<ProbeState>(*dom);
            CheckType<ProbeNode>(*dom);
            CheckType<ProbeShapes>(*dom);
        }
    }

}  // namespace BECore::Fuzz
