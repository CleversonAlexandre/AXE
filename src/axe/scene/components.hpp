#pragma once
#include "axe/utils/glm_config.hpp"
#include "axe/scene/transform.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/material/material.hpp"
#include "axe/lighting/directional_light.hpp"
#include <memory>
#include <string>
#include <imgui.h>
#include <entt/entt.hpp>
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

	struct FolderComponent
	{
		ImVec4 Color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
	};
	struct RelationshipComponent
	{
		entt::entity Parent = entt::null;
		std::vector<entt::entity> Children;
	};
}//namespace axe