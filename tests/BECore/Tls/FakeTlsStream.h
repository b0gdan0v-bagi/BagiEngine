#pragma once

#include <BECore/Format/Format.h>
#include <BECore/Threading/LockGuards.h>
#include <BECore/Threading/Mutex.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/StringUtils.h>
#include <Common/Printers.h>
#include <RemoteControl/Tls/TlsStream.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <gtest/gtest.h>
#include <utility>

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
        inline bool ReadLine(Socket& socket, String& line, std::chrono::steady_clock::time_point deadline, const std::atomic<bool>* cancel, String& whyNot) {
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
                const auto received = socket.Receive(Span<uint8_t>(&byte, 1));
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

        std::expected<void, String> Connect(const TlsConnectParams& params) override {
            const String target = Format("{}:{}", params.host, params.port);
            auto connected = Socket::Connect(params.host, params.port, params.connectTimeout, params.cancel);
            if (!connected) {
                return std::unexpected(SetError(connected.error()));
            }
            Socket socket = std::move(*connected);
            const String hello = Format("FAKE-HELLO {} {}\n", params.host, params.additionalTrustAnchorsDer.size());
            if (!socket.SendAll(BECore::AsBytes(hello))) {
                return std::unexpected(SetError(Format("TLS: sending the hello to {} failed", target)));
            }
            String answer;
            String whyNot;
            if (!FakeTlsDetail::ReadLine(socket, answer, params.handshakeDeadline, params.cancel, whyNot)) {
                return std::unexpected(SetError(Format("TLS: no handshake answer from {}: {}", target, whyNot)));
            }
            if (answer != "FAKE-OK") {
                constexpr StringView kReject = "FAKE-REJECT ";
                const String reason = StringView(answer).starts_with(kReject) ? answer.substr(kReject.size()) : Format("bad handshake answer: {}", answer);
                return std::unexpected(SetError(Format("TLS: {}", reason)));
            }
            socket.SetReceiveTimeout(std::chrono::milliseconds::zero());
            const LockGuard lock(_mutex);
            _socket = std::move(socket);
            _valid = true;
            return {};
        }

        bool SendAll(Span<const uint8_t> data) override {
            if (!_valid) {
                SetError("TLS: not connected");
                return false;
            }
            Vector<uint8_t> masked(data.begin(), data.end());
            FakeTlsMask(masked.data(), masked.size());
            const bool sent = _socket.SendAll(Span<const uint8_t>(masked.data(), masked.size()));
            if (!sent) {
                SetError("TLS: sending failed");
            }
            return sent;
        }

        std::expected<size_t, SocketError> Receive(Span<uint8_t> buffer) override {
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

        String GetLastError() const override {
            const LockGuard lock(_mutex);
            return _lastError;
        }

    private:
        /// Remembers @p text for GetLastError() and gives it back
        String SetError(String text) {
            const LockGuard lock(_mutex);
            _lastError = text;
            return text;
        }

        Socket _socket;  // published by Connect() before any other thread has the stream
        std::atomic<bool> _valid{false};
        mutable Mutex _mutex{BE_LOCK_SITE("Fake TLS stream error")};  // guards _lastError (Receive and SendAll fail on different threads)
        String _lastError;
    };

    /// A factory that gives a FakeTlsStream
    [[nodiscard]] inline TlsStreamFactory FakeTlsFactory() {
        return [] { return UniquePtr<ITlsStream>(MakeUnique<FakeTlsStream>()); };
    }

    /// The server's end of the fake handshake and of the "encryption", for a hand-written server on an accepted Socket
    struct FakeTlsServerSide {
        String serverName;  // what the client's hello said
        size_t anchorCount = 0;
        bool hello = false;  // a hello line was read

        /// Reads the client's hello and answers it: FAKE-OK, or FAKE-REJECT with @p rejectReason when that is not empty.
        /// True when the stream is encrypted from here on (the client was accepted)
        bool Handshake(Socket& peer, const String& rejectReason = {}, std::chrono::milliseconds limit = kFakeTlsServerWait) {
            String line;
            String whyNot;
            if (!FakeTlsDetail::ReadLine(peer, line, std::chrono::steady_clock::now() + limit, nullptr, whyNot)) {
                ADD_FAILURE() << "no fake TLS hello: " << whyNot;
                return false;
            }
            constexpr StringView kHello = "FAKE-HELLO ";
            if (!StringView(line).starts_with(kHello)) {
                ADD_FAILURE() << "not a fake TLS hello: " << line;
                return false;
            }
            const String rest = line.substr(kHello.size());
            const size_t space = rest.rfind(' ');
            serverName = rest.substr(0, space);
            anchorCount = space == String::npos ? 0 : StringUtils::ParseNumber<size_t>(StringView(rest).substr(space + 1)).value_or(0);
            hello = true;

            const String answer = rejectReason.empty() ? String("FAKE-OK\n") : Format("FAKE-REJECT {}\n", rejectReason);
            EXPECT_TRUE(peer.SendAll(BECore::AsBytes(answer)));
            return rejectReason.empty();
        }
    };

}  // namespace BECore::Tests
