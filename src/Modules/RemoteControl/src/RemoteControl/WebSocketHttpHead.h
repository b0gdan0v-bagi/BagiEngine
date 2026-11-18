#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Utils/FunctionRef.h>
#include <RemoteControl/Socket.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace BECore {

    CORE_ENUM(HttpHeadOutcome, uint8_t, Complete, DeadlinePassed, TooLong, StreamEnded)

    struct HttpHeadResult {
        HttpHeadOutcome outcome = HttpHeadOutcome::StreamEnded;
        /// StreamEnded: what the stream reported
        SocketError error = SocketError::Failed;
        /// Complete: the bytes of the head, its blank line included; what follows them was received with it
        size_t headSize = 0;
    };

    /**
     * @brief Reads an HTTP head of at most kMaxHandshakeBytes into @p received, for both sides of the opening handshake.
     *
     * One deadline for the whole head: a timeout per read would let a peer that sends a byte every few seconds hold the connection for ever.
     */
    [[nodiscard]] HttpHeadResult ReadHttpHead(std::chrono::steady_clock::time_point deadline, FunctionRef<void(std::chrono::milliseconds)> setReceiveTimeout,
                                              FunctionRef<std::expected<size_t, SocketError>(Span<uint8_t>)> receive, Vector<uint8_t>& received);

}  // namespace BECore
