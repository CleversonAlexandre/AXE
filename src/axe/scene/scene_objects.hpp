#pragma once
#include "transform.hpp"
#include <string>
#include <cstdint>

namespace axe
{
	struct SceneObject
	{
		uint32_t ID = 0;
		std::string Name = "SceneObject";
		Transform TransformData;
	};
}