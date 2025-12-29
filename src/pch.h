// pch.h
#pragma once

// Чтобы Asio не тянул лишнего из Windows.h
#ifdef _WIN32
#define _WIN32_WINNT 0x0A00  // Windows 10+
#endif

#include <boost/asio.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <filesystem>

// Другие полезные header-only компоненты Boost:
#include <boost/algorithm/string.hpp>  // строковые алгоритмы
#include <boost/any.hpp>               // any
#include <boost/bind.hpp>              // bind
#include <boost/function.hpp>          // function
#include <boost/optional.hpp>          // optional
#include <boost/smart_ptr.hpp>         // smart_ptr, shared_ptr, weak_ptr, intrusive_ptr
#include <boost/type_traits.hpp>       // type_traits
#include <boost/utility.hpp>           // utility функции
#include <boost/variant.hpp>           // variant

// Стандартные библиотеки C++
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// pugi
#include <pugixml.hpp>

// SDL
#include <SDL3/SDL.h>

// entt
#include <entt/entt.hpp>

// magic enum
#include <magic_enum/magic_enum.hpp>

// C стандартные библиотеки
#include <cctype>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

// Core
#include <Core/Math/Color.h>
#include <Core/Utils/New.h>
#include <Core/Utils/PassKey.h>
#include <Core/Utils/RefCounted.h>
#include <Core/Utils/String.h>
