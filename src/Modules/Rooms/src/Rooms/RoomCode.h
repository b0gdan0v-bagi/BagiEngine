#pragma once

#include <Rooms/RoomTypes.h>

namespace BECore {

    /// A new room code: kRoomCodeLength characters of kRoomCodeAlphabet, from one word of @p random (5 bits per character, so every
    /// character is equally likely). The same word gives the same code. It does not check that no room has the code: the hub does.
    [[nodiscard]] eastl::string GenerateRoomCode(const RandomFn& random);

    /**
     * @brief The room code a client typed, in the form the server keeps: upper case.
     *
     * @return nullopt when the text is not kRoomCodeLength characters of the alphabet once upper-cased (so `abcde` is `ABCDE`,
     *         but a code with `I`, `O`, `0` or `1`, spaces, or the wrong length is nothing). Never throws or asserts on any bytes.
     */
    [[nodiscard]] Optional<eastl::string> NormalizeRoomCode(StringView text);

    /// A new reconnect token: 128 bits from two words of @p random as kTokenLength lower-case hexadecimal characters. The token is
    /// a secret: it goes to its owner only.
    [[nodiscard]] eastl::string GenerateToken(const RandomFn& random);

    /// True when @p text has the form of a token (kTokenLength lower-case hexadecimal characters), whether or not any member has it
    [[nodiscard]] bool IsWellFormedToken(StringView text);

}  // namespace BECore
