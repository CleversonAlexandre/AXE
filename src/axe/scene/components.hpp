#pragma once
#include "axe/utils/glm_config.hpp"
#include "axe/scene/transform.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/material/material.hpp"
#include "axe/lighting/directional_light.hpp"
#include <memory>
#include <string>

namespace axe
{
	//Nome do objeto - todo entity tem um
	struct NameComponent
	{
		std::string Name = "Entity";
	};

	//Transform - posição, rotação, escala 
	struct TransformComponent
	{
		Transform Data;
	};
	//Mesh - geometria do objet
	struct MeshComponent
	{
		std::shared_ptr<Mesh> Data;
		std::string           AssetUUID;
	};

	//Metrial - shader + cor
	struct MaterialComponent
	{
		std::shared_ptr<Material> Data;
	};

	//Luz direcional
	struct LightComponent
	{
		std::shared_ptr<DirectionalLight> Data;
	};
}//namespace axe