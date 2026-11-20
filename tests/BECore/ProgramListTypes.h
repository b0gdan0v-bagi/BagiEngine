#pragma once

#include <BECore/Math/Vec3.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore::ProgramListData {

    // The reflected context objects of the list and loop programs (ProgramListTest, the native set of tests/NativePrograms): the .gen.hpp is included once per executable, by ProgramListTest.cpp
    // and by the tool. A type is found by its simple name, so the names are unique among all test headers.

    CORE_ENUM(ProgramListSuit, uint8_t, Dots, Bamboo, Characters, Winds, Dragons)

    /// An element with a field of every kind a loop reads at an offset; its size is no power of two, so a stride that is wrong shows
    struct ProgramListTile {
        BE_CLASS(ProgramListTile)

        BE_REFLECT_FIELD ProgramListSuit suit = ProgramListSuit::Dots;
        BE_REFLECT_FIELD bool terminal = false;
        BE_REFLECT_FIELD int32_t rank = 0;
        BE_REFLECT_FIELD float weight = 0.0f;
        BE_REFLECT_FIELD Vec3 position;
        BE_REFLECT_FIELD PoolString name;
    };

    /// A list inside an element of a list: the nested loop of `sum` over the tiles of each set
    struct ProgramListSet {
        BE_CLASS(ProgramListSet)

        BE_REFLECT_FIELD Array<ProgramListTile, 4> tiles;
        BE_REFLECT_FIELD bool pung = false;
    };

    /// One field of every container a program reads, of an object and of a scalar element
    struct ProgramListHand {
        BE_CLASS(ProgramListHand)

        BE_REFLECT_FIELD int32_t before = 0;
        BE_REFLECT_FIELD Array<ProgramListTile, 14> tiles;
        BE_REFLECT_FIELD Vector<ProgramListSet> sets;
        BE_REFLECT_FIELD SmallVector<int32_t, 4> counts;
        BE_REFLECT_FIELD FixedVector<float, 8> weights;
        BE_REFLECT_FIELD Array<bool, 3> flags;
        BE_REFLECT_FIELD Vector<ProgramListTile> none;
        BE_REFLECT_FIELD int32_t after = 0;
    };

    /// A list a formula may not read (of strings that own their text), two it may, and a set held by value (the path of a list that crosses an embedded object). A list of lists, optionals,
    /// maps or enums is no reflected field (the serializers have no element form), so those are tested as types
    struct ProgramListRefused {
        BE_CLASS(ProgramListRefused)

        BE_REFLECT_FIELD Vector<String> texts;
        BE_REFLECT_FIELD Vector<PoolString> names;
        BE_REFLECT_FIELD Vector<Vec3> points;
        BE_REFLECT_FIELD ProgramListSet set;
    };

    /// An element with a field a formula cannot read (`uint8_t`) and one it reaches through an object it holds by value
    struct ProgramListCrate {
        BE_CLASS(ProgramListCrate)

        BE_REFLECT_FIELD ProgramListTile tile;
        BE_REFLECT_FIELD uint8_t small = 0;
    };

    /// Lists whose elements a formula reads only in part: of an object with an embedded object, and of a scalar it has no type for
    struct ProgramListStock {
        BE_CLASS(ProgramListStock)

        BE_REFLECT_FIELD Vector<ProgramListCrate> crates;
        BE_REFLECT_FIELD Vector<uint8_t> bytes;
    };

    /// Sixteen bytes that a test fills with the bytes of a value of any program type and reads back through `LoadElem`: a one-element list of this object, so an element of every type is one
    /// reflected type
    struct ProgramListScratch {
        BE_CLASS(ProgramListScratch)

        BE_REFLECT_FIELD Array<uint64_t, 2> bytes;
    };

}  // namespace BECore::ProgramListData
