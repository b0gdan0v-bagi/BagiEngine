#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonStreamSerializer.h>
#include <BECore/Control/JsonTape.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonViewOps.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Reflection/MethodCall.h>
#include <BECore/Reflection/TypedRef.h>
#include <Fuzz/FuzzChecks.h>
#include <Fuzz/FuzzJsonText.h>
#include <Fuzz/FuzzProbeAccess.h>
#include <Fuzz/FuzzTargets.h>

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
            return reading;
        }

        /// From the text as it is, with the reader's own tape, into @p state
        template <typename T>
        Reading ReadInto(StringView text, T& state, DiagnosticsMode mode) {
            JsonDeserializer reader;
            FUZZ_CHECK(reader.Load(text));
            return Summarize(reader, state, mode);
        }

        template <typename T>
        Reading ReadText(StringView text, DiagnosticsMode mode) {
            T state;
            return ReadInto(text, state, mode);
        }

        /// The state saved by the serializer and read again: the same state, no complaint, and the text it writes is JSON that the tape takes back
        /// and that saves to the same text again
        template <typename T>
        void CheckRoundTrip(StringView text) {
            T loaded;
            const Reading first = ReadInto(text, loaded, DiagnosticsMode::Off);

            const String saved = SavedText(loaded);
            T reloaded;
            // Strict: a bad value in the written document would be an error, an unknown key a finding
            const Reading again = ReadInto(saved, reloaded, DiagnosticsMode::Error);
            FUZZ_CHECK(again.errors == 0);
            FUZZ_CHECK(again.digest == first.digest);
            FUZZ_CHECK(SavedText(reloaded) == saved);
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

        /// One type read from the text under every mode, by one reader that loads the text twice, twice by fresh objects, and saved and read again
        template <typename T>
        void CheckType(StringView text) {
            const Reading off = ReadText<T>(text, DiagnosticsMode::Off);
            const Reading warn = ReadText<T>(text, DiagnosticsMode::Warn);
            const Reading error = ReadText<T>(text, DiagnosticsMode::Error);
            CheckModes(off, warn, error);

            // Same input, fresh objects: the same state and the same complaints
            const Reading twice = ReadText<T>(text, DiagnosticsMode::Off);
            FUZZ_CHECK(twice.digest == off.digest);
            FUZZ_CHECK(twice.text == off.text);

            // A reader that served another document (here the same one) starts clean: Load resets the errors and findings and keeps the mode
            T first;
            T second;
            JsonDeserializer reader;
            FUZZ_CHECK(reader.Load(text));
            const Reading once = Summarize(reader, first, DiagnosticsMode::Warn);
            FUZZ_CHECK(reader.Load(text));
            const Reading repeated = Summarize(reader, second, DiagnosticsMode::Warn);
            FUZZ_CHECK(once.digest == repeated.digest);
            FUZZ_CHECK(once.text == repeated.text);
            FUZZ_CHECK(once.digest == warn.digest);

            CheckRoundTrip<T>(text);
        }

        /// What every method of the probe did with the arguments: whether it ran, and the state it left
        using CallOutcomes = Vector<uint64_t>;

        /// The document is the arguments of every method of the probe: refused (and the object as it was) or run once with a result that is JSON.
        /// @p loadArguments fills the reader a call takes: from the text, or from a view of a tape the caller keeps (the way `call_method` reads)
        template <typename TLoadArguments>
        CallOutcomes CheckCalls(TLoadArguments&& loadArguments) {
            CallOutcomes outcomes;
            ProbeCalls probe;
            const TypedRef ref = TypedRef::Of(probe);
            FUZZ_CHECK(ref);

            for (const MethodDesc& method : ref.type->methods) {
                const int32_t callsBefore = probe.calls;
                const uint64_t before = DigestProbe(probe);

                JsonDeserializer arguments;
                loadArguments(arguments);
                String resultText;
                JsonWriter resultWriter(resultText);
                JsonStreamSerializer result(resultWriter);
                const auto outcome = CallMethod(ref, method.name, arguments, result);
                result.Finish();
                outcomes.push_back(DigestProbe(probe) * 2 + (outcome.has_value() ? 1 : 0));

                // The result is one object with at most the member `result`; a refused call wrote nothing
                JsonTape resultTape;
                FUZZ_CHECK(resultWriter.IsComplete() && resultTape.Parse(resultText).has_value());
                const JsonView written(resultTape, 0);
                FUZZ_CHECK(written.IsObject());

                FUZZ_CHECK(arguments.GetDiagnostics() == DiagnosticsMode::Off);
                if (outcome.has_value()) {
                    FUZZ_CHECK(method.callable);
                    FUZZ_CHECK(!arguments.HasErrors());
                    FUZZ_CHECK(probe.calls == callsBefore + 1);
                    FUZZ_CHECK(written.Size() <= 1);
                } else {
                    FUZZ_CHECK(outcome.error().status == (method.callable ? CallStatus::bad_arguments : CallStatus::not_callable));
                    FUZZ_CHECK(!outcome.error().message.empty());
                    FUZZ_CHECK(!HasControlCharacter(outcome.error().message));
                    FUZZ_CHECK(DigestProbe(probe) == before);
                    FUZZ_CHECK(written.Size() == 0);
                }
            }

            const uint64_t before = DigestProbe(probe);
            JsonDeserializer arguments;
            loadArguments(arguments);
            String resultText;
            JsonWriter resultWriter(resultText);
            JsonStreamSerializer result(resultWriter);
            const auto unknown = CallMethod(ref, kUnknownMethod, arguments, result);
            FUZZ_CHECK(!unknown.has_value() && unknown.error().status == CallStatus::unknown_method);
            FUZZ_CHECK(DigestProbe(probe) == before);
            return outcomes;
        }

        /// A document the tape accepts, written again by `WriteJson`: the text is a document the tape accepts, it is written again as the same text, and
        /// its keys in byte order are the same document with no key twice. What the writer cannot say (a number past a double, a container deeper
        /// than the writer) is `null`, so the first text is not the input but the fixed point of this check
        void CheckWriteBack(JsonView document) {
            String written;
            JsonWriter writer(written);
            WriteJson(writer, document);
            FUZZ_CHECK(writer.IsComplete());

            JsonTape writtenTape;
            FUZZ_CHECK(writtenTape.Parse(written).has_value());
            String again;
            JsonWriter againWriter(again);
            WriteJson(againWriter, JsonView(writtenTape, 0));
            FUZZ_CHECK(again == written);

            String sorted;
            JsonWriter sortedWriter(sorted);
            WriteJsonSorted(sortedWriter, document);
            FUZZ_CHECK(sortedWriter.IsComplete());
            JsonTape sortedTape;
            FUZZ_CHECK(sortedTape.Parse(sorted).has_value());
            const JsonView sortedView(sortedTape, 0);
            // A key twice is gone, so the document equals itself (it does not while one is left: the second finds the first)
            FUZZ_CHECK(JsonEquals(sortedView, sortedView));
            String sortedAgain;
            JsonWriter sortedAgainWriter(sortedAgain);
            WriteJsonSorted(sortedAgainWriter, sortedView);
            FUZZ_CHECK(sortedAgain == sorted);
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

        if (accepted) {
            CheckWriteBack(JsonView(tape, 0));
            CheckType<ProbeState>(text);
            CheckType<ProbeNode>(text);
            CheckType<ProbeShapes>(text);
            const CallOutcomes fromText = CheckCalls([&](JsonDeserializer& reader) { FUZZ_CHECK(reader.Load(text)); });
            // The arguments of `call_method` are a view of the request's tape
            const CallOutcomes fromView = CheckCalls([&](JsonDeserializer& reader) { FUZZ_CHECK(reader.Load(JsonView(tape, 0))); });
            FUZZ_CHECK(fromText == fromView);
        }
    }

}  // namespace BECore::Fuzz
