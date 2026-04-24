// src/glm_config.hpp
#pragma once

// ============================================
// CONFIGURAÇÕES GLOBAIS DO GLM
// ============================================


// Habilita extensões experimentais (GTX)
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

// Força uso de radianos (padrão OpenGL)
#ifndef GLM_FORCE_RADIANS
#define GLM_FORCE_RADIANS
#endif

// Força profundidade 0-1 (Vulkan/DirectX style)
#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

// Silencia warnings (opcional)
#ifndef GLM_FORCE_SILENT_WARNINGS
#define GLM_FORCE_SILENT_WARNINGS
#endif

// ============================================
// INCLUDES DO GLM
// ============================================

// Core
#include <glm/glm.hpp>
#include <glm/gtx/matrix_decompose.hpp>
// Stable extensions (GTC)
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>

// Experimental extensions (GTX) - se necessário
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/component_wise.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/transform.hpp>