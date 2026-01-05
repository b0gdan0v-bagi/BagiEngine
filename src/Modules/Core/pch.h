// pch.h
#pragma once

// Чтобы Asio не тянул лишнего из Windows.h
#ifdef _WIN32
#define _WIN32_WINNT 0x0A00  // Windows 10+
#endif

// Boost компоненты
#include <boost/algorithm/string.hpp>  // строковые алгоритмы
#include <boost/any.hpp>               // any
#include <boost/bind.hpp>              // bind
#include <boost/function.hpp>          // function
#include <boost/optional.hpp>          // optional
#include <boost/smart_ptr.hpp>         // smart_ptr, shared_ptr, weak_ptr, intrusive_ptr
#include <boost/type_traits.hpp>       // type_traits
#include <boost/utility.hpp>           // utility функции
#include <boost/variant.hpp>           // variant

// PugiXML
#include <pugixml.hpp>

// Magic Enum
#include <magic_enum/magic_enum.hpp>

// EASTL
#include <EABase/eabase.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/functional.h>
#include <EASTL/internal/config.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

// Стандартные библиотеки C++
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
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
#include <Core/Utils/String.h>
#include <Core/Utils/Singleton.h>
#include <Core/Utils/PassKey.h>
#include <Core/Utils/EnumUtils.h>
#include <Core/Utils/Overload.h>
#include <Core/RefCounted/RefCounted.h>
#include <Core/RefCounted/IntrusivePtr.h>
#include <Core/RefCounted/New.h>

#include <Core/Config/XmlNode.h>
#include <Core/Config/XmlConfig.h>
