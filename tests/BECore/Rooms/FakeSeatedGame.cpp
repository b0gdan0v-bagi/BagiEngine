#include "FakeSeatedGame.h"

#include <gtest/gtest.h>

namespace BECore::Tests {

    void ExpectNoLeak(const String& text, int seat, const FakeSeatedGame& game) {
        const eastl::vector<String> leaked = FindLeakedCards(text, seat, game);
        if (leaked.empty()) {
            return;
        }
        String names;
        for (const String& card : leaked) {
            names += card;
            names += ' ';
        }
        ADD_FAILURE() << "a message for seat " << seat << " names the hidden cards " << names.c_str() << ": " << text.c_str();
    }

}  // namespace BECore::Tests
