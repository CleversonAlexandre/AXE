
#pragma once

#ifdef AXE_PLATFORM_WINDOWS
#ifdef AXE_BUILD_DLL
#define AXE_API __declspec(dllexport)
#else
#define AXE_API __declspec(dllimport)  
#endif
#else
#define AXE_API
#endif

#ifdef AXE_ENABLE_ASSERTS
#define AXE_ASSERT(x, ...) {if(!(x)) {AXE_ERROR("Assertiom Failed: {0}", __VA_ARGS__); __debugbreak(); } }
#define AXE_CORE_ASSERT(x, ...) {if(!(x)) {AXE_ERROR("Assertiom Failed: {0}", __VA_ARGS__); __debugbreak(); } }
#else
#define	AXE_ASSERT(x, ...)
#define	AXE_CORE_ASSERT(x, ...)
#endif

#define BIT(x) (1 << x)

#define AXE_BIND_EVENT_FN(fn) std::bind(&fn, this, std::placeholders::_1)

#ifdef AXE_DEBUG
#define AXE_CORE_ASSERT(x, ...) { if(!(x)) { AXE_CORE_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
#else
#define AXE_CORE_ASSERT(x, ...)
#endif