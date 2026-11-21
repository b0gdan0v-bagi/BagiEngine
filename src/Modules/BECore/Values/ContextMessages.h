#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Values/Context.h>

namespace BECore {

    /// `unknown level 'x', did you mean 'y'?; the declared levels are: a, b`: the words every refusal of an undeclared level shares, so `LinkSlot` and `DerivedReads` say it alike
    [[nodiscard]] String DescribeUnknownLevel(const ContextDecl& declaration, StringView levelName);

}  // namespace BECore
