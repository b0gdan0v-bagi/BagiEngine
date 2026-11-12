#pragma once

#include <RemoteControl/Tls/TlsStream.h>
#include <gtest/gtest.h>
#include <string>

namespace BECore::Tests {

    /// A scripted TLS backend for tests, over a real loopback Socket, so the code that uses an ITlsStream runs on every platform.
    ///
    /// The fake handshake: the client sends `FAKE-HELLO <serverName> <anchor count>\n`; the server answers `FAKE-OK\n` or
    /// `FAKE-REJECT <reason>\n`. After that every byte is XOR-ed with kFakeTlsMask both ways, so a server that forgets the "encryption"
    /// (or a client that sends plain text) is noticed. FakeTlsServerSide is the other end, for hand-written servers.

    inline constexpr uint8_t kFakeTlsMask = 0x5A;
    inline constexpr std::chrono::milliseconds kFakeTlsServerWait{20000};

    inline void FakeTlsMask(uint8_t* data, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            data[i] ^= kFakeTlsMask;
        }
    }

    namespace FakeTlsDetail {

        /// One line of the handshake, a byte at a time so nothing after it is taken. The socket's receive timeout is set here
        /// in slices; @p cancel and @p deadline end the wait. Empty text and false on a timeout, a cancel or the end of the stream
        inline bool ReadLine(Socket& socket, std::string& line, std::chrono::steady_clock::time_point deadline, const std::atomic<bool>* cancel, std::string& whyNot) {
            for (;;) {
                if (cancel != nullptr && cancel->load()) {
                    whyNot = "cancelled";
                    return false;
                }
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
                if (left <= std::chrono::milliseconds::zero()) {
                    whyNot = "timed out";
                    return false;
                }
                socket.SetReceiveTimeout(std::min(left, Socket::kWakeInterval));
                uint8_t byte = 0;
                const auto received = socket.Receive(eastl::span<uint8_t>(&byte, 1));
                if (!received) {
                    if (received.error() == SocketError::TimedOut) {
                        continue;
                    }
                    whyNot = "the connection ended";
                    return false;
                }
                if (byte == '\n') {
                    return true;
                }
                line.push_back(static_cast<char>(byte));
            }
        }

    }  // namespace FakeTlsDetail

    class FakeTlsStream final : public ITlsStream {
    public:
        ~FakeTlsStream() override {
            Close();
        }

        std::expected<void, eastl::string> Connect(const TlsConnectParams& params) override {
            const std::string target = std::string(params.host.data(), params.host.size()) + ":" + std::to_string(params.port);
            auto connected = Socket::Connect(params.host, params.port, params.connectTimeout, params.cancel);
            if (!connected) {
                return std::unexpected(SetError(connected.error()));
            }
            Socket socket = std::move(*connected);
            const std::string hello = "FAKE-HELLO " + std::string(params.host.data(), params.host.size()) + " " + std::to_string(params.additionalTrustAnchorsDer.size()) + "\n";
            if (!socket.SendAll(BECore::AsBytes(eastl::string_view(hello.data(), hello.size())))) {
                return std::unexpected(SetError(eastl::string(("TLS: sending the hello to " + target + " failed").c_str())));
            }
            std::string answer;
            std::string whyNot;
            if (!FakeTlsDetail::ReadLine(socket, answer, params.handshakeDeadline, params.cancel, whyNot)) {
                return std::unexpected(SetError(eastl::string(("TLS: no handshake answer from " + target + ": " + whyNot).c_str())));
            }
            if (answer != "FAKE-OK") {
                constexpr std::string_view kReject = "FAKE-REJECT ";
                const std::string reason = answer.rfind(kReject, 0) == 0 ? answer.substr(kReject.size()) : "bad handshake answer: " + answer;
                return std::unexpected(SetError(eastl::string(("TLS: " + reason).c_str())));
            }
            socket.SetReceiveTimeout(std::chrono::milliseconds::zero());
            std::lock_guard<std::mutex> lock(_mutex);
            _socket = std::move(socket);
            _valid = true;
            return {};
        }

        bool SendAll(eastl::span<const uint8_t> data) override {
            if (!_valid) {
                SetError("TLS: not connected");
                return false;
            }
            eastl::vector<uint8_t> masked(data.begin(), data.end());
            FakeTlsMask(masked.data(), masked.size());
            const bool sent = _socket.SendAll(eastl::span<const uint8_t>(masked.data(), masked.size()));
            if (!sent) {
                SetError("TLS: sending failed");
            }
            return sent;
        }

        std::expected<size_t, SocketError> Receive(eastl::span<uint8_t> buffer) override {
            if (!_valid) {
                SetError("TLS: not connected");
                return std::unexpected(SocketError::Failed);
            }
            auto received = _socket.Receive(buffer);
            if (received) {
                FakeTlsMask(buffer.data(), *received);
            } else if (received.error() == SocketError::Failed) {
                SetError("TLS: receiving failed");
            }
            return received;
        }

        bool SetReceiveTimeout(std::chrono::milliseconds timeout) override {
            return _valid && _socket.SetReceiveTimeout(timeout);
        }

        void ShutdownBoth() override {
            if (_valid) {
                _socket.ShutdownBoth();
            }
        }

        void Close() override {
            _valid = false;
            _socket.Close();
        }

        bool IsValid() const override {
            return _valid;
        }

        eastl::string GetLastError() const override {
            std::lock_guard<std::mutex> lock(_mutex);
            return _lastError;
        }

    private:
        /// Remembers @p text for GetLastError() and gives it back
        eastl::string SetError(eastl::string text) {
            std::lock_guard<std::mutex> lock(_mutex);
            _lastError = text;
            return text;
        }

        Socket _socket;  // published by Connect() before any other thread has the stream
        std::atomic<bool> _valid{false};
        mutable std::mutex _mutex;  // guards _lastError (Receive and SendAll fail on different threads)
        eastl::string _lastError;
    };

    /// A factory that gives a FakeTlsStream
    [[nodiscard]] inline TlsStreamFactory FakeTlsFactory() {
        return [] { return eastl::unique_ptr<ITlsStream>(eastl::make_unique<FakeTlsStream>()); };
    }

    /// The server's end of the fake handshake and of the "encryption", for a hand-written server on an accepted Socket
    struct FakeTlsServerSide {
        std::string serverName;  // what the client's hello said
        size_t anchorCount = 0;
        bool hello = false;  // a hello line was read

        /// Reads the client's hello and answers it: FAKE-OK, or FAKE-REJECT with @p rejectReason when that is not empty.
        /// True when the stream is encrypted from here on (the client was accepted)
        bool Handshake(Socket& peer, const std::string& rejectReason = {}, std::chrono::milliseconds limit = kFakeTlsServerWait) {
            std::string line;
            std::string whyNot;
            if (!FakeTlsDetail::ReadLine(peer, line, std::chrono::steady_clock::now() + limit, nullptr, whyNot)) {
                ADD_FAILURE() << "no fake TLS hello: " << whyNot;
                return false;
            }
            constexpr std::string_view kHello = "FAKE-HELLO ";
            if (line.rfind(kHello, 0) != 0) {
                ADD_FAILURE() << "not a fake TLS hello: " << line;
                return false;
            }
            const std::string rest = line.substr(kHello.size());
            const size_t space = rest.rfind(' ');
            serverName = rest.substr(0, space);
            anchorCount = space == std::string::npos ? 0 : static_cast<size_t>(std::stoul(rest.substr(space + 1)));
            hello = true;

            const std::string answer = rejectReason.empty() ? "FAKE-OK\n" : "FAKE-REJECT " + rejectReason + "\n";
            EXPECT_TRUE(peer.SendAll(BECore::AsBytes(eastl::string_view(answer.data(), answer.size()))));
            return rejectReason.empty();
        }
    };

}  // namespace BECore::Tests
