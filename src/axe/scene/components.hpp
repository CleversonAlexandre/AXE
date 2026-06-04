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
	// Nome do objeto
	struct NameComponent
	{
		std::string Name = "Entity";
	};

	// Transform
	struct TransformComponent
	{
		Transform Data;
	};

	// Mesh
	struct MeshComponent
	{
		std::shared_ptr<Mesh> Data;
		std::string           AssetUUID;
	};

	// Material
	struct MaterialComponent
	{
		std::shared_ptr<Material> Data;
		std::string MaterialAssetUUID;
	};

	// Luz direcional
	struct LightComponent
	{
		std::shared_ptr<DirectionalLight> Data;
	};

	// Point Light
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
		bool IsGlobal = true;
	};

	// Câmera de jogo
	struct CameraComponent
	{
		float Fov = 60.0f;
		float NearClip = 0.1f;
		float FarClip = 1000.0f;
		float MoveSpeed = 5.0f;
		float Sensitivity = 0.1f;
		bool  IsPrimary = true;
	};

	// Environment
	struct EnvironmentComponent
	{
		std::string HDRIPath;
		float       SkyboxRotation = 0.0f;
	};

} // namespace axe