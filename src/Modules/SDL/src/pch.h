// pch.h
#pragma once

// Чтобы Asio не тянул лишнего из Windows.h
#ifdef _WIN32
#define _WIN32_WINNT 0x0A00  // Windows 10+
#endif

// Boost компоненты
#include <boost/algorithm/string.hpp>  // строковые алгоритмы

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
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

// SDL3
#include <SDL3/SDL.h>

// PugiXML (через Core)
#include <pugixml.hpp>

