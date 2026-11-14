#include "TlsStream.h"

// The stub of a platform without a backend; macOS and Windows have their own file that defines the two functions at the end
#if !defined(PLATFORM_MACOS) && !defined(PLATFORM_WINDOWS)

namespace BECore {

    namespace {

        /// The backend of a platform that has none: it never connects
        class UnsupportedTlsStream final : public ITlsStream {
        public:
            std::expected<void, String> Connect(const TlsConnectParams& params) override {
                _lastError = Format("{}: this platform has no TLS backend (the browser build has its own wss://), cannot reach {}:{}", kTlsUnsupported, params.host, params.port);
                return std::unexpected(_lastError);
            }

            bool SendAll(Span<const uint8_t>) override {
                return false;
            }

            std::expected<size_t, SocketError> Receive(Span<uint8_t>) override {
                return std::unexpected(SocketError::Failed);
            }

            bool SetReceiveTimeout(std::chrono::milliseconds) override {
                return false;
            }

            void ShutdownBoth() override {}

            void Close() override {}

            bool IsValid() const override {
                return false;
            }

            String GetLastError() const override {
                return _lastError;
            }

        private:
            String _lastError;  // written by Connect() alone, before any other thread has the stream
        };

    }  // namespace

    bool IsTlsSupported() {
        return false;
    }

    UniquePtr<ITlsStream> MakeTlsStream() {
        return MakeUnique<UnsupportedTlsStream>();
    }

}  // namespace BECore

#endif  // no TLS backend on this platform
