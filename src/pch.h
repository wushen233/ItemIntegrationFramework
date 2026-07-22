#pragma once

// ==========================================
// 1. 宏定义：必须放在最上面
// ==========================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

// ==========================================
// 2. 游戏逆向基础库 (必须在 Windows API 之前！)
// ==========================================
#include <RE/Fallout.h>   // REX and Scaleform are included transitively
#include <F4SE/F4SE.h>

// ==========================================
// 3. Windows API 与 DirectX (必须在 CommonLibF4 之后)
// ==========================================
#include <winsock2.h>
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

// ==========================================
// 4. 终极净化：解除 Windows 宏污染 
// (消除 ERROR 被替换为 0 导致的 C2589 编译错误)
// ==========================================
#ifdef ERROR
#undef ERROR
#endif
#ifdef GetObject
#undef GetObject
#endif
#ifdef GetMessage
#undef GetMessage
#endif

// ==========================================
// 5. STL 常用库
// ==========================================
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cmath>

// 常用宏定义
#define MAKE_EXE_VERSION_EX(major, minor, build, sub) ((((major) & 0xFF) << 24) | (((minor) & 0xFF) << 16) | (((build) & 0xFFF) << 4) | ((sub) & 0xF))
#define MAKE_EXE_VERSION(major, minor, build)         MAKE_EXE_VERSION_EX(major, minor, build, 0)

using namespace std::literals;
using namespace F4SE;