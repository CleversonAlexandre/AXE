#pragma once

// Configuracao centralizada do Jolt Physics
// Este header deve ser incluido ANTES de qualquer header do Jolt
// tanto no Jolt.lib quanto no axe.dll

// Sem double precision (nao precisamos)
// #define JPH_DOUBLE_PRECISION

// Sem determinismo cross-platform (nao precisamos)
// #define JPH_CROSS_PLATFORM_DETERMINISTIC

// Sem floating point exceptions
// #define JPH_FLOATING_POINT_EXCEPTIONS_ENABLED

// Sem profiling
// #define JPH_PROFILE_ENABLED
// #define JPH_EXTERNAL_PROFILE

// Sem debug renderer (causaria simbolos nao implementados)
// #define JPH_DEBUG_RENDERER

// Com temp allocator (padrao)
// #define JPH_DISABLE_TEMP_ALLOCATOR

// Com custom allocator (padrao)
// #define JPH_DISABLE_CUSTOM_ALLOCATOR

// Object layer bits (padrao = 16 bits)
// #define JPH_OBJECT_LAYER_BITS 32

// Com asserts apenas em Debug
#ifdef _DEBUG
#define JPH_ENABLE_ASSERTS
#endif

// Com object stream (necessario para shapes e serialization)
#define JPH_OBJECT_STREAM