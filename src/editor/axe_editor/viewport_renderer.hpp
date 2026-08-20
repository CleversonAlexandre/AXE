#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/renderer/scene_renderer.hpp"
#include "axe/graphics/renderer/taa_pass.hpp"
#include "axe/graphics/renderer/ssr_pass.hpp"
#include "axe/graphics/renderer/picking_renderer.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/components.hpp"
#include "axe/scene/transform.hpp"
#include "axe/core/command_history.hpp"
#include <entt/entt.hpp>
#include <functional>
#include <memory>
#include <imgui.h>
#include <ImGuizmo.h>

#include "axe/graphics/renderer/grid_renderer.hpp"
#include "axe/graphics/renderer/collider_debug_renderer.hpp"
#include "axe/graphics/renderer/sound_visualization_renderer.hpp"
#include "axe/graphics/game_camera.hpp"
#include "axe/graphics/renderer/skybox_renderer.hpp"
#include "axe/scene/scene_environment.hpp"
#include "axe/graphics/renderer/post_process_pass.hpp"
#include "axe/graphics/renderer/ssao_pass.hpp"

// SR2 — o caminho de render do JOGO mora aqui.
//
// O ViewportRenderer continua sendo o renderer de EDITOR; o que ele faz
// agora e delegar o frame de Play ao WorldRenderer, em vez de monta-lo
// ele mesmo. Ver a nota em RenderToFramebuffer.
#include "axe/renderer/world_renderer.hpp"

namespace axe
{

	class Framebuffer;
	class EditorCamera;
	class SceneRenderer;
	class Scene;


	class ViewportRenderer
	{
	public:
		void Initialize();

		void SetScene(Scene* scene) { m_Scene = scene; }
		void SetSelectedEntity(entt::entity* e) { m_SelectedEntity = e; }

		void RenderToFramebuffer(Framebuffer& framebuffer, std::uint32_t width,
			std::uint32_t height, float timeSeconds);

		void OnMouseRotate(const glm::vec2& delta);
		void OnMousePan(const glm::vec2& delta);
		void OnMouseZoom(float delta);

		void DrawGuizmo(const glm::vec2& boundsMin, const glm::vec2& boundsMax);

		std::uint32_t PickObject(float mouseX, float mouseY);
		void ResizePicking(std::uint32_t width, std::uint32_t height);

		void SetCommandHistory(CommandHistory* history) { m_CommandHistory = history; }

		bool  ShowGrid = true;
		bool  ShowColliders = true; // wireframe dos colliders no editor
		bool  ShowLights = true;    // wireframe do raio das Point Lights no editor

		// Visualizacao de som (acessibilidade).
		//
		// Ao contrario de ShowColliders e ShowLights, isto NAO e gizmo de
		// editor: e recurso de jogo, e por isso desenha tambem sob a
		// GameCamera. Fica desligado por padrao porque a maioria dos
		// jogadores nao precisa dele — quem precisa liga, e a partir dai vale
		// em Play tambem.
		bool  ShowSoundVisualization = false;
		bool  SnapEnabled = false;
		float SnapValue = 0.5f;   // unidades para translate
		float SnapAngle = 15.0f;  // graus para rotate
		float SnapScale = 0.1f;   // para scale

		ImGuizmo::OPERATION m_GuizmoOperation = ImGuizmo::TRANSLATE;
		std::unique_ptr<EditorCamera> m_Camera;

		// ═══════════════════════════════════════════════════════════════════
		//  GIZMO PEDIDO POR OUTRA FERRAMENTA
		//
		//  O caminho normal do gizmo pressupoe uma ENTIDADE com
		//  TransformComponent. Osso de esqueleto, socket e controle de rig nao
		//  sao entidades e nao tem transform proprio na cena — o Sequencer
		//  precisa manipular exatamente essas tres coisas.
		//
		//  A alternativa seria o Sequencer chamar ImGuizmo por conta propria,
		//  mas ele nao tem (nem deve ter) a camera, o retangulo da imagem do
		//  viewport nem o drawlist certo. Aqui ele entrega uma matriz de mundo
		//  e um callback, e o viewport cuida do resto — inclusive de garantir
		//  que so exista UM gizmo na tela.
		//
		//  Enquanto Active for true, o gizmo de entidade NAO desenha: dois
		//  gizmos sobrepostos disputariam o mesmo clique.
		// ═══════════════════════════════════════════════════════════════════
		struct ExternalGizmo
		{
			bool      Active = false;

			// Matriz de MUNDO do que esta sendo manipulado (entrada).
			glm::mat4 World{ 1.0f };

			// Chamado a cada frame em que o usuario esta arrastando, com a
			// matriz de mundo ja manipulada.
			std::function<void(const glm::mat4&)> OnManipulate;

			// Chamado uma vez quando o arrasto termina. E onde o Sequencer
			// fecha o comando de undo, se houver.
			std::function<void()> OnFinish;
		};

		void SetExternalGizmo(const ExternalGizmo& g) { m_ExternalGizmo = g; }
		void ClearExternalGizmo() { m_ExternalGizmo = ExternalGizmo{}; }

		// Qual operacao o usuario escolheu na barra do viewport (T/R/S). A
		// ferramenta externa precisa saber para decidir QUAIS canais keyar —
		// keyar os nove a cada toque encheria a timeline de curvas retas.
		ImGuizmo::OPERATION GetGizmoOperation() const { return m_GuizmoOperation; }

		void SetGameCamera(GameCamera* cam) { m_GameCamera = cam; }

		// Força recompilação do shader do Lighting Pass — ver
		// SceneRenderer::RecompileLightingShader.
		void RecompileLightingShader()
		{
			if (m_SceneRenderer) m_SceneRenderer->RecompileLightingShader();
		}

		void SetEnvironment(SceneEnvironment* env) { m_Environment = env; }
		void DrawGrid();
		void Resize(uint32_t width, uint32_t height);

		SceneRenderer* GetSceneRenderer() { return m_SceneRenderer.get(); }
		void SetPreviewMode(bool preview) { m_PreviewMode = preview; }

		// ── Drag & Drop ghost preview ─────────────────────────────────────────
		// Chame SetDragGhost antes de RenderToFramebuffer para mostrar silhueta.
		// Chame ClearDragGhost quando o drag terminar.
		void SetDragGhost(std::shared_ptr<Mesh> mesh, const glm::mat4& transform)
		{
			m_GhostMesh = mesh;
			m_GhostTransform = transform;
			m_HasGhost = true;
		}
		void ClearDragGhost() { m_HasGhost = false; m_GhostMesh = nullptr; }
		/// <summary>
		/// temp
		Scene* GetScene() const { return m_Scene; }
		void SetPickingEnabled(bool enabled);
		/// </summary>
	private:
		bool m_PickingEnabled = true;

		// Ghost preview
		bool                   m_HasGhost = false;
		std::shared_ptr<Mesh>  m_GhostMesh;
		glm::mat4              m_GhostTransform{ 1.0f };

		CommandHistory* m_CommandHistory = nullptr;
		bool            m_GizmoWasUsing = false;
		Transform       m_TransformSnapshot;

		// Pedido da ferramenta externa. Reposto TODO FRAME por quem o pediu —
		// ver a nota em SequencerWindow: um gizmo que sobrevivesse ao fechar da
		// janela ficaria pendurado no viewport manipulando um osso de uma
		// sequence que nem esta mais aberta.
		ExternalGizmo   m_ExternalGizmo;
		bool            m_ExternalGizmoWasUsing = false;
		std::unique_ptr<SceneRenderer> m_SceneRenderer;
		PickingRenderer                m_PickingRenderer;

		Scene* m_Scene = nullptr;
		entt::entity* m_SelectedEntity = nullptr;

		GameCamera* m_GameCamera = nullptr;

		SkyboxRenderer   m_SkyboxRenderer;
		GridRenderer            m_GridRenderer;
		ColliderDebugRenderer   m_ColliderDebugRenderer;
		SoundVisualizationRenderer m_SoundVisualization;
		SceneEnvironment* m_Environment = nullptr;

		std::shared_ptr<PostProcessPass>  m_PostProcess;
		std::shared_ptr<Framebuffer>      m_HDRFramebuffer;
		std::shared_ptr<TAAPass>          m_TAAPass;
		TAASettings                       m_TAASettings;
		std::shared_ptr<SSRPass>          m_SSRPass;
		SSRSettings                       m_SSRSettings;
		PostProcessSettings               m_PostProcessSettings;
		bool  m_PreviewMode = false;
		float m_LastTimeSeconds = 0.0f; // para cálculo de dt no Time of Day

		// SR2 — o frame de Play.
		//
		// Criado sempre, inicializado sob demanda (no primeiro Play), porque
		// as ~9 superficies do editor que instanciam um ViewportRenderer
		// (previews de material, rig, particulas, anim clip, anim graph,
		// script, e os dois thumbnail renderers) nunca entram em Play — e
		// alocar GBuffer, HDR e passes para cada uma delas seria dezenas de
		// megabytes de VRAM que ninguem usaria.
		WorldRenderer m_WorldRenderer;
		bool          m_WorldRendererReady = false;
	};

} // namespace axe