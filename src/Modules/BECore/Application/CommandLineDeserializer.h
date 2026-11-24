#pragma once

#include <BECore/Application/CommandLine.h>
#include <BECore/Reflection/IDeserializer.h>

namespace BECore {

    /// The longest flag a field can have, `--` included. A longer key is not found (and is no flag): the name is built in a buffer of this
    /// size so a lookup allocates nothing
    inline constexpr size_t kMaxFlagBytes = 64;

    using FlagBuffer = Array<char, kMaxFlagBytes>;

    /**
     * @brief The flag of a field: `--` and the key in kebab case (`controlPort` is `--control-port`, `headless` is `--headless`)
     *
     * Every upper case letter starts a new word, so a run of capitals is a word each (`url` is not `URL`). The text lives in @p buffer;
     * the result is empty when it does not fit.
     */
    StringView MakeFlagName(StringView key, FlagBuffer& buffer);

    /**
     * @brief Reads the fields of a reflected struct from the `--name` and `--name=value` arguments of a CommandLine
     *
     * One flag is one flat value: the key of a field is the name of the flag (MakeFlagName), `Read` and `ReadAttribute` are the same, and a
     * struct or an array is no flag (`BeginObject` and `BeginArray` fail). A flag that is not on the command line leaves the field as it
     * is and is no error, so the reader can run over a struct that a file already filled: the command line then wins field by field.
     *
     * Values are always strict, with the grammar of XmlDeserializer (StrictText): a bool is a bare `--x` (true) or `--x=true|false|1|0`; a
     * number is `--x=<number>` of the field's type, and a bare number flag is the error `needs a value`; a `String` or `PoolString` takes
     * the text after the `=`, which may be empty (a bare one is `needs a value` too). An error is recorded under the **flag** (`--x`),
     * not the key, so the message can be shown to the person who typed it. A repeated flag is read once, its first form wins.
     *
     * With diagnostics `Warn` (or `Error`), FinishLoad() reports an argument that starts with `--`, that nothing asked for, and whose
     * name is near an asked flag, as a typo (`Unknown flag '--contol-port', did you mean '--control-port'?`). Any other unknown argument
     * is silent: a game has flags the engine does not know.
     */
    class CommandLineDeserializer final : public IDeserializer {
    public:
        /**
         * @param line outlives the reader
         * @param keys the keys of the fields that are flags (`controlPort`), the views outlive the reader; empty means every key. A key
         *             that is not listed is not a flag of this reader: `HasKey` says no and a read finds nothing, so a field of the struct
         *             that a game uses another way (`type`, `path`) never becomes a flag by accident
         */
        explicit CommandLineDeserializer(const CommandLine& line, Span<const StringView> keys = {});
        ~CommandLineDeserializer() override = default;

        CommandLineDeserializer(const CommandLineDeserializer&) = delete;
        CommandLineDeserializer& operator=(const CommandLineDeserializer&) = delete;
        CommandLineDeserializer(CommandLineDeserializer&&) = delete;
        CommandLineDeserializer& operator=(CommandLineDeserializer&&) = delete;

        bool Read(StringView name, bool& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool Read(StringView name, int8_t& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool Read(StringView name, uint8_t& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool Read(StringView name, int16_t& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool Read(StringView name, uint16_t& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool Read(StringView name, int32_t& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool Read(StringView name, uint32_t& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool Read(StringView name, int64_t& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool Read(StringView name, uint64_t& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool Read(StringView name, float& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool Read(StringView name, double& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool Read(StringView name, String& outValue) override {
            return ReadText(name, outValue);
        }
        bool Read(StringView name, PoolString& outValue) override {
            return ReadPoolText(name, outValue);
        }

        bool ReadAttribute(StringView name, bool& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool ReadAttribute(StringView name, int8_t& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool ReadAttribute(StringView name, uint8_t& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool ReadAttribute(StringView name, int16_t& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool ReadAttribute(StringView name, uint16_t& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool ReadAttribute(StringView name, int32_t& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool ReadAttribute(StringView name, uint32_t& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool ReadAttribute(StringView name, int64_t& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool ReadAttribute(StringView name, uint64_t& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool ReadAttribute(StringView name, float& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool ReadAttribute(StringView name, double& outValue) override {
            return ReadFlag(name, outValue);
        }
        bool ReadAttribute(StringView name, String& outValue) override {
            return ReadText(name, outValue);
        }
        bool ReadAttribute(StringView name, PoolString& outValue) override {
            return ReadPoolText(name, outValue);
        }

        /// A flag holds one value: both fail with an error (a key that is not a flag of this reader fails without one)
        bool BeginObject(StringView name) override;
        void EndObject() override {}
        bool BeginArray(StringView name, StringView elementName, size_t& count) override;
        void EndArray() override {}
        bool BeginArrayElement() override {
            return false;
        }
        void EndArrayElement() override {}

        /// Always on: a flag is typed by the field that reads it, there is no lenient reading to keep
        void SetStrictValues(bool /*strict*/) override {}
        bool IsStrictValues() const override {
            return true;
        }

        /// Whether the flag of @p name is on the command line (in either form)
        bool HasKey(StringView name) const override;
        void NoteKnownKey(StringView name) override;
        /// Reports the typos of flags under the diagnostics mode; call it once, after the load
        void FinishLoad() override;

        bool HasErrors() const override;
        const Vector<DeserializeError>& GetErrors() const override;
        void ClearErrors() override;

    protected:
        /// A root-level path is a key and is stored as its flag; a path with `.` or `[`, or one that is already a flag, stays as it is
        void AddError(StringView path, StringView message, int32_t line) override;

    private:
        bool IsFlagKey(StringView key) const;

        /// The argument of the flag of @p key; empty for a key that is no flag of this reader or has no argument. Notes the flag as asked
        /// when the diagnostics are on
        Optional<CommandLineArgument> Lookup(StringView key);

        void NoteAsked(StringView flag);
        bool IsAsked(StringView flag) const;
        bool FindingsFull() const;

        template <typename T>
        bool ReadFlag(StringView key, T& outValue);

        bool ReadText(StringView key, String& outValue);
        bool ReadPoolText(StringView key, PoolString& outValue);

        /// The value text of a flag that needs one, or empty with the error `needs a value` for a bare flag
        Optional<StringView> RequireValue(StringView key, const CommandLineArgument& argument);

        const CommandLine& _line;
        Span<const StringView> _keys;
        /// The flags asked, only while the diagnostics are on (FinishLoad compares the unknown arguments with them)
        Vector<String> _askedFlags;
        Vector<DeserializeError> _errors;
    };

}  // namespace BECore
