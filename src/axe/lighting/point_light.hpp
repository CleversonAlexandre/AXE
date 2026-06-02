#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"

namespace axe
{
	struct AXE_API PointLight
	{
		glm::vec3 Position{ 0.0f, 1.0f, 0.0f };
		glm::vec3 Color{ 1.0f, 1.0f, 1.0f };

		float Intensity = 5.0f;
		float Radius = 10.0f; //Raio de influência (Atenuação fisica)
	};
}//namespace axe
