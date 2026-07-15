#pragma once
#include "axe/utils/glm_config.hpp"
#include "axe/scene/transform.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/animation/skinned_mesh.hpp"
#include "axe/animation/skeleton.hpp"
#include "axe/animation/animation_clip.hpp"
#include "axe/animation/animation_player.hpp"
#include "axe/animation/blend_space_1d.hpp"
#include "axe/animation/anim_graph.hpp"
#include "axe/animation/anim_graph_instance.hpp"
#include "axe/animation/skeletal_mesh_asset.hpp"
#include "axe/animation/anim_graph_asset.hpp"
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

	// Skeletal Mesh — personagem animado.
	//
	// Convive com MaterialComponent normalmente: graças ao Skin Cache, o
	// material de um personagem é um material COMUM (o mesmo node graph que
	// você usa numa parede). Nenhuma variante skinned é necessária.
	//
	// Uma entidade tem MeshComponent OU SkeletalMeshComponent — nunca os
	// dois. O SceneCollector prioriza o skeletal se ambos existirem.
	struct SkeletalMeshComponent
	{
		// O ASSET e a fonte de verdade. Data/Clips abaixo sao so o conteudo
		// resolvido dele, copiado pra ca por conveniencia do runtime.
		//
		// Dez inimigos arrastados pra cena apontam pro MESMO
		// SkeletalMeshAsset — e portanto compartilham UMA malha na GPU.
		std::shared_ptr<SkeletalMeshAsset> Asset;

		std::shared_ptr<SkinnedMesh> Data;

		// UUID do .axeskel. E a UNICA coisa que a cena serializa — ao
		// recarregar, o asset e reencontrado no AssetDatabase e resolvido.
		std::string                  AssetUUID;

		// Clipes disponíveis. Vêm do próprio arquivo do personagem e/ou de
		// SkeletalMeshLoader::LoadClips() em arquivos separados
		// (idle.fbx, run.fbx...), religados por nome de bone.
		std::vector<std::shared_ptr<AnimationClip>> Clips;

		// ── Seleção da animação ──────────────────────────────────────────
		//
		// Três formas, em ordem de PRIORIDADE (a primeira setada vence):
		//
		//   Graph        — a state machine completa. É o modo "de produção":
		//                  o gameplay só escreve parâmetros (Speed,
		//                  IsGrounded, Attack) e o grafo decide tudo.
		//
		// As duas abaixo continuam existindo e são úteis: um NPC de fundo,
		// um prop animado ou um teste rápido não precisam de grafo nenhum.
		//
		//   CurrentClip  — um clipe por vez, com crossfade automático na
		//                  troca. Bom pra ações discretas (atacar, pular).
		//
		//   BlendSpace   — um contínuo dirigido por um parâmetro. Bom pra
		//                  locomoção: em vez de trocar de idle pra walk pra
		//                  run, você só varia a velocidade e a pose
		//                  acompanha, sem "pop" nenhum.

		// Asset .axeanim. E o que a cena serializa (so o UUID) e o que o
		// editor de nos abre. Se != nullptr, tem prioridade sobre BlendSpace
		// e CurrentClip: quem manda passa a ser o grafo.
		//
		// NAO ha mais um `shared_ptr<AnimGraph>` aqui. Havia, e era um erro: o
		// grafo do asset e um MOLDE. Quem guarda a copia viva deste personagem
		// e o GraphInstance — senao dois personagens com o mesmo .axeanim
		// compartilhariam o tempo da animacao e o estado da maquina.
		std::shared_ptr<AnimGraphAsset> GraphAsset;
		std::string                     GraphAssetUUID;

		// Runtime do grafo — a COPIA dos nos, o blackboard, e o pool de poses.
		// O Script Editor escreve em GraphInstance.Params.
		AnimGraphInstance GraphInstance;

		// Índice em Clips. -1 = nenhum -> bind pose (T-pose).
		int   CurrentClip = -1;

		// Duração do crossfade quando CurrentClip muda.
		float BlendTime = 0.2f;

		// Se != nullptr, ignora CurrentClip e usa o blend space.
		std::shared_ptr<BlendSpace1D> BlendSpace;

		// Posição no eixo do blend space (tipicamente a velocidade do pawn,
		// escrita pelo Script Editor a cada frame).
		float BlendParam = 0.0f;
		float BlendSpaceTime = 0.0f;

		// Runtime do crossfade/camadas. É ele que faz o trabalho de verdade.
		AnimationPlayer Player;

		// Última CurrentClip que o AnimationWorld realmente aplicou. Serve
		// pra detectar a MUDANÇA (e disparar o crossfade) em vez de
		// reiniciar o clipe todo frame.
		int _AppliedClip = -2;   // -2 = nunca aplicado (distinto de "nenhum")

		// Preenchida pelo AnimationWorld a cada frame, consumida pelo
		// SceneCollector. Vive no componente (e não no renderer) porque é
		// dado de SIMULAÇÃO, não de render — gameplay pode querer ler a
		// pose (IK, sockets, hitbox por bone).
		std::vector<glm::mat4> BonePalette;

		// Anima no EDITOR, sem precisar apertar Play.
		//
		// E como o Unreal e o Unity se comportam, e por um bom motivo: a
		// primeira coisa que voce faz ao importar um personagem e conferir se
		// a animacao esta certa. Obrigar a entrar em Play so pra isso torna o
		// ciclo de iteracao lento e o preview inutil.
		//
		// O botao Tocar/Pausar do Inspector controla Player.Playing; este flag
		// controla se o tempo corre fora do Play.
		bool PreviewInEditor = true;

		// ── Debug ────────────────────────────────────────────────────────
		//
		// Desenha o esqueleto como linhas por cima da malha.
		//
		// Esta é A ferramenta de diagnóstico do sistema inteiro. Se o
		// personagem aparecer errado:
		//
		//   ossos CERTOS + malha explodida  -> o bug esta no compute shader
		//                                      ou nos pesos (skinning)
		//   ossos JA TORTOS                 -> o bug esta no loader ou no
		//                                      sampler (hierarquia/matrizes)
		//
		// Corta o espaco de busca pela metade em dois segundos.
		bool ShowSkeleton = false;

		// Matrizes GLOBAIS dos ossos (model-space), preenchidas pelo
		// AnimationWorld so quando ShowSkeleton esta ligado. Sao diferentes
		// da BonePalette: a palette ja tem a InverseBindPose aplicada e nao
		// serve pra saber ONDE o osso esta.
		std::vector<glm::mat4> BoneGlobals;

		const Skeleton* GetSkeleton() const
		{
			return (Data && Data->GetSkeleton()) ? Data->GetSkeleton().get() : nullptr;
		}
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