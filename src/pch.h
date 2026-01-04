// pch.h
#pragma once

// Чтобы Asio не тянул лишнего из Windows.h
#ifdef _WIN32
#define _WIN32_WINNT 0x0A00  // Windows 10+
#endif

// Стандартные библиотеки C++ для общего использования
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// C стандартные библиотеки
#include <cctype>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

// Math
#include <Math/Color.h>