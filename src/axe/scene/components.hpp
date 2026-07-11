#pragma once
#include "axe/utils/glm_config.hpp"
#include "axe/scene/transform.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/material/material.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/lighting/point_light.hpp"
#include "axe/lighting/interior_volume.hpp"
#include "axe/lighting/probe_volume.hpp"
#include "axe/lighting/reflection_probe.hpp"
#include "axe/physics/physics_components.hpp"
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

	// Interior Volume — caixa que bloqueia sol + ambient/IBL em ambientes
	// fechados. O tamanho da caixa vem da ESCALA do Transform da entity.
	// Ver comentário completo em axe/lighting/interior_volume.hpp.
	struct InteriorVolumeComponent
	{
		InteriorVolume Data;
	};

	// Reflection Probe — cubemap local pré-filtrado pro especular.
	// Ver comentário completo em axe/lighting/reflection_probe.hpp.
	struct ReflectionProbeComponent
	{
		ReflectionProbeSettings Settings;

		// Resultado da captura — runtime only, nunca serializado (barato
		// de recapturar: o load da cena rebakeia via BakeRequested).
		std::shared_ptr<ReflectionCapture> Capture;

		bool BakeRequested = false;
	};

	// Probe Volume (Light Probes / GI-lite) — grid de irradiância SH L1
	// bakeada. Ver comentário completo em axe/lighting/probe_volume.hpp.
	struct ProbeVolumeComponent
	{
		ProbeVolumeSettings Settings;

		// Resultado do bake — runtime only, NUNCA serializado (o load da
		// cena dispara um rebake automático via BakeRequested).
		std::shared_ptr<ProbeGrid> Grid;

		// Setado pelo Inspector (botão "Bake") ou pelo load da cena;
		// consumido (e resetado) pelo SceneCollector, que enfileira um
		// ProbeBakeRequest na RenderQueue — o editor nunca fala com o
		// renderer diretamente.
		bool BakeRequested = false;
	};

	// Câmera de jogo
	// ── Spring Arm ───────────────────────────────────────────────────────────
	// Define a posição da câmera em relação à entidade (braço de câmera).
	// Usado pelo GameCamera em modo ThirdPerson.
	struct SpringArmComponent
	{
		float Length = 5.0f;    // distância atrás do pawn
		float HeightOffset = 2.0f; // altura acima do pawn
		glm::vec3 SocketOffset = { 0, 0, 0 }; // offset lateral/depth fino
		float LagSpeed = 8.0f;   // suavização do follow (lerp)
		bool  EnableCameraLag = true;
		bool  MouseRotates = true;  // mouse orbita a câmera
	};

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