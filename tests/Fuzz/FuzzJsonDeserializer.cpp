#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/Reflection/MethodCall.h>
#include <BECore/Reflection/TypedRef.h>
#include <Fuzz/FuzzProbeAccess.h>
#include <Fuzz/FuzzTargets.h>

namespace BECore::Fuzz {

    namespace {

        using FuzzProbeData::ProbeNode;

        constexpr uint8_t kLastControlCharacter = 0x1F;
        constexpr uint8_t kDelete = 0x7F;

        /// A name no type has: the call must be refused whatever the document says
        constexpr StringView kUnknownMethod = "FuzzNoSuchMethod";

        bool HasControlCharacter(StringView text) {
            for (const char c : text) {
                const auto byte = static_cast<uint8_t>(c);
                if (byte <= kLastControlCharacter || byte == kDelete) {
                    return true;
                }
            }
            return false;
        }

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

        template <typename T>
        Reading ReadInto(const Json& document, T& state, DiagnosticsMode mode) {
            JsonDeserializer reader(document);
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

        template <typename T>
        Reading Read(const Json& document, DiagnosticsMode mode) {
            T state;
            return ReadInto(document, state, mode);
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
        }

        /// One type read from the document under every mode of diagnostics: the findings change nothing that was loaded
        template <typename T>
        void CheckType(const Json& document) {
            const Reading off = Read<T>(document, DiagnosticsMode::Off);
            const Reading warn = Read<T>(document, DiagnosticsMode::Warn);
            const Reading error = Read<T>(document, DiagnosticsMode::Error);

            FUZZ_CHECK(off.warnings == 0);
            FUZZ_CHECK(warn.digest == off.digest);
            FUZZ_CHECK(warn.errors == off.errors);
            FUZZ_CHECK(error.digest == off.digest);
            FUZZ_CHECK(error.errors >= off.errors);
            FUZZ_CHECK(error.warnings == 0);

            // Same input, fresh objects: the same state and the same complaints
            const Reading twice = Read<T>(document, DiagnosticsMode::Off);
            FUZZ_CHECK(twice.digest == off.digest);
            FUZZ_CHECK(twice.text == off.text);

            CheckRoundTrip<T>(document);
        }

        /// The document is the arguments of every method of the probe: refused (and the object as it was) or run once with a result that is JSON
        void CheckCalls(const Json& document) {
            ProbeCalls probe;
            const TypedRef ref = TypedRef::Of(probe);
            FUZZ_CHECK(ref);

            for (const MethodDesc& method : ref.type->methods) {
                const int32_t callsBefore = probe.calls;
                const uint64_t before = DigestProbe(probe);

                JsonDeserializer arguments(document);
                JsonSerializer result;
                const auto outcome = CallMethod(ref, method.name, arguments, result);
                const Json written = result.Take();

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
            JsonDeserializer arguments(document);
            JsonSerializer result;
            const auto unknown = CallMethod(ref, kUnknownMethod, arguments, result);
            FUZZ_CHECK(!unknown.has_value() && unknown.error().status == CallStatus::unknown_method);
            FUZZ_CHECK(DigestProbe(probe) == before);
        }

    }  // namespace

    void FuzzJsonDeserializer(const uint8_t* data, size_t size) {
        const StringView text(reinterpret_cast<const char*>(data), size);
        // As ControlService does: a value that is kept, copied or dumped recurses once per level
        if (IsJsonNestedDeeperThan(text, ControlService::kMaxRequestDepth)) {
            return;
        }
        auto parsed = ParseJson(text);
        if (!parsed.has_value()) {
            return;
        }
        const Json document = std::move(*parsed);

        CheckType<FuzzProbeData::ProbeState>(document);
        CheckType<ProbeNode>(document);
        CheckType<ProbeShapes>(document);
        CheckCalls(document);
    }

}  // namespace BECore::Fuzz
