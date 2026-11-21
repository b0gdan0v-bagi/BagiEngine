#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Value.h>
#include <cstdint>

namespace BECore::ValueListMahjongData {

    // A hand of a tile game in the shape the scoring of Mahjong reads it (lists of objects, lists inside them, enums and flags), as context objects and as a document of values that read
    // them: ValueListLoadTest, the Mahjong proof and the native set. The .gen.hpp is included once per executable. A type is found by its simple name, so the names are unique among all
    // test headers.

    CORE_ENUM(ValueListSuit, uint8_t, Man, Pin, Sou, Wind, Dragon)
    CORE_ENUM(ValueListRank, uint8_t, One, Two, Three, Four, Five, Six, Seven, Eight, Nine)

    struct ValueListTile {
        BE_CLASS(ValueListTile)

        BE_REFLECT_FIELD ValueListSuit suit = ValueListSuit::Man;
        BE_REFLECT_FIELD ValueListRank rank = ValueListRank::One;
        BE_REFLECT_FIELD bool honor = false;
        BE_REFLECT_FIELD bool terminal = false;
        BE_REFLECT_FIELD bool dragon = false;
    };

    struct ValueListSet {
        BE_CLASS(ValueListSet)

        BE_REFLECT_FIELD Array<ValueListTile, 4> tiles;
        BE_REFLECT_FIELD bool pung = false;
        BE_REFLECT_FIELD bool dragon = false;
        BE_REFLECT_FIELD ValueListSuit suit = ValueListSuit::Man;
        BE_REFLECT_FIELD int32_t fu = 0;
    };

    struct ValueListYaku {
        BE_CLASS(ValueListYaku)

        BE_REFLECT_FIELD bool on = false;
        BE_REFLECT_FIELD int32_t han = 0;
    };

    struct ValueListHand {
        BE_CLASS(ValueListHand)

        BE_REFLECT_FIELD Array<ValueListTile, 14> tiles;
        BE_REFLECT_FIELD Vector<ValueListSet> sets;
        BE_REFLECT_FIELD Vector<ValueListYaku> yaku;
    };

    /// What a content file of the game would hold: the `<context>` first, then values that read the hand
    struct ValueListMahjongDocument {
        BE_CLASS(ValueListMahjongDocument)

        BE_REFLECT_FIELD ContextDecl context;
        BE_REFLECT_FIELD Value<bool> win = false;
        BE_REFLECT_FIELD Value<int32_t> han = 7;
        BE_REFLECT_FIELD Value<int32_t> fu = 7;
        BE_REFLECT_FIELD Value<int32_t> tiles = 7;
        BE_REFLECT_FIELD Value<bool> pure = false;
    };

}  // namespace BECore::ValueListMahjongData
