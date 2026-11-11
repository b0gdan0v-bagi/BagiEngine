#include "FakeSeatedGame.h"

#include <gtest/gtest.h>

namespace BECore::Tests {

    void ExpectNoLeak(const eastl::string& text, int seat, const FakeSeatedGame& game) {
        const eastl::vector<eastl::string> leaked = FindLeakedCards(text, seat, game);
        if (leaked.empty()) {
            return;
        }
        eastl::string names;
        for (const eastl::string& card : leaked) {
            names += card;
            names += ' ';
        }
        ADD_FAILURE() << "a message for seat " << seat << " names the hidden cards " << names.c_str() << ": " << text.c_str();
    }

}  // namespace BECore::Tests
