#include "TlsStream.h"

namespace BECore {

    namespace {

        /// The backend of a platform that has none: it never connects
        class UnsupportedTlsStream final : public ITlsStream {
        public:
            std::expected<void, eastl::string> Connect(const TlsConnectParams& params) override {
                _lastError = Format("{}: this platform has no TLS backend (the browser build has its own wss://), cannot reach {}:{}", kTlsUnsupported, params.host, params.port);
                return std::unexpected(_lastError);
            }

            bool SendAll(eastl::span<const uint8_t>) override {
                return false;
            }

            std::expected<size_t, SocketError> Receive(eastl::span<uint8_t>) override {
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

            eastl::string GetLastError() const override {
                return _lastError;
            }

        private:
            eastl::string _lastError;  // written by Connect() alone, before any other thread has the stream
        };

    }  // namespace

    bool IsTlsSupported() {
        return false;
    }

    eastl::unique_ptr<ITlsStream> MakeTlsStream() {
        return eastl::make_unique<UnsupportedTlsStream>();
    }

}  // namespace BECore
