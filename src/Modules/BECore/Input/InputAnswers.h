#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>

namespace BECore {

    /// `click_at`: the point of the request and the output pixel it hit, as doubles so the text of a widened float stays
    struct InputClickAnswer {
        BE_CLASS(InputClickAnswer)

        BE_REFLECT_FIELD bool consumed = false;
        BE_REFLECT_FIELD double outputX = 0.0;
        BE_REFLECT_FIELD double outputY = 0.0;
        BE_REFLECT_FIELD double x = 0.0;
        BE_REFLECT_FIELD double y = 0.0;
    };

}  // namespace BECore
