#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"

namespace axe
{
	struct AXE_API DirectionalLight
	{
		glm::vec3 Direction { -0.3f, -1.0f, -0.3f }; // aponta para baixo por padrão
		glm::vec3 Color		{ 1.0f, 1.0f, 1.0f }; //branco

		float Intensity = 1.0f;
		float AmbientStrength = 0.15f;
		float SpecularStrength = 0.5f;
		float Shininess = 32.0f;
	};
}//namespace axe

