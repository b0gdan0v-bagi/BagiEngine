#include "WebSocketHttpHead.h"

#include <RemoteControl/WebSocketHandshake.h>

namespace BECore {

    namespace {

        constexpr StringView kHeadEnd = "\r\n\r\n";
        constexpr size_t kChunkSize = 1024;

    }  // namespace

    HttpHeadResult ReadHttpHead(std::chrono::steady_clock::time_point deadline, FunctionRef<void(std::chrono::milliseconds)> setReceiveTimeout,
                                FunctionRef<std::expected<size_t, SocketError>(Span<uint8_t>)> receive, Vector<uint8_t>& received) {
        Array<uint8_t, kChunkSize> chunk;
        for (;;) {
            // Zero would mean "wait for ever" to the socket, so a deadline that is (nearly) reached ends the read
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (left <= std::chrono::milliseconds::zero()) {
                return {HttpHeadOutcome::DeadlinePassed};
            }
            setReceiveTimeout(left);
            const auto count = receive(Span<uint8_t>(chunk.data(), chunk.size()));
            if (!count) {
                return {HttpHeadOutcome::StreamEnded, count.error()};
            }
            // Plain pointers: an Array iterator is a pointer in EASTL, and its insert does not take MSVC's class iterator of a std:: container
            received.insert(received.end(), chunk.data(), chunk.data() + *count);

            const StringView text(reinterpret_cast<const char*>(received.data()), received.size());
            const size_t end = text.find(kHeadEnd);
            if (end != StringView::npos) {
                return {HttpHeadOutcome::Complete, SocketError::Failed, end + kHeadEnd.size()};
            }
            if (received.size() > kMaxHandshakeBytes) {
                return {HttpHeadOutcome::TooLong};
            }
        }
    }

}  // namespace BECore
