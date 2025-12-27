// pch.h
#pragma once

// Чтобы Asio не тянул лишнего из Windows.h
#ifdef _WIN32
#define _WIN32_WINNT 0x0A00 // Windows 10+
#endif


#include <boost/asio.hpp>

#include <filesystem> 

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/xml_parser.hpp>

// Другие полезные header-only компоненты Boost:
#include <boost/smart_ptr.hpp>        // smart_ptr, shared_ptr, weak_ptr, intrusive_ptr
#include <boost/optional.hpp>         // optional
#include <boost/variant.hpp>          // variant
#include <boost/any.hpp>              // any
#include <boost/utility.hpp>          // utility функции
#include <boost/type_traits.hpp>      // type_traits
#include <boost/function.hpp>         // function
#include <boost/bind.hpp>             // bind
#include <boost/algorithm/string.hpp> // строковые алгоритмы

// Стандартные библиотеки C++
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// SDL
#include <SDL3/SDL.h>

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
#include <Core/Utils/RefCounted.h>
#include <Core/Utils/New.h>
