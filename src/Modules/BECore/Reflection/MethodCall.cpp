#include "MethodCall.h"

namespace BECore {

    namespace {

        constexpr unsigned char kFirstPrintable = 0x20;
        constexpr unsigned char kDelete = 0x7F;
        constexpr char kReplacementChar = '?';
        constexpr StringView kUnknownKeyPrefix = "Unknown key ";
        constexpr size_t kMaxQuotedErrors = 3;

        /// Client text in a message: cut, and free of control characters that would break a line of the log
        String Shown(StringView text) {
            const bool cut = text.size() > MethodCallLimits::kMaxShownNameBytes;
            String shown;
            for (const char c : text.substr(0, MethodCallLimits::kMaxShownNameBytes)) {
                const auto byte = static_cast<unsigned char>(c);
                shown += (byte < kFirstPrintable || byte == kDelete) ? kReplacementChar : c;
            }
            if (cut) {
                shown += "...";
            }
            return shown;
        }

        std::unexpected<CallError> Fail(CallStatus status, String message) {
            return std::unexpected(CallError{status, std::move(message)});
        }

        /// Puts the reader's settings back whatever way the call ends
        class StrictArguments {
        public:
            explicit StrictArguments(IDeserializer& args) : _args(args), _strict(args.IsStrictValues()), _diagnostics(args.GetDiagnostics()) {
                _args.SetStrictValues(true);
                _args.SetDiagnostics(DiagnosticsMode::Error);
            }

            ~StrictArguments() {
                _args.SetStrictValues(_strict);
                _args.SetDiagnostics(_diagnostics);
            }

            StrictArguments(const StrictArguments&) = delete;
            StrictArguments& operator=(const StrictArguments&) = delete;

        private:
            IDeserializer& _args;
            bool _strict;
            DiagnosticsMode _diagnostics;
        };

        /// `Add: argument 'amount': Invalid integer value '3.5'`, the first errors one after the other; an unknown key reads
        /// `Unknown argument 'x', did you mean 'amount'?`
        String ArgumentMessage(StringView method, const IDeserializer& args) {
            const Vector<DeserializeError>& errors = args.GetErrors();
            String text = String(method.data(), method.size());
            text += ':';
            for (size_t i = 0; i < errors.size() && i < kMaxQuotedErrors; ++i) {
                const StringView path = errors[i].path.ToStringView();
                const StringView message = errors[i].errorMessage.ToStringView();
                text += i == 0 ? " " : "; ";
                if (message.starts_with(kUnknownKeyPrefix)) {
                    text += Format("Unknown argument {}", message.substr(kUnknownKeyPrefix.size()));
                } else if (path.empty()) {
                    text.append(message.data(), message.size());
                } else {
                    text += Format("argument '{}': {}", path, message);
                }
            }
            if (errors.size() > kMaxQuotedErrors) {
                text += Format(" (and {} more)", errors.size() - kMaxQuotedErrors);
            }
            return text;
        }

    }  // namespace

    std::expected<void, CallError> CallMethod(TypedRef object, StringView name, IDeserializer& args, ISerializer& result, const TypeRegistry& registry) {
        if (!object) {
            return Fail(CallStatus::unknown_method, Format("Cannot call '{}': there is no object", Shown(name)));
        }
        const MethodRef found = registry.FindMethod(*object.type, name);
        if (!found) {
            return Fail(CallStatus::unknown_method, Format("Type '{}' has no method '{}'", object.type->Name(), Shown(name)));
        }
        const MethodDesc& method = *found.method;
        if (!method.callable || method.invoke == nullptr) {
            return Fail(CallStatus::not_callable, Format("Method '{}' of '{}' cannot be called by name", method.name, found.owner->Name()));
        }
        if (!method.isConst && object.isConst) {
            return Fail(CallStatus::const_object, Format("Method '{}' of '{}' changes the object, and the object is const", method.name, found.owner->Name()));
        }

        // The owner's subobject: the offsets of the chain are the registry's rule
        void* const owner = static_cast<char*>(object.Address()) + found.offset;
        bool ran = false;
        {
            const StrictArguments strict(args);
            ran = method.invoke(owner, args, result);
        }
        if (!ran) {
            return Fail(CallStatus::bad_arguments, ArgumentMessage(method.name, args));
        }
        return {};
    }

}  // namespace BECore
