// pch.h
#pragma once

// PugiXML
#include <pugixml.hpp>

// EASTL
#include <EABase/eabase.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/functional.h>
#include <EASTL/hash_map.h>
#include <EASTL/internal/config.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>
#include <EASTL/fixed_string.h>
#include <EASTL/string_view.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>
#include <EASTL/variant.h>

// Стандартные библиотеки C++
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <ranges>
#include <array>
#include <limits>

// Math
#include <Math/Color.h>

// Core
#include <BECore/Utils/String.h>
#include <BECore/Utils/Singleton.h>
#include <BECore/Utils/PassKey.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/StaticPoolString.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Utils/Overload.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/New.h>

#include <Events/SubscriptionHolder.h>

#include <BECore/Reflection/ReflectionMarkers.h>

#include <BECore/Format/Format.h>

#include <BECore/Config/XmlNode.h>
#include <BECore/Config/XmlConfig.h>

// Logger system
#include <BECore/Logger/LogLevel.h>
#include <BECore/Logger/Logger.h>

// Assert system (macros: ASSERT, EXPECT, FATALERROR)
#include <BECore/Assert/AssertMacros.h>
