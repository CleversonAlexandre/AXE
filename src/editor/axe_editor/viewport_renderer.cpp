#include "editor/axe_editor/viewport_renderer.hpp"
#include "axe/audio/audio_engine.hpp"
#include "axe/log/log.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/editor_camera.hpp"
#include "axe/renderer/scene_renderer.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/components.hpp"
#include "axe/utils/glm_config.hpp"

// viewport_renderer.cpp — sem includes diretos de OpenGL
#include <imgui.h>
#include <ImGuizmo.h>

#include "axe/graphics/game_camera.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/graphics/renderer/taa_pass.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/mesh/mesh.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <cmath>

static float smoothstep(float edge0, float edge1, float x) {
	float t = std::max(0.f, std::min(1.f, (x - edge0) / (edge1 - edge0)));
	return t * t * (3.f - 2.f * t);
}

namespace {
	// Shader de silhueta para drag preview — cor sólida + alpha baixo
	static const char* s_GhostVert = R"(
	#version 460 core
	layout(location = 0) in vec3 a_Position;
	layout(location = 1) in vec3 a_Normal;
	uniform mat4 u_ViewProjection;
	uniform mat4 u_Model;
	void main() {
		gl_Position = u_ViewProjection * u_Model * vec4(a_Position, 1.0);
	}
)";
	static const char* s_GhostFrag = R"(
	#version 460 core
	out vec4 FragColor;
	uniform vec4 u_Color;
	void main() { FragColor = u_Color; }
)";
	static std::shared_ptr<axe::Shader> s_GhostShader;
} // namespace

namespace axe
{

	static bool DecomposeTransform(const glm::mat4& transform, glm::vec3& position,
		glm::vec3& rotation, glm::vec3& scale)
	{
		using namespace glm;
		vec3 skew; vec4 perspective; quat orientation;
		if (!decompose(transform, scale, orientation, position, skew, perspective))
			return false;
		rotation = eulerAngles(orientation);
		return true;
	}


	void ViewportRenderer::Initialize()
	{
		s_GhostShader = axe::Shader::Create(s_GhostVert, s_GhostFrag);
		m_SceneRenderer = std::make_unique<SceneRenderer>();
		m_Camera = std::make_unique<EditorCamera>(45.0f, 1.0f, 0.1f, 1000.0f);
		m_SkyboxRenderer.Initialize();
		m_GridRenderer.Initialize();
		m_ColliderDebugRenderer.Initialize();

		FramebufferSpecification hdrSpec;
		hdrSpec.Width = 1280;
		hdrSpec.Height = 720;
		hdrSpec.Attachments = {
			FramebufferTextureFormat::RGBA16F,
			FramebufferTextureFormat::DEPTH32F,
		};
		m_HDRFramebuffer = Framebuffer::Create(hdrSpec);

		m_PostProcess = PostProcessPass::Create();
		m_PostProcess->Initialize(1280, 720);

		// ✅ Inicializa SSAO aqui — contexto OpenGL garantido
		m_SceneRenderer->InitializeDeferredPasses(1280, 720);
	}


	void ViewportRenderer::SetPickingEnabled(bool enabled)
	{
		m_PickingEnabled = enabled;
	}

	void ViewportRenderer::RenderToFramebuffer(Framebuffer& framebuffer,
		std::uint32_t width, std::uint32_t height, float timeSeconds)
	{
		// ── SR2: o frame de Play nao e montado aqui ──────────────────────────
		//
		// Tudo o que vinha depois deste bloco no ramo `if (m_GameCamera)` — HDR,
		// environment, ceu procedural, skybox, TAA, SSR, post-process e a
		// visualizacao de som — mudou para o WorldRenderer, em src/axe/renderer/.
		//
		// POR QUE: aquele ramo nunca foi editor; era o frame do jogo. E este
		// arquivo inclui <imgui.h> e <ImGuizmo.h>. Enquanto o unico caminho que
		// sabia montar um frame de jogo morasse aqui, o `game.exe` teria que
		// duplicar ~150 linhas ou arrastar ImGuizmo para dentro do jogo. Mesmo
		// padrao do B1, um andar abaixo.
		//
		// POR QUE O RETURN CEDO, e nao uma refatoracao do metodo inteiro: nove
		// superficies do editor instanciam um ViewportRenderer (previews de
		// material, rig, particulas, anim clip, anim graph, script, e os dois
		// thumbnail renderers). Mexer no caminho de Edit poria as nove em risco
		// de uma vez, por ganho estetico. Abaixo desta linha, nada mudou.
		//
		// O CUSTO, assumido: o caminho de Edit mantem sua propria copia de SSR,
		// TAA, post-process e sync de environment. Sao duas implementacoes que
		// podem divergir. A rota de saida e o Edit passar a delegar tambem — mas
		// isso e patch proprio, com as nove superficies testadas uma a uma, e
		// nao um efeito colateral deste.
		//
		// GANHO IMEDIATO: o Play do editor renderiza pelo MESMO caminho que o
		// jogo empacotado vai usar. O WorldRenderer fica exercitado todo dia,
		// em vez de ser um caminho que so seria descoberto quebrado no dia do
		// empacotamento.
		if (m_GameCamera)
		{
			if (!m_WorldRendererReady)
			{
				m_WorldRenderer.Initialize();
				m_WorldRendererReady = true;
			}

			const float aspect = height > 0 ? (float)width / (float)height : 1.0f;

			WorldRenderer::FrameParams p;
			p.WorldScene = m_Scene;
			p.Environment = m_Environment;
			p.View = m_GameCamera->GetViewMatrix();
			p.Projection = m_GameCamera->GetProjectionMatrix(aspect);
			p.EyePosition = m_GameCamera->GetPosition();
			p.Width = width;
			p.Height = height;
			p.TimeSeconds = timeSeconds;
			p.ShowSoundVisualization = ShowSoundVisualization;

			m_WorldRenderer.RenderToFramebuffer(framebuffer, p);
			return;
		}

		// 1. Resize HDR primeiro
		auto& hdrSpec = m_HDRFramebuffer->GetSpecification();
		if (hdrSpec.Width != width || hdrSpec.Height != height)
			m_HDRFramebuffer->Resize(width, height);

		if (!m_PostProcess->IsInitialized())
			m_PostProcess->Initialize(width, height);
		else
			m_PostProcess->Resize(width, height);

		// Configura SceneRenderer para editor camera OU game camera
		if (m_GameCamera)
		{
			m_SceneRenderer->SetTargetFramebuffer(m_HDRFramebuffer->GetRendererID());
			m_SceneRenderer->SetDeferredEnabled(true);
			m_SceneRenderer->SetDeferredSupported(true);
		}
		else if (m_Camera)
		{
			m_SceneRenderer->SetTargetFramebuffer(m_HDRFramebuffer->GetRendererID());
			m_SceneRenderer->SetDeferredEnabled(!m_PreviewMode);
			if (m_PreviewMode)
				m_SceneRenderer->SetDeferredSupported(false);
		}

		// Sincroniza HDRI ANTES de bindar o framebuffer —
		// LoadFromHDRI muda viewport e FBO internamente, deve rodar com estado limpo
		if (m_Scene)
		{
			auto& registry = m_Scene->GetRegistry();
			for (auto entity : registry.view<EnvironmentComponent>())
			{
				auto& ec = registry.get<EnvironmentComponent>(entity);
				if (m_Environment)
				{
					m_Environment->SkyboxRotation = ec.SkyboxRotation;
					if (!ec.HDRIPath.empty() && ec.HDRIPath != m_Environment->SkyboxPath)
						m_Environment->LoadHDRI(ec.HDRIPath);
				}
				break;
			}

			// ── Céu Procedural + Time of Day — lê do Directional Light ────────
			// O sol É a luz direcional, então faz sentido controlar aqui.
			for (auto le : registry.view<LightComponent>())
			{
				auto& lc = registry.get<LightComponent>(le);
				if (!lc.Data) continue;
				auto& dl = *lc.Data;

				if (!dl.ProceduralSky)
				{
					if (m_SceneRenderer)
						m_SceneRenderer->SetProceduralSky(false, { 0,1,0 },
							2.5f, 0.5f, 0.02f, { 1,1,1 }, { 0.01f,0.01f,0.03f });
					break;
				}

				// Calcula direção do sol (a partir do Time of Day ou da direção manual)
				glm::vec3 sunDir = glm::normalize(-dl.Direction);

				if (dl.TimeOfDayEnabled)
				{
					float dt = timeSeconds - m_LastTimeSeconds;
					if (dt < 0.0f || dt > 0.5f) dt = 0.016f;

					dl.Hour = std::fmod(dl.Hour + dt * (dl.DaySpeed / 3600.0f), 24.0f);

					// Ângulo horário: 0 ao meio-dia (12h), π/2 ao pôr do sol (18h)
					float hourAngle = (dl.Hour - 12.0f) * (3.14159f / 12.0f);
					float latRad = dl.SunLatitude * (3.14159f / 180.0f);
					float elevation = std::asin(std::cos(latRad) * std::cos(hourAngle));
					float azimuth = std::atan2(std::sin(hourAngle),
						std::cos(hourAngle) * std::sin(latRad));

					sunDir = glm::normalize(glm::vec3(
						std::cos(elevation) * std::sin(azimuth),
						std::sin(elevation),
						std::cos(elevation) * std::cos(azimuth)));

					// Atualiza direção, cor e intensidade da luz pelo ciclo solar
					dl.Direction = -sunDir;
					float elev = std::max(0.0f, sunDir.y);
					float sunsetF = smoothstep(0.0f, 0.3f, elev);
					dl.Color = glm::mix(
						glm::vec3(1.0f, 0.42f, 0.08f),
						glm::vec3(1.0f, 0.93f, 0.88f), sunsetF);
					dl.Intensity = elev * 8.0f;
				}

				if (m_SceneRenderer)
					m_SceneRenderer->SetProceduralSky(true, sunDir,
						dl.Turbidity, dl.CloudCoverage, dl.CloudSpeed,
						dl.CloudColor, dl.NightColor);
				break;
			}
			m_LastTimeSeconds = timeSeconds;
		}

		// 3. Binda HDR e limpa
		m_HDRFramebuffer->Bind();
		RenderCommand::SetColorWrite(true);
		RenderCommand::SetDepthWrite(true);
		RenderCommand::SetViewport(0, 0, width, height);
		RenderCommand::SetClearColor(0.1f, 0.1f, 0.12f, 1.0f);
		RenderCommand::Clear();

		entt::entity selected = m_SelectedEntity ? *m_SelectedEntity : entt::null;

		// SR2 — INALCANCAVEL a partir daqui.
		//
		// O return cedo no topo do metodo garante que `m_GameCamera` e nulo
		// nesta altura, entao este ramo e todos os ternarios
		// `m_GameCamera ? A : B` mais abaixo sempre resolvem para o lado do
		// editor.
		//
		// Deixado no lugar DE PROPOSITO, e nao apagado: remover o if/else
		// significa reindentar ~70 linhas do caminho de Edit, que e o caminho
		// usado pelas nove superficies de preview. Trocar risco real por
		// higiene de codigo, num patch cujo objetivo era justamente nao
		// encostar no Edit, seria o negocio errado.
		//
		// Sai junto com a delegacao do caminho de Edit (SR2b), quando as nove
		// superficies forem testadas uma a uma.
		if (m_GameCamera)
		{
			float aspect = height > 0 ? (float)width / (float)height : 1.0f;

			// Lê PostProcess igual ao modo editor
			if (m_Scene)
			{
				auto& registry = m_Scene->GetRegistry();
				for (auto entity : registry.view<PostProcessComponent>())
				{
					auto& pp = registry.get<PostProcessComponent>(entity);
					m_PostProcessSettings = pp.Settings;
					if (m_SceneRenderer)
					{
						m_SceneRenderer->SetSSAOSettings(pp.SSAO);
						m_SceneRenderer->SetFogSettings(pp.Settings.Fog);
						m_TAASettings = pp.Settings.TAA;
						m_SSRSettings = pp.Settings.SSR;
					}
					break;
				}
			}

			if (m_SceneRenderer && m_Environment)
				m_SceneRenderer->SetEnvironment(m_Environment);

			if (m_SceneRenderer && m_Environment && m_Environment->HasSkybox())
			{
				m_SkyboxRenderer.SetCubemap(m_Environment->Skybox);
				// Mesmo tratamento do caminho do Editor: remove a translação
				// da view (o céu fica "infinitamente distante", não anda
				// junto com a câmera) e aplica a rotação configurada do
				// skybox. Antes o Play usava a view da câmera direto —
				// o céu deslizava ao mover e a rotação configurada não
				// tinha efeito nenhum.
				m_SceneRenderer->SetSkyboxRenderer(
					&m_SkyboxRenderer,
					m_Environment->GetSkyboxView(m_GameCamera->GetViewMatrix()),
					m_GameCamera->GetProjectionMatrix(aspect));
			}
			else if (m_SceneRenderer)
			{
				m_SceneRenderer->SetSkyboxRenderer(nullptr, {}, {});
			}

			if (m_SceneRenderer && m_Scene)
			{
				glm::mat4 proj = m_GameCamera->GetProjectionMatrix(aspect);
				glm::mat4 view = m_GameCamera->GetViewMatrix();

				// Inicializa TAA se ativo — jitter na projection antes do render
				if (m_TAASettings.Enabled)
				{
					if (!m_TAAPass) { m_TAAPass = TAAPass::Create(); m_TAAPass->Initialize(width, height); }
					m_TAAPass->BeginFrame(proj * view);
					proj = m_SceneRenderer->BeginTAAFrame(proj, proj * view, width, height);
				}

				m_SceneRenderer->RenderScene(
					*m_Scene,
					view, proj,
					m_GameCamera->GetPosition(),
					entt::null,
					width, height);
			}
		}
		else
		{
			if (m_Camera && height > 0)
			{
				m_Camera->SetAspectRatio((float)width / (float)height);
				m_Camera->SetViewportSize((float)width, (float)height);
			}
			if (m_SceneRenderer && m_Environment)
				m_SceneRenderer->SetEnvironment(m_Environment);

			// Lê PostProcessComponent antes de renderizar
			if (m_Scene)
			{
				auto& registry = m_Scene->GetRegistry();
				for (auto entity : registry.view<PostProcessComponent>())
				{
					auto& pp = registry.get<PostProcessComponent>(entity);
					m_PostProcessSettings = pp.Settings;
					if (m_SceneRenderer)
					{
						m_SceneRenderer->SetSSAOSettings(pp.SSAO);
						m_SceneRenderer->SetFogSettings(pp.Settings.Fog);
						m_TAASettings = pp.Settings.TAA;
						m_SSRSettings = pp.Settings.SSR;
					}
					break;
				}
			}

			// Skybox
			if (m_SceneRenderer && m_Environment && m_Environment->HasSkybox() && m_Camera)
			{
				m_SkyboxRenderer.SetCubemap(m_Environment->Skybox);
				m_SceneRenderer->SetSkyboxRenderer(
					&m_SkyboxRenderer,
					m_Environment->GetSkyboxView(m_Camera->GetViewMatrix()),
					m_Camera->GetProjectionMatrix());
			}
			else if (m_SceneRenderer)
			{
				m_SceneRenderer->SetSkyboxRenderer(nullptr, {}, {});
			}

			if (m_SceneRenderer && m_Scene && m_Camera)
				m_SceneRenderer->RenderScene(*m_Scene, *m_Camera, selected);

			// Grid — dentro do HDR framebuffer, com valores lineares baixos
			// que sobrevivem ao tone mapping sem saturar
			if (ShowGrid && m_Camera)
				m_GridRenderer.Render(
					m_Camera->GetViewMatrix(),
					m_Camera->GetProjectionMatrix());

			// Picking
			if (m_Scene && m_Camera && m_PickingEnabled)
			{
				m_PickingRenderer.Resize(width, height);
				m_PickingRenderer.Begin(m_Camera->GetViewProjectionMatrix());
				auto& registry = m_Scene->GetRegistry();
				for (auto entity : registry.view<TransformComponent>())
				{
					if (registry.any_of<LightComponent>(entity)) continue;
					auto& tc = registry.get<TransformComponent>(entity);
					glm::mat4 model = tc.Data.GetMatrix();
					auto* mc = registry.try_get<MeshComponent>(entity);
					std::uint32_t pickID = (std::uint32_t)entity;
					if (mc && mc->Data)
						m_PickingRenderer.DrawMesh(*mc->Data, model, pickID);
					else
						m_PickingRenderer.DrawCube(model, pickID);
				}
				m_PickingRenderer.End();
			}
		}

		m_HDRFramebuffer->Unbind();

		uint32_t finalColorID = m_HDRFramebuffer->GetColorAttachmentRendererID();

		// 3.5. SSR — reflexões screen-space. Roda antes do TAA pra ser
		// estabilizado por ele. Precisa do GBuffer (position/normal/pbr).
		if (m_SSRSettings.Enabled && m_SceneRenderer)
		{
			if (!m_SSRPass)
			{
				m_SSRPass = SSRPass::Create();
				m_SSRPass->Initialize(width, height);
			}
			else
			{
				m_SSRPass->Resize(width, height);
			}

			if (m_SSRPass && m_SSRPass->IsInitialized())
			{
				glm::mat4 proj = m_GameCamera
					? m_GameCamera->GetProjectionMatrix((float)width / (float)height)
					: (m_Camera ? m_Camera->GetProjectionMatrix() : glm::mat4(1.f));
				glm::mat4 viewM = m_GameCamera
					? m_GameCamera->GetViewMatrix()
					: (m_Camera ? m_Camera->GetViewMatrix() : glm::mat4(1.f));

				uint32_t ssrResult = m_SSRPass->Execute(
					m_SceneRenderer->GetGBuffer(), finalColorID,
					proj, viewM, m_SSRSettings, width, height);
				if (ssrResult != 0) finalColorID = ssrResult;
			}
		}

		// 4. TAA Resolve (se ativo) — entre o SSR e o post-process.
		if (m_TAASettings.Enabled)
		{
			if (!m_TAAPass)
			{
				m_TAAPass = TAAPass::Create();
				m_TAAPass->Initialize(width, height);
			}
			else
			{
				m_TAAPass->Resize(width, height);
			}

			if (m_TAAPass && m_TAAPass->IsInitialized() && m_SceneRenderer)
			{
				glm::mat4 vp = m_GameCamera
					? m_GameCamera->GetViewMatrix()
					: (m_Camera ? m_Camera->GetViewMatrix() : glm::mat4(1.f));
				glm::mat4 proj = m_GameCamera
					? m_GameCamera->GetProjectionMatrix((float)width / (float)height)
					: (m_Camera ? m_Camera->GetProjectionMatrix() : glm::mat4(1.f));
				glm::mat4 fullVP = proj * vp;
				glm::mat4 invVP = glm::inverse(fullVP);
				glm::vec2 jitter = m_TAAPass->GetCurrentJitter();
				glm::mat4 prevVP = m_TAAPass->GetPrevViewProj();

				uint32_t depthID = 0; // GBuffer depth (obtido via SceneRenderer)
				// Acessa depth do GBuffer pelo SceneRenderer
				if (auto* sr = m_SceneRenderer.get())
					depthID = sr->GetGBufferDepthID();

				uint32_t resolved = m_TAAPass->Execute(
					finalColorID, depthID,
					invVP, prevVP, jitter,
					m_TAASettings, width, height);
				if (resolved != 0) finalColorID = resolved;
			}
		}

		// 5. Post process
		framebuffer.Bind();
		RenderCommand::SetViewport(0, 0, width, height);

		m_PostProcess->Execute(finalColorID, m_PostProcessSettings);

		// Collider wireframes — só no modo editor
		if (ShowColliders && m_Camera && !m_GameCamera && m_Scene)
			m_ColliderDebugRenderer.Render(
				*m_Scene,
				m_Camera->GetViewMatrix(),
				m_Camera->GetProjectionMatrix());

		// Raio de alcance das Point Lights — só no modo editor
		if (ShowLights && m_Camera && !m_GameCamera && m_Scene)
			m_ColliderDebugRenderer.RenderLights(
				*m_Scene,
				m_Camera->GetViewMatrix(),
				m_Camera->GetProjectionMatrix());

		// ── Visualizacao de som ─────────────────────────────────────────────────
		//
		// SEM a guarda `!m_GameCamera` que os dois debug renderers acima usam:
		// eles sao gizmo de autoria e nao podem aparecer no jogo; este e
		// recurso de acessibilidade e existe PRA aparecer no jogo.
		//
		// A camera usada e a ativa — GameCamera em Play, editor no viewport —
		// porque o anel e billboard e precisa encarar quem esta olhando.
		if (ShowSoundVisualization)
		{
			const glm::mat4 v = m_GameCamera
				? m_GameCamera->GetViewMatrix()
				: (m_Camera ? m_Camera->GetViewMatrix() : glm::mat4(1.0f));

			// GameCamera::GetProjectionMatrix EXIGE o aspect: ela nao guarda o
			// tamanho do alvo, ao contrario da EditorCamera. Mesma forma que
			// as outras chamadas deste arquivo ja usam.
			const glm::mat4 p = m_GameCamera
				? m_GameCamera->GetProjectionMatrix((float)width / (float)height)
				: (m_Camera ? m_Camera->GetProjectionMatrix() : glm::mat4(1.0f));

			const glm::vec3 eye = m_GameCamera
				? m_GameCamera->GetPosition()
				: (m_Camera ? m_Camera->GetPosition() : glm::vec3(0.0f));

			// Depth desligado de proposito: o pulso tem que ser visivel
			// ATRAVES da parede. Um anel oculto pelo cenario nao informaria
			// nada sobre o inimigo do outro lado — que e o caso de uso.
			RenderCommand::SetDepthTest(false);
			RenderCommand::SetBlend(true);
			RenderCommand::SetBlendFunc(RendererAPI::BlendFactor::SrcAlpha,
				RendererAPI::BlendFactor::OneMinusSrcAlpha);

			m_SoundVisualization.Render(AudioEngine::GetActiveSounds(), v, p, eye);

			RenderCommand::SetBlend(false);
			RenderCommand::SetDepthTest(true);
		}

		// ── Ghost preview de drag & drop ────────────────────────────────────────
		// Renderizado no framebuffer final com blending, sobre tudo
		if (m_HasGhost && m_GhostMesh && m_Camera && s_GhostShader)
		{
			framebuffer.Bind();
			RenderCommand::SetViewport(0, 0, width, height);

			// Blending: silhueta azul-ciano semitransparente
			RenderCommand::SetBlend(true);
			RenderCommand::SetBlendFunc(RendererAPI::BlendFactor::SrcAlpha, RendererAPI::BlendFactor::OneMinusSrcAlpha);
			RenderCommand::SetDepthWrite(false); // não escreve depth — fica sempre visível
			RenderCommand::SetDepthTest(false);
			RenderCommand::SetCullFace(false);

			glm::mat4 vp = m_Camera->GetViewProjectionMatrix();
			s_GhostShader->Bind();
			s_GhostShader->SetMat4("u_ViewProjection", glm::value_ptr(vp));
			s_GhostShader->SetMat4("u_Model", glm::value_ptr(m_GhostTransform));
			s_GhostShader->SetFloat4("u_Color", { 0.3f, 0.7f, 1.0f, 0.45f }); // azul-ciano

			m_GhostMesh->GetVertexArray()->Bind();
			RenderCommand::DrawIndexedCount(m_GhostMesh->GetIndexCount());

			// Restaura estado
			RenderCommand::SetDepthTest(true);
			RenderCommand::SetDepthWrite(true);
			RenderCommand::SetBlend(false);
			RenderCommand::SetCullFace(true);

			framebuffer.Unbind();
		}
		else
		{
			framebuffer.Unbind();
		}
	}
	std::uint32_t ViewportRenderer::PickObject(float mouseX, float mouseY)
	{
		std::uint32_t height = m_PickingRenderer.GetFramebufferHeight();
		std::uint32_t x = static_cast<std::uint32_t>(mouseX);
		std::uint32_t y = static_cast<std::uint32_t>(height - mouseY - 1);
		return m_PickingRenderer.ReadPixel(x, y);
	}

	void ViewportRenderer::ResizePicking(std::uint32_t width, std::uint32_t height)
	{
		m_PickingRenderer.Resize(width, height);
	}

	void ViewportRenderer::OnMouseRotate(const glm::vec2& delta) { if (m_Camera) m_Camera->Rotate(delta); }
	void ViewportRenderer::OnMousePan(const glm::vec2& delta) { if (m_Camera) m_Camera->Pan(delta); }
	void ViewportRenderer::OnMouseZoom(float delta) { if (m_Camera) m_Camera->Zoom(delta); }


	// ── DIRECAO DE UMA ENTIDADE-CAMERA ───────────────────────────────────────
	//
	// A MESMA conta que o SceneRuntime usa para posicionar a camera do Play:
	// yaw vem de Rotation.y, pitch de Rotation.x, os dois em radianos no
	// Transform e em graus na GameCamera.
	//
	// Passa pela `GameCamera::ForwardFromYawPitch` de proposito. Reescrever o
	// cos/sin aqui daria um frustum que aponta para um lado e um Play que
	// aponta para outro — e o desenho estaria mentindo exatamente sobre a
	// unica coisa que ele existe para mostrar.
	static glm::vec3 CameraEntityForward(const Transform& t)
	{
		return GameCamera::ForwardFromYawPitch(
			glm::degrees(t.Rotation.y), glm::degrees(t.Rotation.x));
	}

	entt::entity ViewportRenderer::PickPilotTarget() const
	{
		if (!m_Scene) return entt::null;

		auto& reg = m_Scene->GetRegistry();

		if (m_SelectedEntity && *m_SelectedEntity != entt::null &&
			reg.valid(*m_SelectedEntity) &&
			reg.all_of<CameraComponent>(*m_SelectedEntity) &&
			reg.all_of<TransformComponent>(*m_SelectedEntity))
		{
			return *m_SelectedEntity;
		}

		entt::entity first = entt::null;

		for (auto e : reg.view<CameraComponent, TransformComponent>())
		{
			if (first == entt::null) first = e;
			if (reg.get<CameraComponent>(e).IsPrimary) return e;
		}

		return first;
	}

	void ViewportRenderer::StopPilot()
	{
		if (m_PilotSaved && m_Camera)
		{
			m_Camera->SetOrbit(m_PilotSavedFocal, m_PilotSavedDistance,
				m_PilotSavedYaw, m_PilotSavedPitch);
			m_Camera->m_FovDegrees = m_PilotSavedFov;
		}

		m_PilotSaved = false;
		PilotCamera = entt::null;
	}

	bool ViewportRenderer::UpdatePilotCamera()
	{
		if (PilotCamera == entt::null) return false;

		if (!m_Scene || !m_Camera)
		{
			StopPilot();
			return false;
		}

		auto& reg = m_Scene->GetRegistry();

		// A entidade pode ter sido apagada, ou perdido o CameraComponent, entre
		// dois frames. Sair sozinho e melhor que pilotar um fantasma.
		if (!reg.valid(PilotCamera) ||
			!reg.all_of<CameraComponent>(PilotCamera) ||
			!reg.all_of<TransformComponent>(PilotCamera))
		{
			StopPilot();
			return false;
		}

		// Guarda a vista do usuario UMA vez, no frame em que o modo comeca.
		if (!m_PilotSaved)
		{
			m_PilotSavedFocal = m_Camera->GetFocalPoint();
			m_PilotSavedDistance = m_Camera->GetDistance();
			m_PilotSavedYaw = m_Camera->GetYaw();
			m_PilotSavedPitch = m_Camera->GetPitch();
			m_PilotSavedFov = m_Camera->m_FovDegrees;
			m_PilotSaved = true;
		}

		const auto& tc = reg.get<TransformComponent>(PilotCamera);
		const auto& cc = reg.get<CameraComponent>(PilotCamera);

		// TODO FRAME, e nao uma vez: e isto que faz arrastar o playhead do
		// Sequencer virar "assistir a cutscene". A camera segue o transform que
		// a sequence esta escrevendo.
		m_Camera->PointAt(tc.Data.Position, CameraEntityForward(tc.Data));

		// O FOV junto: um enquadramento visto com o FOV errado nao e o
		// enquadramento. E a diferenca entre conferir o plano e achar que
		// conferiu.
		m_Camera->m_FovDegrees = cc.Fov;

		return true;
	}

	// ── O DESENHO DA CAMERA ──────────────────────────────────────────────────
	//
	// Ver a nota em ShowCameras: ImDrawList por cima, e nao wireframe no GPU.
	// ═══════════════════════════════════════════════════════════════════════
	//  EDITOR DE CAMINHO
	// ═══════════════════════════════════════════════════════════════════════
	//
	// Ver a nota na declaracao. Como as formas do Control Rig e o frustum da
	// camera, tudo aqui e ImDrawList POR CIMA da cena: um ponto de controle
	// escondido atras de uma parede e um ponto que nao se consegue arrastar —
	// e um trilho de camera passa por dentro de cenario o tempo todo.
	bool ViewportRenderer::DrawSplineEditor(const glm::vec2& boundsMin,
		const glm::vec2& boundsMax)
	{
		if (!m_Scene || !m_Camera) return false;

		const float extW = boundsMax.x - boundsMin.x;
		const float extH = boundsMax.y - boundsMin.y;
		if (extW <= 0.0f || extH <= 0.0f) return false;

		auto& registry = m_Scene->GetRegistry();

		const entt::entity sel =
			(m_SelectedEntity && *m_SelectedEntity != entt::null &&
				registry.valid(*m_SelectedEntity)) ? *m_SelectedEntity : entt::null;

		// Trocar de entidade solta o ponto. Sem isto, selecionar outra curva
		// deixaria o gizmo no indice antigo — que pode nem existir na nova.
		if (sel != m_SplineOwner)
		{
			m_SplineOwner = sel;
			SelectedSplinePoint = -1;
		}

		ImDrawList* dl = ImGui::GetWindowDrawList();
		if (!dl) return false;

		const glm::mat4 viewProj =
			m_Camera->GetProjectionMatrix() * m_Camera->GetViewMatrix();

		// Mundo -> tela. Devolve false ATRAS da camera: sem esse teste o ponto
		// aparece espelhado do lado oposto da tela.
		auto project = [&](const glm::vec3& world, ImVec2& out) -> bool
			{
				const glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
				if (clip.w <= 0.0001f) return false;

				const glm::vec3 ndc = glm::vec3(clip) / clip.w;
				out = ImVec2(
					boundsMin.x + (ndc.x * 0.5f + 0.5f) * extW,
					boundsMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * extH);
				return true;
			};

		bool handledGizmo = false;

		auto view = registry.view<SplineComponent, TransformComponent>();

		for (auto e : view)
		{
			auto& sp = view.get<SplineComponent>(e);
			const bool isSel = (e == sel);

			if (!isSel && !sp.AlwaysVisible) continue;
			if (sp.Points.size() < 2 && !isSel) continue;

			const glm::mat4 world = m_Scene->GetWorldTransform(e);

			// ── A LINHA ──────────────────────────────────────────────────────
			//
			// Amostrada no PARAMETRO, e nao por distancia: para desenhar, o
			// espacamento desigual nao incomoda ninguem, e assim o desenho nao
			// depende da tabela de comprimento estar construida.
			if (sp.Points.size() >= 2)
			{
				const int segs = sp.Closed
					? static_cast<int>(sp.Points.size())
					: static_cast<int>(sp.Points.size()) - 1;

				const int steps = segs * 24;

				const ImU32 lineCol = isSel
					? IM_COL32(255, 190, 80, 235)
					: IM_COL32(150, 160, 180, 150);

				ImVec2 prev;
				bool havePrev = false;

				for (int i = 0; i <= steps; ++i)
				{
					const float t = static_cast<float>(i) / 24.0f;
					const glm::vec3 lp =
						SplinePath::EvaluateParam(sp.Points, sp.Closed, t);

					ImVec2 cur;
					if (!project(glm::vec3(world * glm::vec4(lp, 1.0f)), cur))
					{
						havePrev = false;
						continue;
					}

					if (havePrev)
						dl->AddLine(prev, cur, lineCol, isSel ? 2.4f : 1.6f);

					prev = cur;
					havePrev = true;
				}
			}

			if (!isSel) continue;

			// ── OS PONTOS DE CONTROLE ────────────────────────────────────────
			const ImVec2 mouse = ImGui::GetMousePos();
			const bool   clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

			int   hovered = -1;
			float hoveredDist = 1e9f;

			for (int i = 0; i < static_cast<int>(sp.Points.size()); ++i)
			{
				const glm::vec3 wp = glm::vec3(world * glm::vec4(sp.Points[i], 1.0f));

				ImVec2 s;
				if (!project(wp, s)) continue;

				const bool isPointSel = (i == SelectedSplinePoint);

				const float dx = mouse.x - s.x;
				const float dy = mouse.y - s.y;
				const float d = std::sqrt(dx * dx + dy * dy);

				if (d < 12.0f && d < hoveredDist)
				{
					hoveredDist = d;
					hovered = i;
				}

				const float r = isPointSel ? 7.0f : 5.0f;

				const ImU32 fill = isPointSel
					? IM_COL32(255, 235, 150, 255)
					: IM_COL32(40, 45, 55, 220);

				const ImU32 edge = isPointSel
					? IM_COL32(255, 255, 255, 255)
					: IM_COL32(255, 190, 80, 235);

				// Quadrado, e nao circulo: distingue o ponto de caminho da
				// forma de Control Rig, que e sempre redonda. Com as duas na
				// tela ao mesmo tempo, a diferenca de silhueta e o que evita
				// agarrar a coisa errada.
				dl->AddRectFilled(ImVec2(s.x - r, s.y - r), ImVec2(s.x + r, s.y + r), fill);
				dl->AddRect(ImVec2(s.x - r, s.y - r), ImVec2(s.x + r, s.y + r), edge,
					0.0f, 0, 2.0f);

				// O primeiro ponto ganha um anel: sem ele, "onde a curva
				// comeca" e invisivel — e o sentido de percurso e o que decide
				// se a camera anda para frente ou para tras.
				if (i == 0)
					dl->AddCircle(s, r + 4.0f, IM_COL32(120, 255, 140, 230), 12, 2.0f);
			}

			// Clique fora do gizmo escolhe um ponto. `IsUsing` evita roubar o
			// clique no meio de um arrasto do proprio gizmo.
			if (clicked && hovered >= 0 && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver())
			{
				SelectedSplinePoint = hovered;
				m_OverlayConsumedClick = true;
			}

			if (SelectedSplinePoint >= static_cast<int>(sp.Points.size()))
				SelectedSplinePoint = -1;

			// ── O GIZMO VAI PARA O PONTO ─────────────────────────────────────
			if (SelectedSplinePoint >= 0)
			{
				ImGuizmo::SetOrthographic(false);
				ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
				ImGuizmo::SetRect(boundsMin.x, boundsMin.y, extW, extH);

				glm::mat4 vw = m_Camera->GetViewMatrix();
				glm::mat4 pj = m_Camera->GetProjectionMatrix();

				// So TRANSLACAO: um ponto de controle nao tem rotacao nem
				// escala. Oferecer as outras operacoes daria um gizmo que gira
				// sem efeito nenhum — e o usuario passaria um tempo tentando
				// entender o que esta quebrado.
				glm::mat4 model = world *
					glm::translate(glm::mat4(1.0f), sp.Points[SelectedSplinePoint]);

				float snapValues[3] = { SnapValue, SnapValue, SnapValue };
				const float* snap = SnapEnabled ? snapValues : nullptr;

				ImGuizmo::Manipulate(
					glm::value_ptr(vw), glm::value_ptr(pj),
					ImGuizmo::TRANSLATE, ImGuizmo::WORLD,
					glm::value_ptr(model), nullptr, snap);

				if (ImGuizmo::IsUsing())
				{
					// Mundo -> LOCAL da entidade. Os pontos sao locais (ver
					// SplineComponent): gravar mundo faria o caminho escorregar
					// no instante em que alguem movesse a entidade.
					const glm::vec3 lp = glm::vec3(
						glm::inverse(world) * glm::vec4(glm::vec3(model[3]), 1.0f));

					sp.Points[SelectedSplinePoint] = lp;
					sp._Dirty = true;
				}

				handledGizmo = true;
			}
		}

		return handledGizmo;
	}

	void ViewportRenderer::DrawCameraGizmos(const glm::vec2& boundsMin,
		const glm::vec2& boundsMax)
	{
		if (!ShowCameras || !m_Scene || !m_Camera) return;

		const float w = boundsMax.x - boundsMin.x;
		const float h = boundsMax.y - boundsMin.y;
		if (w <= 0.0f || h <= 0.0f) return;

		auto& reg = m_Scene->GetRegistry();
		ImDrawList* dl = ImGui::GetWindowDrawList();

		const glm::mat4 vp = m_Camera->GetViewProjectionMatrix();

		for (auto e : reg.view<CameraComponent, TransformComponent>())
		{
			// Pilotando ESTA camera: desenhar o proprio frustum de dentro dele
			// encheria a tela de linhas na borda, sem informar nada.
			if (e == PilotCamera) continue;

			const auto& tc = reg.get<TransformComponent>(e);
			const auto& cc = reg.get<CameraComponent>(e);

			const glm::vec3 pos = tc.Data.Position;
			const glm::vec3 fwd = CameraEntityForward(tc.Data);

			// Base ortonormal a partir da frente. O "up do mundo" como
			// referencia e o mesmo que a GameCamera usa no lookAt — uma camera
			// de cutscene nao tem roll, e inventar um aqui faria o desenho
			// discordar do que o Play mostra.
			glm::vec3 right = glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f));

			// Olhando reto para cima ou para baixo, o cross degenera. Qualquer
			// eixo perpendicular serve, e +X e tao bom quanto outro.
			if (glm::dot(right, right) < 1e-6f)
				right = glm::vec3(1.0f, 0.0f, 0.0f);

			right = glm::normalize(right);
			const glm::vec3 up = glm::normalize(glm::cross(right, fwd));

			// Comprimento FIXO, e nao o FarClip: um frustum de 982 unidades
			// (o Far Clip padrao) cobriria o nivel inteiro de linhas. O que o
			// desenho precisa dizer e a DIRECAO e a ABERTURA, e para isso um
			// cone curto basta.
			const float len = 1.25f;
			const float aspect = (h > 0.0f) ? (w / h) : 1.6f;

			const float th = std::tan(glm::radians(cc.Fov * 0.5f)) * len;
			const float tw = th * aspect;

			const glm::vec3 c = pos + fwd * len;

			const glm::vec3 corners[4] = {
				c + up * th - right * tw,
				c + up * th + right * tw,
				c - up * th + right * tw,
				c - up * th - right * tw,
			};

			bool behind = false;

			auto project = [&](const glm::vec3& p, ImVec2& out) -> bool
				{
					const glm::vec4 clip = vp * glm::vec4(p, 1.0f);
					if (clip.w <= 0.0001f) { behind = true; return false; }

					const glm::vec3 ndc = glm::vec3(clip) / clip.w;

					out = ImVec2(
						boundsMin.x + (ndc.x * 0.5f + 0.5f) * w,
						boundsMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * h);
					return true;
				};

			ImVec2 sp, sc[4];
			if (!project(pos, sp)) continue;

			bool ok = true;
			for (int i = 0; i < 4; ++i)
				if (!project(corners[i], sc[i])) { ok = false; break; }

			if (!ok || behind) continue;

			const bool selected = (m_SelectedEntity && *m_SelectedEntity == e);

			const ImU32 col = selected ? IM_COL32(255, 210, 90, 255)
				: cc.IsPrimary ? IM_COL32(120, 200, 255, 220)
				: IM_COL32(150, 150, 160, 190);

			const float thick = selected ? 2.2f : 1.4f;

			// Os quatro raios e o retangulo da abertura.
			for (int i = 0; i < 4; ++i)
			{
				dl->AddLine(sp, sc[i], col, thick);
				dl->AddLine(sc[i], sc[(i + 1) % 4], col, thick);
			}

			// Corpo: um losango no ponto da camera, para ela existir na tela
			// mesmo vista de tras (com o frustum apontando para longe).
			dl->AddQuadFilled(
				ImVec2(sp.x, sp.y - 6.0f), ImVec2(sp.x + 6.0f, sp.y),
				ImVec2(sp.x, sp.y + 6.0f), ImVec2(sp.x - 6.0f, sp.y), col);

			// ── QUAL LADO E O DE CIMA ────────────────────────────────────────
			//
			// Sem isto, um frustum girado 180 graus no roll seria identico ao
			// nao girado — e "a imagem esta de cabeca para baixo" viraria um
			// misterio. O tracinho marca o topo do quadro.
			const ImVec2 topMid((sc[0].x + sc[1].x) * 0.5f, (sc[0].y + sc[1].y) * 0.5f);
			const ImVec2 upTip(
				topMid.x + (topMid.x - sp.x) * 0.16f,
				topMid.y + (topMid.y - sp.y) * 0.16f);

			dl->AddLine(sc[0], upTip, col, thick);
			dl->AddLine(sc[1], upTip, col, thick);

			if (const auto* nm = reg.try_get<NameComponent>(e))
				dl->AddText(ImVec2(sp.x + 9.0f, sp.y - 7.0f), col, nm->Name.c_str());
		}
	}

	void ViewportRenderer::DrawGuizmo(const glm::vec2& boundsMin, const glm::vec2& boundsMax)
	{
		const float extW = boundsMax.x - boundsMin.x;
		const float extH = boundsMax.y - boundsMin.y;

		// ── DESENHO DE FERRAMENTA EXTERNA, ANTES DE TUDO ─────────────────────
		//
		// Antes do gizmo porque o ImDrawList e uma pilha: o gizmo tem de ficar
		// POR CIMA das formas, senao um controle grande cobriria a seta que o
		// usuario esta tentando arrastar.
		//
		// A flag e zerada aqui e nao no fim: se o overlay for embora entre dois
		// frames, um "consumiu" esquecido bloquearia a selecao de entidade para
		// sempre, sem nada na tela que explicasse por que clicar parou de
		// funcionar.
		m_OverlayConsumedClick = false;

		// A flag e zerada ANTES da saida: um "consumiu" esquecido em Play
		// bloquearia a selecao de entidade quando voltasse ao Edit.
		if (SuppressEditorGizmos)
			return;

		// Antes de tudo: e informacao de cena, nao ferramenta. Fica atras do
		// gizmo e das formas do rig, que sao o que se agarra.
		DrawCameraGizmos(boundsMin, boundsMax);

		if (m_ExternalOverlay.Active && m_ExternalOverlay.OnDraw &&
			extW > 0.0f && extH > 0.0f)
		{
			m_OverlayConsumedClick = m_ExternalOverlay.OnDraw(boundsMin, boundsMax);
		}

		// ── GIZMO DE FERRAMENTA EXTERNA ──────────────────────────────────────
		//
		// Vem ANTES do caminho de entidade, e sai com return: dois gizmos na
		// tela disputariam o mesmo clique, e qual deles ganharia dependeria da
		// ordem de desenho — imprevisivel para quem esta usando.
		//
		// A selecao de entidade continua existindo enquanto isto esta ativo (o
		// outliner segue mostrando o personagem); o que muda e so QUEM o gizmo
		// manipula: o osso, e nao o objeto.
		if (m_ExternalGizmo.Active && m_Camera && extW > 0.0f && extH > 0.0f)
		{
			ImGuizmo::SetOrthographic(false);
			ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
			ImGuizmo::SetRect(boundsMin.x, boundsMin.y, extW, extH);

			glm::mat4 view = m_Camera->GetViewMatrix();
			glm::mat4 projection = m_Camera->GetProjectionMatrix();
			glm::mat4 model = m_ExternalGizmo.World;

			float snapValues[3] = { SnapValue, SnapValue, SnapValue };
			if (m_GuizmoOperation == ImGuizmo::ROTATE)
				snapValues[0] = snapValues[1] = snapValues[2] = SnapAngle;
			else if (m_GuizmoOperation == ImGuizmo::SCALE)
				snapValues[0] = snapValues[1] = snapValues[2] = SnapScale;

			const float* snap = SnapEnabled ? snapValues : nullptr;

			// O espaco vem da barra do viewport, como no caminho de entidade.
			//
			// O default continua sendo LOCAL, e por um motivo: girar um osso
			// nos eixos do MUNDO nao e o que o animador pensa — ele pensa
			// "dobra o cotovelo", que e o eixo do proprio osso. Mas alinhar uma
			// mao com o chao, ou empurrar um socket meio metro para tras, sao
			// pedidos em espaco de MUNDO, e forcar local ali obrigava a girar o
			// alvo ate os eixos coincidirem antes de poder mexer.
			//
			// Nada mais muda: o ApplyGizmoTo* recebe uma matriz de mundo nos
			// dois modos, e a conversao para o local do pai e identica.
			ImGuizmo::Manipulate(
				glm::value_ptr(view),
				glm::value_ptr(projection),
				m_GuizmoOperation,
				m_GuizmoMode,
				glm::value_ptr(model),
				nullptr,
				snap);

			if (ImGuizmo::IsUsing())
			{
				m_ExternalGizmoWasUsing = true;

				if (m_ExternalGizmo.OnManipulate)
					m_ExternalGizmo.OnManipulate(model);
			}
			else if (m_ExternalGizmoWasUsing)
			{
				m_ExternalGizmoWasUsing = false;

				if (m_ExternalGizmo.OnFinish)
					m_ExternalGizmo.OnFinish();
			}

			return;
		}

		// ── CAMINHOS ─────────────────────────────────────────────────────────
		//
		// Depois do gizmo externo (o Sequencer manda quando esta ativo) e antes
		// do caminho de entidade: com um ponto de controle escolhido, o gizmo
		// e dele.
		if (DrawSplineEditor(boundsMin, boundsMax))
			return;

		if (!m_Scene || !m_SelectedEntity || *m_SelectedEntity == entt::null || !m_Camera)
			return;

		auto& registry = m_Scene->GetRegistry();
		if (!registry.valid(*m_SelectedEntity))
			return;

		// Ignora gizmo em luzes
		if (registry.any_of<LightComponent>(*m_SelectedEntity))
			return;

		auto* tc = registry.try_get<TransformComponent>(*m_SelectedEntity);
		if (!tc) return;

		float width = boundsMax.x - boundsMin.x;
		float height = boundsMax.y - boundsMin.y;
		if (width <= 0.0f || height <= 0.0f) return;

		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
		ImGuizmo::SetRect(boundsMin.x, boundsMin.y, width, height);

		glm::mat4 view = m_Camera->GetViewMatrix();
		glm::mat4 projection = m_Camera->GetProjectionMatrix();

		// Usa o transform MUNDIAL no gizmo — necessário pra entities filhas
		// aparecerem no lugar certo no viewport.
		glm::mat4 model = m_Scene
			? m_Scene->GetWorldTransform(*m_SelectedEntity)
			: tc->Data.GetMatrix();

		// Snap
		float snapValues[3] = { SnapValue, SnapValue, SnapValue };
		if (m_GuizmoOperation == ImGuizmo::ROTATE)
			snapValues[0] = snapValues[1] = snapValues[2] = SnapAngle;
		else if (m_GuizmoOperation == ImGuizmo::SCALE)
			snapValues[0] = snapValues[1] = snapValues[2] = SnapScale;

		const float* snap = SnapEnabled ? snapValues : nullptr;

		ImGuizmo::Manipulate(
			glm::value_ptr(view),
			glm::value_ptr(projection),
			m_GuizmoOperation,
			m_GuizmoMode,
			glm::value_ptr(model),
			nullptr,
			snap
		);

		if (ImGuizmo::IsUsing())
		{
			// Salva transform antes da primeira modificação
			if (!m_GizmoWasUsing)
			{
				m_TransformSnapshot = tc->Data;
				m_GizmoWasUsing = true;
			}

			// Se a entity tem pai, converte world matrix → local space
			// dividindo pelo transform mundial do pai.
			glm::mat4 localModel = model;
			if (m_Scene)
			{
				auto& registry = m_Scene->GetRegistry();
				auto* rel = registry.try_get<RelationshipComponent>(*m_SelectedEntity);
				if (rel && rel->Parent != entt::null && registry.valid(rel->Parent))
				{
					glm::mat4 parentWorld = m_Scene->GetWorldTransform(rel->Parent);
					localModel = glm::inverse(parentWorld) * model;
				}
			}

			tc->Data.WorldMatrix = localModel;
			tc->Data.UseWorldMatrix = false; // usa Position/Rotation/Scale decompostos

			glm::vec3 position, rotation, scale;
			if (DecomposeTransform(localModel, position, rotation, scale))
			{
				tc->Data.Position = position;
				tc->Data.Rotation = rotation;
				tc->Data.Scale = scale;
			}
		}
		else if (m_GizmoWasUsing)
		{
			// Gizmo soltou — registra comando de undo
			m_GizmoWasUsing = false;

			if (m_CommandHistory)
			{
				Transform before = m_TransformSnapshot;
				Transform after = tc->Data;
				entt::entity ent = *m_SelectedEntity;
				auto* scene = m_Scene;

				m_CommandHistory->Push({
					"Mover objeto",
					[scene, ent, after]() {
						auto* t = scene->GetRegistry().try_get<TransformComponent>(ent);
						if (t) t->Data = after;
					},
					[scene, ent, before]() {
						auto* t = scene->GetRegistry().try_get<TransformComponent>(ent);
						if (t) t->Data = before;
					}
					});
			}
		}


	}

	void ViewportRenderer::DrawGrid()
	{
		if (!m_Camera) return;
		m_GridRenderer.Render(m_Camera->GetViewMatrix(), m_Camera->GetProjectionMatrix());
	}

	void ViewportRenderer::Resize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0) return;
		if (m_HDRFramebuffer)
			m_HDRFramebuffer->Resize(width, height);
		if (m_PostProcess && m_PostProcess->IsInitialized())
			m_PostProcess->Resize(width, height);
	}

} // namespace axe