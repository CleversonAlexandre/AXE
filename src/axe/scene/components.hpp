#pragma once
#include "axe/utils/glm_config.hpp"
#include "axe/scene/transform.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/material/material.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/lighting/point_light.hpp"
#include <memory>
#include <string>
#include <imgui.h>
#include <entt/entt.hpp>

#include "axe/graphics/renderer/post_process_pass.hpp"
#include "axe/graphics/renderer/ssao_pass.hpp"

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
		std::string MaterialAssetUUID;
	};

	//Luz direcional
	struct LightComponent
	{
		std::shared_ptr<DirectionalLight> Data;
	};

	//Point Light
	struct PointLightComponent
	{
		std::shared_ptr<PointLight> Data;
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

	struct PostProcessComponent
	{
		PostProcessSettings Settings;
		SSAOSettings        SSAO;
		bool IsGlobal = true; // afeta toda a cena
	};

	// Componente que liga uma entity ao SceneEnvironment do editor
	// Permite editar HDRI e rotação do skybox pelo inspector
	struct EnvironmentComponent
	{
		std::string HDRIPath;
		float       SkyboxRotation = 0.0f;
		std::shared_ptr<DirectionalLight> Data;
	};
	
 
}//namespace axe