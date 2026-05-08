#pragma once
#include "transform.hpp"
#include <string>
#include <cstdint>
#include <memory>

#include "axe/mesh/mesh.hpp"
#include "axe/material/material.hpp"
#include "axe/lighting/directional_light.hpp"


namespace axe
{
	
	struct SceneObject
	{
		uint32_t ID = 0;
		std::string Name = "SceneObject";
		Transform TransformData;

		std::shared_ptr<Mesh> MeshData;
		std::shared_ptr<Material> MaterialData;
		std::shared_ptr<DirectionalLight> LightData;
	};
}// namespace axe