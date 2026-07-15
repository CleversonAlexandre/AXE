// anim_graph_preview.cpp
//
// Preview 3D do AnimGraph: uma cena PROPRIA, com o personagem e uma luz,
// rodando o AnimationWorld com o grafo que voce esta editando.
//
// ── POR QUE UMA CENA PROPRIA ─────────────────────────────────────────────
//
// A alternativa seria espiar o personagem da cena principal. E tentador — ja
// esta ali. Mas ai o preview so funcionaria se o personagem ESTIVESSE na cena,
// e mexer nos parametros do preview mexeria no jogo.
//
// Com cena propria, voce edita um AnimGraph de um personagem que nem foi
// colocado na cena ainda. E o mesmo caminho que o Material e o Script Editor ja
// seguem aqui.

#include "anim_graph_window.hpp"

#include "axe/scene/components.hpp"
#include "axe/graphics/renderer/viewport_renderer.hpp"
#include "axe/renderer/scene_renderer.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/scene/scene_environment.hpp"
#include "axe/graphics/editor_camera.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/log/log.hpp"

#include <imgui.h>

namespace axe
{
	void AnimGraphWindow::InitPreviewScene()
	{
		FramebufferSpecification spec;
		spec.Width = 512;
		spec.Height = 512;
		spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::DEPTH32F };
		m_PreviewFramebuffer = Framebuffer::Create(spec);

		m_PreviewRenderer = std::make_unique<ViewportRenderer>();
		m_PreviewRenderer->Initialize();
		m_PreviewRenderer->SetPickingEnabled(false);
		m_PreviewRenderer->SetPreviewMode(true);

		// Deferred desligado no preview, como nas outras janelas: um G-Buffer
		// inteiro para desenhar um personagem num fundo vazio e desperdicio, e
		// o forward evita o custo de inicializar os alvos do deferred.
		if (auto* sr = m_PreviewRenderer->GetSceneRenderer())
		{
			sr->SetDeferredEnabled(false);
			sr->SetDeferredSupported(false);
		}

		m_PreviewRenderer->m_Camera = std::make_unique<EditorCamera>(45.0f, 1.0f, 0.1f, 1000.0f);
		m_PreviewRenderer->ShowGrid = true;
		m_PreviewRenderer->ShowColliders = false;

		m_PreviewScene = std::make_unique<Scene>();
		m_PreviewEntity = m_PreviewScene->CreateEntity("AnimPreview");

		auto& reg = m_PreviewScene->GetRegistry();

		auto light = m_PreviewScene->CreateEntity("PreviewLight");
		auto& lc = reg.emplace<LightComponent>(light);
		lc.Data = std::make_shared<DirectionalLight>();
		lc.Data->Direction = glm::vec3(0.3f, -1.0f, -0.6f);
		lc.Data->Color = glm::vec3(1.0f);
		lc.Data->Intensity = 3.0f;
		lc.Data->AmbientStrength = 0.35f;
		lc.Data->IBLIntensity = 0.2f;

		m_PreviewRenderer->SetScene(m_PreviewScene.get());

		m_PreviewEnvironment = std::make_unique<SceneEnvironment>();
		m_PreviewEnvironment->LoadHDRI("resources/quarry_04_puresky_2k.hdr");
		m_PreviewRenderer->SetEnvironment(m_PreviewEnvironment.get());

		if (auto* sr = m_PreviewRenderer->GetSceneRenderer())
			sr->SetEnvironment(m_PreviewEnvironment.get());

		// O AnimationWorld do PREVIEW. Proprio, separado do da cena — senao dar
		// play no preview mexeria no jogo.
		m_PreviewAnim = std::make_unique<AnimationWorld>();

		m_PreviewInit = true;
	}

	void AnimGraphWindow::SyncPreviewCharacter()
	{
		if (!m_PreviewScene || !m_Skeleton)
			return;

		// Ja esta sincronizado com este asset? Nao refaz — recriar o componente
		// resetaria o tempo da animacao a cada frame, e o personagem ficaria
		// congelado no frame 0.
		if (m_PreviewAssetInScene == m_Asset)
			return;

		auto& reg = m_PreviewScene->GetRegistry();

		// `get_or_emplace` nao existe nesta versao do EnTT (3.11). O idiomatico
		// e try_get + emplace — e e melhor mesmo: deixa explicito que o
		// componente pode nao existir ainda.
		auto* existing = reg.try_get<SkeletalMeshComponent>(m_PreviewEntity);
		auto& sk = existing ? *existing : reg.emplace<SkeletalMeshComponent>(m_PreviewEntity);

		sk.Asset = m_Skeleton;
		sk.Data = m_Skeleton->GetMesh();
		sk.Clips = m_Skeleton->GetClips();
		sk.CurrentClip = -1;          // quem manda e o grafo
		sk.ShowSkeleton = false;

		sk.GraphAsset = m_Asset;
		sk.GraphInstance.SetAsset(m_Asset);   // clona o grafo pra esta instancia

		m_PreviewAssetInScene = m_Asset;

		AXE_EDITOR_INFO("AnimGraph preview: personagem '{}' carregado ({} clipe(s)).",
			m_Skeleton->GetName(), m_Skeleton->GetClips().size());
	}

	void AnimGraphWindow::RenderPreview()
	{
		if (!m_Open || !m_Asset)
			return;

		if (!m_PreviewInit)
			InitPreviewScene();

		if (!m_PreviewRenderer || !m_PreviewFramebuffer || !m_PreviewScene)
			return;

		SyncPreviewCharacter();

		// ── O TICK DA ANIMACAO ───────────────────────────────────────────────
		//
		// `inPlay = false`: o preview anima no modo EDICAO. E o ponto inteiro —
		// voce mexe no grafo e ve o resultado sem ter que dar Play na cena.
		//
		// O grafo do preview e um CLONE (SetAsset clona), entao o tempo dele nao
		// interfere no personagem da cena principal.
		if (m_PreviewAnim && m_PreviewPlaying)
		{
			const float dt = ImGui::GetIO().DeltaTime;
			m_PreviewAnim->OnUpdate(*m_PreviewScene, dt, false);
		}

		if (auto* sr = m_PreviewRenderer->GetSceneRenderer())
		{
			sr->SetDeferredEnabled(false);
			sr->SetDeferredSupported(false);
			sr->SetEnvironment(m_PreviewEnvironment.get());
		}

		m_PreviewRenderer->SetEnvironment(m_PreviewEnvironment.get());

		const uint32_t w = (m_PreviewSize.x > 4.0f) ? (uint32_t)m_PreviewSize.x : 512u;
		const uint32_t h = (m_PreviewSize.y > 4.0f) ? (uint32_t)m_PreviewSize.y : 512u;

		auto& spec = m_PreviewFramebuffer->GetSpecification();

		if ((uint32_t)spec.Width != w || (uint32_t)spec.Height != h)
			m_PreviewFramebuffer->Resize(w, h);

		// RenderToFramebuffer, e nao "Render": e a API que o Material e o Script
		// preview ja usam. Escrevi de memoria e errei — agora esta copiada da
		// que comprovadamente funciona.
		m_PreviewRenderer->RenderToFramebuffer(*m_PreviewFramebuffer, w, h, 0.0f);
	}

	void AnimGraphWindow::DrawPreviewWindow()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

		if (ImGui::Begin("Preview##anim"))
		{
			// ── Barra de controle ────────────────────────────────────────────
			//
			// ACIMA da imagem, num child de altura fixa — nao sobreposta.
			//
			// Antes eu punha os botoes com SetCursorPos por cima do preview, e o
			// ImGui::Image desenhado depois os COBRIA. Eram os "dois botoes
			// impossiveis de ver": existiam, mas atras da imagem.
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
			ImGui::BeginChild("preview_bar", ImVec2(0, 32), false);

			if (ImGui::Button(m_PreviewPlaying ? "Pausar" : "Tocar"))
				m_PreviewPlaying = !m_PreviewPlaying;

			ImGui::SameLine();

			if (ImGui::Button("Reiniciar"))
			{
				if (m_PreviewScene && m_PreviewEntity != entt::null)
				{
					auto& reg = m_PreviewScene->GetRegistry();

					if (auto* sk = reg.try_get<SkeletalMeshComponent>(m_PreviewEntity))
						sk->GraphInstance.Reset();
				}
			}

			ImGui::SameLine();

			// O estado atual, ao vivo. Sem isto, montar uma maquina de estados e
			// adivinhacao: voce ve o personagem se mexer mas nao sabe em QUE
			// estado, nem se a transicao disparou.
			if (m_PreviewScene && m_PreviewEntity != entt::null)
			{
				auto& reg = m_PreviewScene->GetRegistry();

				if (auto* sk = reg.try_get<SkeletalMeshComponent>(m_PreviewEntity))
				{
					const std::string state = sk->GraphInstance.GetCurrentStateName();

					ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "  Estado: %s",
						state.empty() ? "(nenhum)" : state.c_str());
				}
			}

			ImGui::SameLine(0.0f, 20.0f);
			ImGui::TextDisabled("Alt + arrastar = camera");

			ImGui::EndChild();
			ImGui::PopStyleVar();

			// ── A imagem, ocupando o resto ───────────────────────────────────
			const ImVec2 avail = ImGui::GetContentRegionAvail();
			m_PreviewSize = avail;

			if (m_PreviewFramebuffer && avail.x > 4.0f && avail.y > 4.0f)
			{
				ImTextureID tid = (ImTextureID)(uintptr_t)
					m_PreviewFramebuffer->GetColorAttachmentRendererID();

				if (tid) ImGui::Image(tid, avail, ImVec2(0, 1), ImVec2(1, 0));
				else     ImGui::Dummy(avail);
			}
			else
			{
				ImGui::Dummy(avail);
			}

			// Hover DA IMAGEM, nao da janela toda: senao mexer na barra de
			// botoes ja arrastaria a camera.
			m_PreviewHovered = ImGui::IsItemHovered();

			HandlePreviewInput();
		}

		ImGui::End();
		ImGui::PopStyleVar();
	}

	void AnimGraphWindow::HandlePreviewInput()
	{
		if (!m_PreviewHovered || !m_PreviewRenderer)
			return;

		ImGuiIO& io = ImGui::GetIO();

		// ── EXATAMENTE o esquema do Material/Script preview ──────────────────
		//
		// O anterior era orbita-com-botao-direito e sensibilidade * 0.3 — 100x
		// mais sensivel que o resto do engine, e um esquema DIFERENTE das outras
		// janelas. Voce reclamou dos dois, com razao: um preview que se controla
		// diferente dos outros forca a reaprender o dedo a cada janela.
		//
		// Agora e identico: Alt + arrastar. Left orbita, Middle faz pan, Right
		// da zoom. E o * 0.003 e a mesma sensibilidade calma das outras.
		const ImVec2 mousePos = ImGui::GetMousePos();
		static ImVec2 lastMousePos = mousePos;

		const ImVec2 rawDelta(mousePos.x - lastMousePos.x, mousePos.y - lastMousePos.y);
		lastMousePos = mousePos;

		if (!io.KeyAlt)
			return;

		glm::vec2 delta(rawDelta.x, rawDelta.y);
		delta *= 0.003f;

		if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
			m_PreviewRenderer->OnMouseRotate(delta);
		else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
			m_PreviewRenderer->OnMousePan(delta);
		else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
			m_PreviewRenderer->OnMouseZoom(delta.y * 10.0f);

		if (io.MouseWheel != 0.0f)
			m_PreviewRenderer->OnMouseZoom(io.MouseWheel);
	}

} // namespace axe
