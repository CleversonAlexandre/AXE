// anim_clip_window.cpp — ANIMCLIP_EDITOR_V1
//
// Ver o header para o mapa geral. Decisoes que moram aqui:
//
//  - O preview usa o caminho MANUAL do AnimationWorld (CurrentClip +
//    AnimationPlayer), nao um AnimGraph: esta janela edita UM clipe por vez,
//    e o player manual e exatamente isso.
//
//  - A timeline e desenhada na mao (draw list): regua + playhead + losangos
//    de notify. O ImGui nao tem widget de timeline, e as libs de terceiros
//    trariam mais dependencia do que estas ~150 linhas.
//
//  - Notifies ficam ORDENADOS por tempo apos qualquer edicao — o disparo em
//    runtime (proxima etapa) varre "cruzou o intervalo [prev, agora]?" e isso
//    so e barato com a lista ordenada.

#include "axe_editor/ui/view_gizmo.hpp"   // VIEW_GIZMO_V1
#include "anim_clip_window.hpp"

#include "axe/scene/components.hpp"
#include "editor/axe_editor/viewport_renderer.hpp"
#include "axe/renderer/scene_renderer.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/editor_camera.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/log/log.hpp"

#include "editor/axe_editor/asset/asset_picker.hpp"
#include "axe_editor/ui/editor_icons.hpp"
#include "axe_editor/ui/editor_widgets.hpp"
#include <glm/gtx/quaternion.hpp>
#include "axe/animation/animation_sampler.hpp"
#include "axe/mesh/mesh_factory.hpp"
#include "axe/material/material.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/particles/particle_system_asset.hpp"
#include "axe/particles/particle_system_component.hpp"
#include "editor/axe_editor/asset_browser.hpp"

#include <imgui_internal.h>   // DockBuilder — layout padrao do dockspace, uma vez

#include <algorithm>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>
#include <ImGuizmo.h>
#include <cstdio>

namespace axe
{
	// ═════════════════════════════════════════════════════════════════════
	//  Ciclo de vida
	// ═════════════════════════════════════════════════════════════════════

	void AnimClipWindow::Initialize()
	{
		// Preview e criado sob demanda (InitPreviewScene) — abrir o editor
		// sem nunca usar esta janela nao deve custar um framebuffer.
	}

	void AnimClipWindow::OpenForAsset(const std::shared_ptr<SkeletalMeshAsset>& skeleton)
	{
		AXE_EDITOR_INFO("Animation Editor — ANIMCLIP_EDITOR_V6B (clip list refresh)");

		// Trocou de PERSONAGEM? A selecao de socket precisa cair junto (ver
		// abaixo). Reabrir o MESMO esqueleto — que e o que acontece ao dar
		// duplo-clique noutra animacao dele — preserva o socket em que se
		// estava trabalhando.
		const bool changedSkeleton = (m_Skeleton != skeleton);

		m_Skeleton = skeleton;
		m_Open = (skeleton != nullptr);
		m_Dirty = false;
		m_SelectedNotify = -1;
		m_DraggingNotify = -1;

		// Primeiro clipe ja selecionado: abrir num editor vazio e abrir
		// numa pergunta ("e agora?").
		m_SelectedClip = (m_Skeleton && !m_Skeleton->GetClips().empty()) ? 0 : -1;

		// SC40 — a selecao de socket e POR ESQUELETO.
		//
		// m_SelectedSocket e um indice em GetSockets(). Abrir outro
		// personagem sem zerar deixava o indice apontando para a lista
		// ANTIGA: com dois sockets no Y Bot e nenhum no personagem novo, o
		// painel lia fora do vetor.
		if (changedSkeleton)
		{
			m_SelectedSocket = -1;
			m_SocketPreviewLogged = -2;
		}

		SyncPreviewCharacter();

		// SC37/SC40 — primeira aparicao, ainda antes do primeiro
		// RenderPreview. O refresh CONTINUO mora no RenderPreview (ver a nota
		// la): esta chamada existe so para que a malha ja esteja na cena no
		// frame em que a janela aparece, em vez de piscar um frame vazia.
		UpdateSocketPreview();
	}

	void AnimClipWindow::SelectClipByName(const std::string& name)
	{
		if (!m_Skeleton || name.empty())
			return;

		const auto& clips = m_Skeleton->GetClips();

		for (std::size_t i = 0; i < clips.size(); ++i)
		{
			if (clips[i] && clips[i]->GetName() == name)
			{
				SelectClip((int)i);
				return;
			}
		}
	}

	std::shared_ptr<AnimationClip> AnimClipWindow::CurrentClip() const
	{
		if (!m_Skeleton)
			return nullptr;

		const auto& clips = m_Skeleton->GetClips();

		if (m_SelectedClip < 0 || m_SelectedClip >= (int)clips.size())
			return nullptr;

		return clips[m_SelectedClip];
	}

	void AnimClipWindow::MarkMetaEdited()
	{
		// SEM reordenar. A versao anterior fazia stable_sort por tempo aqui
		// e re-identificava a selecao por nome+tempo — com dois notifies
		// chamados "Notify", a re-identificacao acertava o OUTRO, e edicao/
		// exclusao caiam no pin errado ("o primeiro pin nao pode ser
		// alterado nem excluido"). Ordem de insercao = indices ESTAVEIS; o
		// disparo varre a lista linear de qualquer jeito.
		if (auto clip = CurrentClip())
			m_Skeleton->StoreClipMeta(clip);

		m_Dirty = true;
	}

	void AnimClipWindow::SelectClip(int index)
	{
		if (index == m_SelectedClip)
			return;

		m_SelectedClip = index;
		m_SelectedNotify = -1;
		m_DraggingNotify = -1;
		m_RecentFired.clear();

		// FX pendurados do clipe anterior morrem junto com ele.
		if (m_PreviewScene)
			for (const auto& fx : m_SpawnedFx)
				if (fx.Entity != entt::null && m_PreviewScene->GetRegistry().valid(fx.Entity))
					m_PreviewScene->DestroyEntity(fx.Entity);

		m_SpawnedFx.clear();

		// Troca o clipe do personagem do preview na hora.
		if (m_PreviewScene && m_PreviewEntity != entt::null)
		{
			auto& reg = m_PreviewScene->GetRegistry();

			if (auto* sk = reg.try_get<SkeletalMeshComponent>(m_PreviewEntity))
			{
				sk->CurrentClip = m_SelectedClip;
				sk->Player.SetTime(0.0f);
			}
		}
	}

	float AnimClipWindow::PreviewTime() const
	{
		if (m_PreviewScene && m_PreviewEntity != entt::null)
		{
			auto& reg = m_PreviewScene->GetRegistry();

			if (auto* sk = reg.try_get<SkeletalMeshComponent>(m_PreviewEntity))
				return sk->Player.GetTime();
		}

		return 0.0f;
	}

	void AnimClipWindow::SetPreviewTime(float t)
	{
		// Andar a agulha PRA FRENTE (scrub ou botao ">") DISPARA o que ela
		// atropela — como na Unreal: arrastar sobre o pin emite a particula.
		// Pra tras nao dispara: rebobinar nao e "acontecer de novo".
		if (auto clip = CurrentClip())
		{
			const float prevW = clip->WrapTime(m_LastPreviewTime);
			const float nowW = clip->WrapTime(t);

			if (nowW > prevW)
			{
				for (const auto& n : clip->Notifies)
				{
					if (n.Time > prevW && n.Time <= nowW)
					{
						m_RecentFired.push_back({ n.Name, ImGui::GetTime() });

						if (n.Type == AnimNotify::Kind::Particle && !n.Payload.empty())
							SpawnNotifyParticle(n);
					}
				}
			}
		}

		if (m_PreviewScene && m_PreviewEntity != entt::null)
		{
			auto& reg = m_PreviewScene->GetRegistry();

			if (auto* sk = reg.try_get<SkeletalMeshComponent>(m_PreviewEntity))
				sk->Player.SetTime(t);
		}

		m_LastPreviewTime = t;
	}

	// ═════════════════════════════════════════════════════════════════════
	//  Preview 3D — mesmo padrao do AnimGraph (cena propria)
	// ═════════════════════════════════════════════════════════════════════

	void AnimClipWindow::InitPreviewScene()
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

		if (auto* sr = m_PreviewRenderer->GetSceneRenderer())
		{
			sr->SetDeferredEnabled(false);
			sr->SetDeferredSupported(false);
		}

		m_PreviewRenderer->m_Camera = std::make_unique<EditorCamera>(45.0f, 1.0f, 0.1f, 1000.0f);
		m_PreviewRenderer->ShowGrid = true;
		m_PreviewRenderer->ShowColliders = false;

		m_PreviewScene = std::make_unique<Scene>();
		m_PreviewEntity = m_PreviewScene->CreateEntity("ClipPreview");

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

		m_PreviewAnim = std::make_unique<AnimationWorld>();

		// Som dos notifies em 2D nesta janela: a cena de preview e a camera
		// daqui nao alimentam o listener global, entao espacializar mediria a
		// distancia ate a camera do viewport PRINCIPAL — e o som sairia
		// baixo por um motivo que nao tem nada a ver com o que se esta
		// editando.
		m_PreviewAnim->SetSoundAudition(true);
		m_PreviewParticles = std::make_unique<ParticleWorld>();

		m_PreviewInit = true;
	}

	void AnimClipWindow::SyncPreviewCharacter()
	{
		if (!m_PreviewScene || !m_Skeleton)
			return;

		if (m_PreviewAssetInScene == m_Skeleton)
			return;

		auto& reg = m_PreviewScene->GetRegistry();

		auto* existing = reg.try_get<SkeletalMeshComponent>(m_PreviewEntity);
		auto& sk = existing ? *existing : reg.emplace<SkeletalMeshComponent>(m_PreviewEntity);

		sk.Asset = m_Skeleton;
		sk.Data = m_Skeleton->GetMesh();
		sk.Clips = m_Skeleton->GetClips();
		sk.CurrentClip = m_SelectedClip;
		sk.BlendTime = 0.05f;
		sk.PreviewInEditor = true;
		sk.ShowSkeleton = false;

		// Enquadramento identico ao do AnimGraph: Mixamo vem em cm.
		if (const auto& mesh = m_Skeleton->GetMesh())
		{
			const auto& verts = mesh->GetVertices();

			if (!verts.empty())
			{
				glm::vec3 mn = verts[0].Position;
				glm::vec3 mx = verts[0].Position;

				for (const auto& v : verts)
				{
					mn = glm::min(mn, v.Position);
					mx = glm::max(mx, v.Position);
				}

				const float height = mx.y - mn.y;

				if (height > 0.0001f)
				{
					constexpr float kTargetHeight = 1.8f;
					const float s = kTargetHeight / height;

					auto* tcp = reg.try_get<TransformComponent>(m_PreviewEntity);
					auto& tc = tcp ? *tcp : reg.emplace<TransformComponent>(m_PreviewEntity);
					tc.Data.Scale = glm::vec3(s);
					tc.Data.Position = glm::vec3(0.0f, -mn.y * s, 0.0f);

					if (m_PreviewRenderer && m_PreviewRenderer->m_Camera)
						m_PreviewRenderer->m_Camera->SetView(
							glm::vec3(0.0f, kTargetHeight * 0.5f, 0.0f),
							kTargetHeight * 1.9f);
				}
			}
		}

		m_PreviewAssetInScene = m_Skeleton;
	}

	void AnimClipWindow::RenderPreview()
	{
		if (!m_Open)
			return;

		if (!m_PreviewInit)
			InitPreviewScene();

		if (!m_PreviewRenderer || !m_PreviewFramebuffer || !m_PreviewScene)
			return;

		SyncPreviewCharacter();

		// Mantem o componente apontando pro clipe selecionado e o Playing
		// espelhando o botao — o AnimationWorld faz o resto.
		{
			auto& reg = m_PreviewScene->GetRegistry();

			if (auto* sk = reg.try_get<SkeletalMeshComponent>(m_PreviewEntity))
			{
				// A lista de clipes do esqueleto pode ter CRESCIDO depois que
				// o preview foi montado (importou 'Walking' com a janela
				// aberta) — a copia do componente ficava velha, o indice novo
				// caia fora do range e o preview continuava preso no clipe
				// antigo. Mesmo objeto de esqueleto (cache v6c), entao a
				// comparacao de vetores de shared_ptr e barata e certeira.
				const auto& srcClips = m_Skeleton->GetClips();

				if (sk->Clips != srcClips)
				{
					sk->Clips = srcClips;
					sk->_AppliedClip = -2;   // forca re-Play com os ponteiros novos
				}

				sk->CurrentClip = m_SelectedClip;
				sk->Player.Playing = m_Playing;
			}
		}

		const float prevTime = m_LastPreviewTime;

		if (m_PreviewAnim)
		{
			const float dt = ImGui::GetIO().DeltaTime;
			m_PreviewAnim->OnUpdate(*m_PreviewScene, dt, false);

			// Particulas dos notifies. allowDestroy=false (semantica de
			// Edit): a VIDA dos emissores e nossa — expiram abaixo.
			if (m_PreviewParticles)
				m_PreviewParticles->OnUpdate(*m_PreviewScene, dt, false,
					glm::vec3(0.0f, 1.2f, 2.5f));
		}

		// Expira FX de notify: alguns segundos de vida e somem — preview e
		// vitrine, nao cena.
		if (m_PreviewScene)
		{
			auto& fxreg = m_PreviewScene->GetRegistry();

			m_SpawnedFx.erase(
				std::remove_if(m_SpawnedFx.begin(), m_SpawnedFx.end(),
					[&](const SpawnedFx& fx)
					{
						if (ImGui::GetTime() - fx.At < 5.0)
							return false;

						if (fx.Entity != entt::null && fxreg.valid(fx.Entity))
							m_PreviewScene->DestroyEntity(fx.Entity);

						return true;
					}),
				m_SpawnedFx.end());
		}

		// ── Deteccao de notifies cruzados (feedback no editor) ───────────
		//
		// O disparo de VERDADE (EventBus/som/particula, no jogo) vem na
		// proxima etapa — mas ver o marcador acender enquanto o clipe toca
		// e o que torna a autoria confiavel: voce sabe que colocou o
		// FootStep no frame certo ANTES de escrever qualquer script.
		if (auto clip = CurrentClip(); clip && m_Playing)
		{
			// O tempo do player e CRU e cresce sem limite quando o clipe
			// loopa (48.4s num clipe de 6s) — o wrap so acontece na
			// amostragem. Comparar notify (2.17s) contra o tempo cru fazia
			// a particula emitir SO na primeira volta e nunca mais. Aqui
			// tudo vive em TEMPO DE CLIPE [0, Duration].
			const float now = PreviewTime();
			const float nowW = clip->WrapTime(now);
			const float prevW = clip->WrapTime(prevTime);

			auto crossed = [&](float t) -> bool
				{
					if (nowW >= prevW)
						return (t > prevW && t <= nowW);

					// deu a volta (loop): [prev, fim] U [0, now]
					return (t > prevW) || (t <= nowW);
				};

			for (const auto& n : clip->Notifies)
			{
				if (crossed(n.Time))
				{
					m_RecentFired.push_back({ n.Name, ImGui::GetTime() });
					AXE_EDITOR_INFO("Notify: '{}' ({:.2f}s, clipe '{}')",
						n.Name, n.Time, clip->GetName());

					// A timeline passou pelo losango: PARTICULA APARECE.
					if (n.Type == AnimNotify::Kind::Particle && !n.Payload.empty())
						SpawnNotifyParticle(n);
				}
			}

			m_LastPreviewTime = now;
		}

		// Expira o feedback visual.
		m_RecentFired.erase(
			std::remove_if(m_RecentFired.begin(), m_RecentFired.end(),
				[](const FiredNotify& f) { return ImGui::GetTime() - f.At > 1.2; }),
			m_RecentFired.end());

		// ── SC40: a malha do socket, TODO FRAME ──────────────────────────
		//
		// Ate aqui UpdateSocketPreview() so era chamado no OpenForAsset. O
		// efeito pratico: a entidade da malha nascia no instante em que a
		// janela abria e nunca mais era tocada. Anexar uma malha ao socket
		// com a janela ja aberta nao criava entidade nenhuma (a arma "nao
		// aparecia"), e mexer em Location/Rotation/Scale so mudava numeros —
		// a unica forma de ver o resultado era fechar e reabrir, que e o
		// unico caminho que passava por OpenForAsset outra vez.
		//
		// AQUI, e nao no DrawPreviewPanel: o painel roda na fase de UI, que
		// acontece DEPOIS do RenderToFramebuffer — o transform escrito la so
		// seria visto no frame seguinte. E DEPOIS do AnimationWorld::OnUpdate
		// acima, para que a matriz do osso amostrada seja a da MESMA pose que
		// este frame vai desenhar; antes dele, a arma ficaria um frame atras
		// da mao em toda animacao.
		UpdateSocketPreview();

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

		m_PreviewRenderer->RenderToFramebuffer(*m_PreviewFramebuffer, w, h, 0.0f);
	}

	void AnimClipWindow::SpawnNotifyParticle(const AnimNotify& n)
	{
		if (!m_PreviewScene)
			return;

		const AssetRecord* rec = AssetDatabase::Get().GetByUUID(n.Payload);

		if (!rec)
		{
			AXE_EDITOR_WARN("Notify '{}': asset de particula nao encontrado (UUID invalido?).", n.Name);
			return;
		}

		auto psAsset = ParticleSystemAsset::LoadFromFile(rec->FilePath);

		if (!psAsset)
		{
			AXE_EDITOR_WARN("Notify '{}': nao consegui carregar '{}'.", n.Name, rec->Name);
			return;
		}

		auto& reg = m_PreviewScene->GetRegistry();

		auto e = m_PreviewScene->CreateEntity("NotifyFX");

		// CreateEntity JA adiciona o TransformComponent — pegar, nao emplace
		// (emplace duplicado e assert do EnTT).
		auto& tc = reg.get<TransformComponent>(e);

		// ═══════════════════════════════════════════════════════════════════
		//  NOTIFY_SOCKET_V1 — a MESMA conta do runtime, chamada daqui
		//
		//  Este bloco dizia que a ancoragem no osso "entra junto com o disparo
		//  no runtime" e que aqui bastava o offset. Era essa diferenca: no
		//  editor a particula nascia no offset cru (que o autor posicionou
		//  olhando o cano), e no jogo nascia a partir da origem do personagem —
		//  nos pes. Duas convencoes para a mesma pergunta.
		//
		//  Agora as duas chamam AnimationWorld::ResolveSocketWorld. Se a conta
		//  mudar, muda nos dois ao mesmo tempo, porque e a mesma funcao.
		// ═══════════════════════════════════════════════════════════════════
		glm::mat4 socketWorld(1.0f);

		if (m_PreviewEntity != entt::null
			&& AnimationWorld::ResolveSocketWorld(*m_PreviewScene, m_PreviewEntity,
				n.Socket, socketWorld))
		{
			glm::mat3 basis(socketWorld);
			basis[0] = glm::normalize(basis[0]);
			basis[1] = glm::normalize(basis[1]);
			basis[2] = glm::normalize(basis[2]);

			const glm::quat sockRot = glm::quat_cast(basis);

			tc.Data.Position = glm::vec3(socketWorld[3]) + sockRot * n.LocationOffset;
			tc.Data.Rotation = glm::eulerAngles(
				sockRot * glm::quat(glm::radians(n.RotationOffset)));
		}
		else
		{
			// Sem socket escolhido: offset cru, como sempre foi aqui.
			tc.Data.Position = n.LocationOffset;
			tc.Data.Rotation = glm::radians(n.RotationOffset);
		}

		tc.Data.Scale = n.Scale;

		auto& ps = reg.emplace<ParticleSystemComponent>(e);
		ps.Data = psAsset;
		ps.ParticleAssetUUID = n.Payload;
		ps.Playing = true;
		ps.EmitterRuntimes.resize(psAsset->Emitters.size());

		m_SpawnedFx.push_back({ e, ImGui::GetTime() });
	}

	void AnimClipWindow::HandlePreviewInput()
	{
		if (!m_PreviewHovered || !m_PreviewRenderer)
			return;

		ImGuiIO& io = ImGui::GetIO();

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
	// ═════════════════════════════════════════════════════════════════════
	//  Janela — dockspace proprio, igual ao AnimGraph
	//
	//  Cada painel e uma JANELA de verdade hospedada num dockspace interno:
	//  arrastavel, redimensionavel, empilhavel em abas. O BeginChild de
	//  tamanho fixo da v1 nao fazia nada disso.
	// ═════════════════════════════════════════════════════════════════════

	void AnimClipWindow::Draw()
	{
		if (!m_Open || !m_Skeleton)
			return;

		// Hifen ASCII de proposito: o em-dash nao existe na fonte padrao do
		// ImGui e virava "?" na barra de titulo.
		char title[160];
		std::snprintf(title, sizeof(title), "Animation - %s%s###AnimClipEditor",
			m_Skeleton->GetName().c_str(), m_Dirty ? " *" : "");

		ImGui::SetNextWindowSize(ImVec2(1280, 760), ImGuiCond_FirstUseEver);

		if (!ImGui::Begin(title, &m_Open, ImGuiWindowFlags_NoCollapse))
		{
			ImGui::End();
			return;
		}

		DrawToolbar();
		ImGui::Separator();

		const ImGuiID dockId = ImGui::GetID("AnimClipDock");

		DrawDockLayout(dockId);

		ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_None);

		ImGui::End();

		// Paineis submetidos FORA do Begin/End da janela-mae — e como o
		// docking do ImGui funciona: sao janelas de topo que o dockspace
		// hospeda. Se a janela-mae esta oculta (aba nao selecionada), o
		// early-return acima ja impediu de chegarmos aqui.
		ImGui::Begin("Clips##animclip");
		DrawClipList();
		ImGui::End();

		ImGui::Begin("Skeleton##animclip");
		DrawSkeletonTree();
		ImGui::End();

		ImGui::Begin("Viewport##animclip");
		DrawPreviewPanel();
		ImGui::End();

		ImGui::Begin("Timeline##animclip");
		DrawTimeline();
		ImGui::End();

		ImGui::Begin("Details##animclip");
		DrawRightPanel();
		ImGui::End();
	}

	void AnimClipWindow::DrawDockLayout(unsigned int dockspaceId)
	{
		// So constroi o layout padrao quando o dockspace NUNCA existiu —
		// depois disso o arranjo e do usuario (o ImGui persiste no
		// imgui.ini). Mesma licao aprendida no AnimGraph: flag de membro
		// nao serve, porque o ImGui recria o dockspace a cada reabertura.
		if (ImGui::DockBuilderGetNode(dockspaceId) != nullptr)
			return;

		ImGui::DockBuilderRemoveNode(dockspaceId);
		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetContentRegionAvail());

		ImGuiID center = dockspaceId;

		const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.16f, nullptr, &center);
		const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, nullptr, &center);
		const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.26f, nullptr, &center);

		// Clips e Skeleton empilhados em abas no mesmo no da esquerda.
		ImGui::DockBuilderDockWindow("Clips##animclip", left);
		ImGui::DockBuilderDockWindow("Skeleton##animclip", left);
		ImGui::DockBuilderDockWindow("Viewport##animclip", center);
		ImGui::DockBuilderDockWindow("Timeline##animclip", bottom);
		ImGui::DockBuilderDockWindow("Details##animclip", right);

		ImGui::DockBuilderFinish(dockspaceId);
	}

	void AnimClipWindow::DrawToolbar()
	{
		// Primary: e a acao principal desta janela, e o asterisco no titulo ja
		// avisa quando ha o que salvar.
		if (ui::AccentButton(ICON_SAVE "  Salvar", ui::Accent::Primary,
			"Grava o .axeskel (sockets, clipes e notifies)"))
		{
			// O meta ja foi copiado pro asset a cada edicao (MarkMetaEdited);
			// aqui e so persistir o .axeskel.
			if (m_Skeleton->Save())
			{
				m_Dirty = false;
				AXE_EDITOR_INFO("Animation Editor: '{}' salvo.", m_Skeleton->GetName());
			}
		}

		ImGui::SameLine();
		ImGui::TextDisabled("|  double-click / right-click a lane = add notify  |  drag diamond = move / change track  |  Alt+drag in viewport = camera  |  T / R / S = move / rotate / scale socket");
	}

	// ─────────────────────────────────────────────────────────────────────────
	//  SC36 — a malha do socket no viewport
	//
	//  Sem isto o socket so tinha numeros: voce digitava Location/Rotation e
	//  torcia. Posicionar uma arma na mao e um trabalho VISUAL — o valor certo
	//  e "aquele em que o cabo encosta na palma", e nao um numero que se
	//  calcule.
	//
	//  A malha e uma SEGUNDA entidade no preview, com MeshComponent comum, cujo
	//  transform e reescrito por frame. Nao e filha da entidade do personagem
	//  nem parte do skinning: o osso ja da a matriz pronta, e enfiar isto no
	//  pipeline skinado significaria criar um bone falso so para autoria.
	// ─────────────────────────────────────────────────────────────────────────
	void AnimClipWindow::UpdateSocketPreview()
	{
		if (!m_PreviewScene) return;

		auto& reg = m_PreviewScene->GetRegistry();

		// Sem esqueleto aberto nao ha o que posicionar. Sai cedo em vez de
		// montar um vector vazio so para o ternario compilar — e o temporario
		// ainda nao poderia ser ligado a uma referencia const com seguranca de
		// tempo de vida ao longo da funcao.
		if (!m_Skeleton) return;

		const auto& sockets = m_Skeleton->GetSockets();

		const bool haveSel = (m_SelectedSocket >= 0 && m_SelectedSocket < (int)sockets.size());
		const std::string meshUUID = haveSel ? sockets[m_SelectedSocket].PreviewMeshUUID : "";

		// Sem socket selecionado, ou sem malha nele: a entidade some.
		//
		// Some de verdade (destroy), e nao "fica invisivel": manter uma
		// entidade vazia na cena de preview a faria aparecer em qualquer
		// varredura futura do registry como um objeto sem malha e sem dono.
		if (!haveSel || meshUUID.empty())
		{
			if (m_SocketPreviewEntity != entt::null && reg.valid(m_SocketPreviewEntity))
				m_PreviewScene->DestroyEntity(m_SocketPreviewEntity);

			m_SocketPreviewEntity = entt::null;
			return;
		}

		if (m_SocketPreviewEntity == entt::null || !reg.valid(m_SocketPreviewEntity))
			m_SocketPreviewEntity = m_PreviewScene->CreateEntity("SocketPreview");

		auto& mc = reg.get_or_emplace<MeshComponent>(m_SocketPreviewEntity);

		// Material explicito: o SceneRenderer aceita material nulo, mas o
		// caminho deferido nao desenha sem ele. Um cinza neutro basta — a
		// malha do socket serve para POSICIONAR, nao para avaliar aparencia.
		if (!reg.all_of<MaterialComponent>(m_SocketPreviewEntity))
		{
			auto m = std::make_shared<Material>(nullptr, "SocketPreview");
			m->UsePBR = true;
			m->Metallic = 0.0f;
			m->Roughness = 0.55f;
			m->Color = glm::vec4(0.85f, 0.55f, 0.15f, 1.0f);   // laranja: e uma
			// ajuda de autoria,
			// nao parte do modelo
			reg.emplace<MaterialComponent>(m_SocketPreviewEntity, m);
		}

		// Recarrega so quando o UUID muda: ResolveByUUID le o arquivo do disco,
		// e faze-lo por frame transformaria o preview num leitor de FBX.
		if (mc.AssetUUID != meshUUID)
		{
			mc.AssetUUID = meshUUID;
			mc.Data = MeshFactory::ResolveByUUID(meshUUID);

			// Uma linha por TROCA de malha, nao por frame. Sem isto, "nao
			// aparece nada" nao distingue malha-nao-carregada de
			// malha-carregada-fora-de-vista, e as duas causas nao tem nada em
			// comum.
			if (mc.Data)
				AXE_EDITOR_INFO("Socket preview: malha '{}' carregada ({} vertices).",
					meshUUID, (int)mc.Data->GetVertices().size());
			else
				AXE_EDITOR_ERROR("Socket preview: nao foi possivel resolver a malha '{}'.",
					meshUUID);
		}

		if (!mc.Data) return;

		// ── Matriz do osso na pose CORRENTE ──────────────────────────────────
		const auto& skel = m_Skeleton->GetSkeleton();
		if (!skel) return;

		const auto& sock = sockets[m_SelectedSocket];

		int boneIdx = -1;
		const auto& bones = skel->GetBones();
		for (int i = 0; i < (int)bones.size(); i++)
			if (bones[i].Name == sock.BoneName) { boneIdx = i; break; }

		if (boneIdx < 0) return;   // osso renomeado no re-import — nao adivinha

		std::vector<glm::mat4> skinning, globals;

		// Amostra a MESMA pose que o viewport esta mostrando. Reamostrar aqui,
		// em vez de reaproveitar o que o renderer calculou, custa uma passada
		// no esqueleto por frame — e evita depender de o componente expor um
		// cache interno que hoje ele nao expoe.
		if (auto clip = CurrentClip())
			AnimationSampler::Sample(*skel, *clip, PreviewTime(), skinning, &globals);
		else
			AnimationSampler::BindPose(*skel, skinning, &globals);

		if (boneIdx >= (int)globals.size()) return;

		// Transform do PERSONAGEM: o preview pode aplicar escala/offset na
		// entidade, e ignorar isso poria a arma em outra escala que a mao.
		glm::mat4 charXf(1.0f);
		if (auto* tc = reg.try_get<TransformComponent>(m_PreviewEntity))
			charXf = tc->Data.GetMatrix();

		const glm::mat4 world = charXf * globals[boneIdx]
			* m_Skeleton->GetSocketLocalTransform(sock);

		// Decompoe para o TransformComponent porque e o que o renderer le. A
		// escala sai do comprimento das colunas; a rotacao, da base
		// normalizada — decomposicao suficiente aqui porque um socket nao tem
		// shear (translate*rotate*scale, sempre).
		auto& tc = reg.get_or_emplace<TransformComponent>(m_SocketPreviewEntity);

		glm::vec3 sc(
			glm::length(glm::vec3(world[0])),
			glm::length(glm::vec3(world[1])),
			glm::length(glm::vec3(world[2])));

		glm::mat3 rot(
			glm::vec3(world[0]) / (sc.x > 0.0f ? sc.x : 1.0f),
			glm::vec3(world[1]) / (sc.y > 0.0f ? sc.y : 1.0f),
			glm::vec3(world[2]) / (sc.z > 0.0f ? sc.z : 1.0f));

		tc.Data.Position = glm::vec3(world[3]);
		tc.Data.Rotation = glm::eulerAngles(glm::quat_cast(rot));
		tc.Data.Scale = sc;

		// Diagnostico de POSICAO, uma vez por troca de socket.
		//
		// O caso que este log resolve: a malha carrega, a entidade existe, e a
		// pistola esta a 300 unidades da camera ou com escala 0.001 — invisivel
		// pelos mesmos sintomas de "nao renderizou". Ver os numeros separa as
		// duas hipoteses num olhar.
		if (m_SocketPreviewLogged != m_SelectedSocket)
		{
			m_SocketPreviewLogged = m_SelectedSocket;
			AXE_EDITOR_INFO("Socket '{}': pos ({:.3f}, {:.3f}, {:.3f})  escala ({:.3f}, {:.3f}, {:.3f}).",
				sock.Name,
				tc.Data.Position.x, tc.Data.Position.y, tc.Data.Position.z,
				tc.Data.Scale.x, tc.Data.Scale.y, tc.Data.Scale.z);
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	//  SC39 — gizmo para posicionar o socket
	//
	//  O gizmo opera no espaco do MUNDO (e o unico em que o ImGuizmo sabe
	//  desenhar), mas o socket guarda um transform RELATIVO ao osso. Entao o
	//  resultado do arrasto e trazido de volta multiplicando pelo inverso da
	//  matriz do osso — sem isso, mover o socket com o personagem em qualquer
	//  pose que nao a bind gravaria um offset que so vale naquela pose.
	//
	//  Atalhos T/R/S, os mesmos do viewport principal: um editor onde cada
	//  janela tem a sua tecla obriga a lembrar de qual janela se esta.
	// ─────────────────────────────────────────────────────────────────────────
	void AnimClipWindow::DrawSocketGizmo()
	{
		if (!m_Skeleton || !m_PreviewRenderer || !m_PreviewRenderer->m_Camera) return;

		auto& sockets = m_Skeleton->GetSockets();
		if (m_SelectedSocket < 0 || m_SelectedSocket >= (int)sockets.size()) return;

		auto& sock = sockets[m_SelectedSocket];

		const auto& skel = m_Skeleton->GetSkeleton();
		if (!skel) return;

		int boneIdx = -1;
		const auto& bones = skel->GetBones();
		for (int i = 0; i < (int)bones.size(); i++)
			if (bones[i].Name == sock.BoneName) { boneIdx = i; break; }

		if (boneIdx < 0) return;

		// SC42 — T/R/S, as MESMAS teclas do viewport principal
		// (editor_layer.cpp, bloco de viewport->IsFocused).
		//
		// Estavam em W/E/R, que e o padrao da Unreal — mas o padrao que vale
		// aqui e o da AXE, e o viewport ja tinha escolhido T/R/S ha muito
		// tempo. Duas convencoes no mesmo editor custam mais do que qualquer
		// uma delas isolada: a mao erra na janela em que se esta menos, e o
		// erro e silencioso (voce arrasta escala achando que e translacao).
		//
		// Teclas so quando o mouse esta sobre o preview: um S digitado num
		// campo de texto do painel ao lado nao pode trocar o modo do gizmo.
		if (m_PreviewHovered && !ImGui::IsAnyItemActive())
		{
			if (ImGui::IsKeyPressed(ImGuiKey_T)) m_SocketGizmoOp = ImGuizmo::TRANSLATE;
			if (ImGui::IsKeyPressed(ImGuiKey_R)) m_SocketGizmoOp = ImGuizmo::ROTATE;
			if (ImGui::IsKeyPressed(ImGuiKey_S)) m_SocketGizmoOp = ImGuizmo::SCALE;
		}

		std::vector<glm::mat4> skinning, globals;

		if (auto clip = CurrentClip())
			AnimationSampler::Sample(*skel, *clip, PreviewTime(), skinning, &globals);
		else
			AnimationSampler::BindPose(*skel, skinning, &globals);

		if (boneIdx >= (int)globals.size()) return;

		glm::mat4 charXf(1.0f);
		if (auto* tc = m_PreviewScene->GetRegistry().try_get<TransformComponent>(m_PreviewEntity))
			charXf = tc->Data.GetMatrix();

		const glm::mat4 boneWorld = charXf * globals[boneIdx];
		glm::mat4 world = boneWorld * m_Skeleton->GetSocketLocalTransform(sock);

		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist();

		const ImVec2 rmin = ImGui::GetItemRectMin();
		const ImVec2 rsz = ImGui::GetItemRectSize();
		ImGuizmo::SetRect(rmin.x, rmin.y, rsz.x, rsz.y);

		glm::mat4 view = m_PreviewRenderer->m_Camera->GetViewMatrix();
		glm::mat4 proj = m_PreviewRenderer->m_Camera->GetProjectionMatrix();

		if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
			(ImGuizmo::OPERATION)m_SocketGizmoOp, ImGuizmo::LOCAL,
			glm::value_ptr(world)))
		{
			// De volta para o espaco do osso.
			const glm::mat4 local = glm::inverse(boneWorld) * world;

			glm::vec3 t, sc;
			glm::vec3 rotDeg;
			ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(local),
				glm::value_ptr(t), glm::value_ptr(rotDeg), glm::value_ptr(sc));

			sock.Location = t;
			sock.Rotation = rotDeg;   // o campo ja e em graus, como o ImGuizmo
			sock.Scale = sc;

			m_SocketGizmoDirty = true;
		}

		// Grava so quando o arrasto TERMINA. Um Save por frame de arrasto
		// reescreveria o .axeskel dezenas de vezes por segundo — e o disco nao
		// e onde se guarda estado intermediario de um gesto.
		if (m_SocketGizmoDirty && !ImGuizmo::IsUsing())
		{
			m_Skeleton->Save();
			m_SocketGizmoDirty = false;
		}
	}

	void AnimClipWindow::DrawPreviewPanel()
	{
		const ImVec2 pavail = ImGui::GetContentRegionAvail();
		m_PreviewSize = pavail;

		if (m_PreviewFramebuffer && pavail.x > 4.0f && pavail.y > 4.0f)
		{
			ImTextureID tid = (ImTextureID)(uintptr_t)
				m_PreviewFramebuffer->GetColorAttachmentRendererID();

			if (tid) ImGui::Image(tid, pavail, ImVec2(0, 1), ImVec2(1, 0));
			else     ImGui::Dummy(pavail);

			// VIEW_GIZMO_V1 — mesmo widget do viewport. Os cantos vem do
			// GetItemRect logo apos a Image: e o retangulo REAL dela, e nao o
			// da janela, que difere quando ha barra de ferramentas ou aba.
			// showTools=false: preview e pequeno, e os botoes de dolly/pan
			// comeriam area util — o arrasto e o clique nos eixos bastam.
			if (m_PreviewRenderer && m_PreviewRenderer->m_Camera)
				ui::DrawViewGizmo(*m_PreviewRenderer->m_Camera,
					ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), false);
		}
		else
		{
			ImGui::Dummy(pavail);
		}

		m_PreviewHovered = ImGui::IsItemHovered();

		// ── SC39: gizmo do socket ────────────────────────────────────────────
		//
		//  Antes de HandlePreviewInput: quando o gizmo esta sendo arrastado, o
		//  input de camera precisa ficar quieto, senao o mesmo arrasto move a
		//  peca E orbita a cena.
		DrawSocketGizmo();

		HandlePreviewInput();

		// Notifies recem-cruzados, por cima do canto do preview.
		if (!m_RecentFired.empty())
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec2 base = ImGui::GetItemRectMin();

			float y = base.y + 8.0f;

			for (const auto& f : m_RecentFired)
			{
				const float age = (float)(ImGui::GetTime() - f.At);
				const float a = 1.0f - (age / 1.2f);

				dl->AddText(ImVec2(base.x + 10.0f, y),
					ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.85f, 0.3f, a)),
					f.Name.c_str());

				y += ImGui::GetTextLineHeight() + 2.0f;
			}
		}
	}



	void AnimClipWindow::DrawClipList()
	{
		// So os clipes DESTE esqueleto — o "asset browser compativel" da
		// Unreal. Nao ha o que filtrar: o .axeskel e a fronteira natural.
		const auto& clips = m_Skeleton->GetClips();

		if (clips.empty())
		{
			ImGui::TextDisabled("No clips.");
			ImGui::TextWrapped("Import via the character's Inspector, or drag an FBX into the anim graph.");
			return;
		}

		for (std::size_t i = 0; i < clips.size(); ++i)
		{
			if (!clips[i])
				continue;

			ImGui::PushID((int)i);

			char label[128];
			std::snprintf(label, sizeof(label), "%s  (%.2fs)",
				clips[i]->GetName().c_str(), clips[i]->GetDuration());

			if (ImGui::Selectable(label, m_SelectedClip == (int)i))
				SelectClip((int)i);

			// Contagem de notifies como badge — de relance voce sabe quais
			// clipes ja foram trabalhados.
			if (!clips[i]->Notifies.empty())
			{
				ImGui::SameLine();
				ImGui::TextDisabled("[%d]", (int)clips[i]->Notifies.size());
			}

			ImGui::PopID();
		}
	}

	void AnimClipWindow::DrawSkeletonTree()
	{
		const auto& skel = m_Skeleton->GetSkeleton();

		if (!skel)
		{
			ImGui::TextDisabled("Skeleton not resolved.");
			return;
		}

		const auto& bones = skel->GetBones();

		ImGui::TextDisabled("%d bones  |  %d socket(s)",
			(int)bones.size(), (int)m_Skeleton->GetSockets().size());
		ImGui::Separator();

		// Filhos por indice — a ordem topologica do Skeleton (pai sempre
		// antes do filho) torna isto um passe unico.
		std::vector<std::vector<int>> children(bones.size());

		for (std::size_t i = 0; i < bones.size(); ++i)
			if (bones[i].ParentIndex >= 0)
				children[bones[i].ParentIndex].push_back((int)i);

		// Recursao com lambda explicita (std::function evitado: profundidade
		// de esqueleto e pequena, mas a chamada e por frame).
		struct Walker
		{
			const std::vector<Bone>* Bones;
			const std::vector<std::vector<int>>* Children;
			std::string* Selected;   // SC35 — osso clicado

			void Walk(int idx) const
			{
				const auto& kids = (*Children)[idx];
				const std::string& name = (*Bones)[idx].Name;

				ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth
					| ImGuiTreeNodeFlags_OpenOnArrow;   // SC35: clicar no nome
				// seleciona em vez de
				// abrir/fechar o galho

				if (kids.empty())
					flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
				else if (idx == 0)
					flags |= ImGuiTreeNodeFlags_DefaultOpen;

				if (Selected && *Selected == name)
					flags |= ImGuiTreeNodeFlags_Selected;

				const bool open = ImGui::TreeNodeEx(name.c_str(), flags);

				if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && Selected)
					*Selected = name;

				if (open && !kids.empty())
				{
					for (int c : kids)
						Walk(c);

					ImGui::TreePop();
				}
			}
		};

		// A arvore fica num filho de altura fixa para que a lista de sockets
		// tenha lugar garantido: com 60+ ossos ela empurraria os sockets para
		// fora da janela, e a secao existiria sem nunca ser vista.
		ImGui::BeginChild("##bonetree", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.5f), true);

		Walker w{ &bones, &children, &m_SelectedBone };

		for (std::size_t i = 0; i < bones.size(); ++i)
			if (bones[i].ParentIndex < 0)
				w.Walk((int)i);

		ImGui::EndChild();

		DrawSocketList();
	}

	// ─────────────────────────────────────────────────────────────────────────
	//  SC35 — sockets: lista e edicao
	// ─────────────────────────────────────────────────────────────────────────

	// ═══════════════════════════════════════════════════════════════════════
	//  SOCKET_UNDO_V1 — desfazer/refazer nos sockets
	//
	//  ── POR QUE UM PAR DE FUNCOES E NAO UM Push DIRETO ────────────────────
	//
	//  Um DragFloat3 dispara a cada frame de arrasto. Empilhar um comando por
	//  frame encheria o historico com centenas de passos de um milimetro, e
	//  desfazer viraria inutil: seriam 300 Ctrl+Z para voltar um gesto.
	//
	//  Begin captura o "antes" quando o gesto COMECA; End compara e empilha UM
	//  comando quando ele termina. E o mesmo par de estados (dirty/commit) que
	//  o painel ja usava para decidir quando gravar — undo entra de carona no
	//  ritmo que ja existia.
	// ═══════════════════════════════════════════════════════════════════════
	void AnimClipWindow::BeginSocketEdit()
	{
		if (m_SocketEditOpen || !m_Skeleton) return;

		auto& sockets = m_Skeleton->GetSockets();
		if (m_SelectedSocket < 0 || m_SelectedSocket >= (int)sockets.size()) return;

		m_SocketBeforeEdit = sockets[m_SelectedSocket];
		m_SocketEditOpen = true;
	}

	void AnimClipWindow::EndSocketEdit(const char* actionName)
	{
		if (!m_SocketEditOpen || !m_Skeleton) { m_SocketEditOpen = false; return; }
		m_SocketEditOpen = false;

		auto& sockets = m_Skeleton->GetSockets();
		if (m_SelectedSocket < 0 || m_SelectedSocket >= (int)sockets.size()) return;

		const int index = m_SelectedSocket;
		const SkeletalMeshAsset::Socket before = m_SocketBeforeEdit;
		const SkeletalMeshAsset::Socket after = sockets[index];

		// Gesto que terminou onde comecou nao e uma acao. Sem esta guarda, um
		// clique sem arrasto entraria no historico e o primeiro Ctrl+Z nao
		// faria nada visivel — que e como se perde a confianca no undo.
		if (before.Name == after.Name
			&& before.BoneName == after.BoneName
			&& before.Location == after.Location
			&& before.Rotation == after.Rotation
			&& before.Scale == after.Scale
			&& before.PreviewMeshUUID == after.PreviewMeshUUID)
			return;

		auto apply = [this, index](const SkeletalMeshAsset::Socket& value)
			{
				if (!m_Skeleton) return;
				auto& list = m_Skeleton->GetSockets();
				if (index < 0 || index >= (int)list.size()) return;
				list[index] = value;
				m_SelectedSocket = index;
				m_Skeleton->Save();
			};

		Command cmd;
		cmd.Name = actionName;
		cmd.Execute = [apply, after]() { apply(after); };
		cmd.Undo = [apply, before]() { apply(before); };

		// O valor novo JA esta no asset — o painel escreveu nele. Push
		// executaria de novo, o que aqui e inofensivo e mantem o Redo correto.
		m_SocketHistory.Push(std::move(cmd));
	}

	void AnimClipWindow::DrawSocketList()
	{
		auto& sockets = m_Skeleton->GetSockets();

		ImGui::Spacing();
		ui::SectionHeader(ICON_LINK, "Sockets", ui::Accent::Neutral);

		// Add usa o osso SELECIONADO na arvore acima. Sem osso selecionado o
		// botao fica desabilitado com a explicacao no tooltip, em vez de criar
		// um socket orfao que so daria erro depois — um socket sem osso nao tem
		// onde existir no espaco.
		const bool canAdd = !m_SelectedBone.empty();

		if (!canAdd) ImGui::BeginDisabled();

		if (ui::IconButton(ICON_PLUS, "Criar socket no osso selecionado",
			ui::Accent::Add))
		{
			SkeletalMeshAsset::Socket s;
			s.BoneName = m_SelectedBone;

			// Nome unico ja na criacao: dois sockets homonimos fariam o
			// FindSocket devolver sempre o primeiro, e o segundo seria
			// inalcancavel sem nenhum aviso — a mesma armadilha das entidades
			// de mesmo nome que ja nos custou uma sessao.
			int n = 1;
			std::string candidate;
			bool taken = true;

			while (taken)
			{
				candidate = "Socket_" + std::to_string(n++);
				taken = false;
				for (const auto& other : sockets)
					if (other.Name == candidate) { taken = true; break; }
			}

			s.Name = candidate;

			// SOCKET_UNDO_V1 — criar entra no historico. O Execute insere e o
			// Undo remove, os dois pelo INDICE do fim da lista: enquanto os
			// comandos forem desfeitos na ordem, esse indice e estavel.
			Command cmd;
			cmd.Name = "Criar socket";
			cmd.Execute = [this, s]()
				{
					if (!m_Skeleton) return;
					auto& list = m_Skeleton->GetSockets();
					list.push_back(s);
					m_SelectedSocket = (int)list.size() - 1;
					m_Skeleton->Save();
				};
			cmd.Undo = [this]()
				{
					if (!m_Skeleton) return;
					auto& list = m_Skeleton->GetSockets();
					if (list.empty()) return;
					list.pop_back();
					m_SelectedSocket = -1;
					m_Skeleton->Save();
				};
			m_SocketHistory.Push(std::move(cmd));
		}

		if (!canAdd)
		{
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Selecione um osso na arvore acima primeiro.");
		}

		// ── Deletar, agora com botao proprio ──────────────────────────────
		//
		// Antes so existia no menu de contexto do item, que e um lugar onde
		// ninguem procura sem ja saber que esta la. O menu continua
		// funcionando; isto e a porta visivel.
		ImGui::SameLine();
		{
			const bool canDelete = m_SelectedSocket >= 0
				&& m_SelectedSocket < (int)sockets.size();

			ImGui::BeginDisabled(!canDelete);

			if (ui::IconButton(ICON_TRASH, "Apagar o socket selecionado\n"
				"(pode ser desfeito com Ctrl+Z)", ui::Accent::Danger) && canDelete)
			{
				const int index = m_SelectedSocket;
				const SkeletalMeshAsset::Socket removed = sockets[index];

				Command cmd;
				cmd.Name = "Apagar socket";
				cmd.Execute = [this, index]()
					{
						if (!m_Skeleton) return;
						auto& list = m_Skeleton->GetSockets();
						if (index < 0 || index >= (int)list.size()) return;
						list.erase(list.begin() + index);

						// O indice selecionado apontaria para outro socket (ou
						// para fora) depois do erase. Limpar e o unico estado
						// honesto: o que estava selecionado deixou de existir.
						m_SelectedSocket = -1;
						m_Skeleton->Save();
					};
				cmd.Undo = [this, index, removed]()
					{
						if (!m_Skeleton) return;
						auto& list = m_Skeleton->GetSockets();
						const int at = std::min(index, (int)list.size());
						list.insert(list.begin() + at, removed);
						m_SelectedSocket = at;
						m_Skeleton->Save();
					};
				m_SocketHistory.Push(std::move(cmd));
			}

			ImGui::EndDisabled();
		}

		// ── Undo / Redo ───────────────────────────────────────────────────
		//
		// Pilha propria desta janela — ver a nota do m_SocketHistory no header.
		// Os atalhos so respondem com a janela em foco, pelo mesmo motivo: o
		// Ctrl+Z do editor de cena nao pode desfazer um socket, nem o
		// contrario.
		ImGui::SameLine();
		{
			ImGui::BeginDisabled(!m_SocketHistory.CanUndo());
			if (ui::IconButton(ICON_UNDO, "Desfazer (Ctrl+Z)"))
				m_SocketHistory.Undo();
			ImGui::EndDisabled();

			ImGui::SameLine();
			ImGui::BeginDisabled(!m_SocketHistory.CanRedo());
			if (ui::IconButton(ICON_REDO, "Refazer (Ctrl+Y)"))
				m_SocketHistory.Redo();
			ImGui::EndDisabled();

			const bool focused = ImGui::IsWindowFocused(
				ImGuiFocusedFlags_RootAndChildWindows);

			if (focused && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().WantTextInput)
			{
				if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) m_SocketHistory.Undo();
				if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) m_SocketHistory.Redo();
			}
		}

		ImGui::Separator();

		if (sockets.empty())
		{
			ImGui::TextDisabled("Nenhum socket ainda.");
			return;
		}

		for (int i = 0; i < (int)sockets.size(); i++)
		{
			ImGui::PushID(i);

			const bool sel = (m_SelectedSocket == i);
			std::string label = sockets[i].Name + "   [" + sockets[i].BoneName + "]";

			if (ImGui::Selectable(label.c_str(), sel))
				m_SelectedSocket = i;

			if (ImGui::BeginPopupContextItem("##sockctx"))
			{
				if (ImGui::MenuItem(ICON_TRASH "  Apagar"))
				{
					// Passa pelo MESMO caminho do botao: selecionar e deixar o
					// bloco de cima empilhar o comando. Apagar aqui direto
					// deixaria uma das duas portas fora do undo, e a diferenca
					// so apareceria quando alguem tentasse desfazer.
					m_SelectedSocket = i;

					const SkeletalMeshAsset::Socket removed = sockets[i];
					const int index = i;

					Command cmd;
					cmd.Name = "Apagar socket";
					cmd.Execute = [this, index]()
						{
							if (!m_Skeleton) return;
							auto& list = m_Skeleton->GetSockets();
							if (index < 0 || index >= (int)list.size()) return;
							list.erase(list.begin() + index);
							m_SelectedSocket = -1;
							m_Skeleton->Save();
						};
					cmd.Undo = [this, index, removed]()
						{
							if (!m_Skeleton) return;
							auto& list = m_Skeleton->GetSockets();
							const int at = std::min(index, (int)list.size());
							list.insert(list.begin() + at, removed);
							m_SelectedSocket = at;
							m_Skeleton->Save();
						};
					m_SocketHistory.Push(std::move(cmd));

					ImGui::EndPopup();
					ImGui::PopID();
					break;
				}
				ImGui::EndPopup();
			}

			ImGui::PopID();
		}

		if (m_SelectedSocket >= 0 && m_SelectedSocket < (int)sockets.size())
		{
			ImGui::Separator();
			DrawSocketDetails();
		}
	}

	void AnimClipWindow::DrawSocketDetails()
	{
		auto& sockets = m_Skeleton->GetSockets();
		auto& s = sockets[m_SelectedSocket];

		// SC40 — dois estados, nao um.
		//
		//  dirty  = o valor EM MEMORIA mudou. O preview le o asset todo frame
		//           (UpdateSocketPreview no RenderPreview), entao isto sozinho
		//           ja e o que faz o arrasto aparecer na hora.
		//  commit = hora de GRAVAR. Um DragFloat3 dispara a cada frame de
		//           arrasto; gravar ali reescrevia o .axeskel dezenas de vezes
		//           por segundo. O gizmo (DrawSocketGizmo) ja tratava isso
		//           corretamente — o painel numerico e que estava fora de
		//           passo. Disco nao e onde se guarda estado intermediario de
		//           um gesto.
		bool dirty = false;
		bool commit = false;

		// ── Nome ─────────────────────────────────────────────────────────────
		char nameBuf[128] = {};
		strncpy(nameBuf, s.Name.c_str(), sizeof(nameBuf) - 1);

		ui::SectionHeader(ICON_FILE, "Nome", ui::Accent::Neutral);
		ImGui::SetNextItemWidth(-1);

		if (ImGui::InputText("##sockname", nameBuf, sizeof(nameBuf)))
		{
			// Rejeita colisao em vez de aceitar e quebrar o FindSocket. Vazio
			// tambem sai fora: um socket sem nome nao pode ser referenciado.
			const std::string candidate = nameBuf;
			bool taken = candidate.empty();

			for (int i = 0; i < (int)sockets.size() && !taken; i++)
				if (i != m_SelectedSocket && sockets[i].Name == candidate) taken = true;

			if (!taken) { s.Name = candidate; dirty = true; }
		}

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			commit = true;

			if (s.Name != nameBuf)
				ImGui::SetTooltip("O nome tem de ser unico e nao pode ser vazio.");
		}

		// ── Osso pai ─────────────────────────────────────────────────────────
		//
		// Combo, e nao "usa o selecionado na arvore": remontar um socket para
		// outro osso e uma correcao deliberada, e depender da selecao da
		// arvore faria isso acontecer sem querer ao navegar.
		ui::SectionHeader(ICON_BONE, "Osso pai", ui::Accent::Neutral);
		ImGui::SetNextItemWidth(-1);

		if (const auto& skel = m_Skeleton->GetSkeleton())
		{
			if (ImGui::BeginCombo("##sockbone", s.BoneName.c_str()))
			{
				for (const auto& b : skel->GetBones())
				{
					const bool selB = (b.Name == s.BoneName);
					if (ImGui::Selectable(b.Name.c_str(), selB))
					{
						s.BoneName = b.Name;
						dirty = true;
						commit = true;   // escolha discreta: grava na hora
					}
					if (selB) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}

		// ── Transform relativo ao osso ───────────────────────────────────────
		ImGui::Spacing();
		ui::SectionHeader(ICON_ARROWS, "Relativo ao osso", ui::Accent::Primary);

		auto vec3Row = [&](const char* label, const char* id, glm::vec3& v, float step)
			{
				ImGui::TextDisabled("%s", label);
				ImGui::SetNextItemWidth(-1);

				// dirty por frame de arrasto (o preview acompanha ao vivo),
				// commit so quando o arrasto termina (um Save por gesto).
				// SOCKET_UNDO_V1 — o "antes" e capturado quando o arrasto
				// COMECA. Um comando por gesto, e nao um por frame.
				if (ImGui::DragFloat3(id, &v.x, step, 0.0f, 0.0f, "%.3f"))
				{
					BeginSocketEdit();
					dirty = true;
				}
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					commit = true;
					EndSocketEdit("Transform do socket");
				}
			};

		vec3Row("Location", "##sockloc", s.Location, 0.01f);
		vec3Row("Rotation", "##sockrot", s.Rotation, 0.5f);    // graus
		vec3Row("Scale", "##socksca", s.Scale, 0.01f);

		// ── SC38: compensacao de unidade ─────────────────────────────────────
		//
		//  Anexar a um osso HERDA a escala do personagem — e isso esta certo:
		//  em runtime a arma presa na mao encolhe se a mao encolher. O problema
		//  e que o Y Bot veio do Mixamo em centimetros e o preview o reduz a
		//  1/100, enquanto a pistola ja foi autorada numa escala onde 1 serve.
		//  Multiplicada por 0.01, ela fica com 2mm dentro da palma: renderiza,
		//  e ninguem ve.
		//
		//  Este botao NAO cancela a heranca no render — fazer isso faria o
		//  editor mentir, mostrando um tamanho que o jogo nao teria. Ele apenas
		//  ESCREVE no campo Scale o fator que compensa, deixando o numero
		//  visivel e editavel. A decisao continua sendo do autor; o botao so
		//  poupa a conta.
		//
		//  A raiz do problema e a falta de um fator de unidade no asset da
		//  malha, gravado na importacao. Enquanto ele nao existe, a compensacao
		//  por socket e o lugar honesto para ela morar.
		// ═══════════════════════════════════════════════════════════════════
		//  SOCKET_SIZE_V3 — o tamanho no MUNDO, em vez de um fator misterioso
		//
		//  ── O QUE O "Match mesh units" FAZIA, E POR QUE VIROU UM BECO ──────
		//
		//  Ele escrevia Scale = 1 / (escala do personagem no preview). Como o
		//  Y Bot vem do Mixamo em centimetros, essa conta da SEMPRE ~100.263 —
		//  o mesmo numero, para qualquer malha. Clicar com 100.263 ja no campo
		//  nao mudava nada; digitar outro valor e clicar devolvia 100.263.
		//  Nao era defeito de implementacao: o botao so nao dependia da malha.
		//
		//  Ele nasceu certo. Naquela epoca a malha nao tinha unidade nenhuma
		//  gravada, e cancelar a heranca do personagem era a unica compensacao
		//  possivel. O `mesh_scale` do `.axemeta` acabou com essa necessidade:
		//  a malha agora TEM um tamanho real, e o que falta saber e com que
		//  tamanho ela vai aparecer.
		//
		//  ── O QUE ENTRA NO LUGAR ──────────────────────────────────────────
		//
		//  O numero que importa: quantos metros a malha vai medir na mao do
		//  personagem. Ele sai da conta completa
		//
		//      tamanho da malha  x  Scale do socket  x  escala do personagem
		//
		//  e o campo ao lado resolve o Scale para o tamanho que se pedir. Em
		//  vez de um fator sem unidade que so um dos tres sistemas entende,
		//  digita-se "0.186" e a pistola fica com 18,6 cm.
		// ═══════════════════════════════════════════════════════════════════
		if (m_PreviewScene && m_PreviewEntity != entt::null)
		{
			auto& preg = m_PreviewScene->GetRegistry();

			if (auto* ctc = preg.try_get<TransformComponent>(m_PreviewEntity))
			{
				const glm::vec3 cs = ctc->Data.Scale;

				// Mede a malha UMA vez por UUID — ver a nota do
				// m_SocketMeshMeasuredUUID no header.
				if (s.PreviewMeshUUID != m_SocketMeshMeasuredUUID)
				{
					m_SocketMeshMeasuredUUID = s.PreviewMeshUUID;
					m_SocketMeshSize = 0.0f;

					if (!s.PreviewMeshUUID.empty())
					{
						if (auto mesh = MeshFactory::ResolveByUUID(s.PreviewMeshUUID))
						{
							const auto& verts = mesh->GetVertices();
							if (!verts.empty())
							{
								glm::vec3 mn = verts[0].Position, mx = mn;
								for (const auto& v : verts)
								{
									mn = glm::min(mn, v.Position);
									mx = glm::max(mx, v.Position);
								}
								const glm::vec3 sz = mx - mn;
								m_SocketMeshSize = std::max({ sz.x, sz.y, sz.z });
							}
						}
					}
				}

				if (m_SocketMeshSize > 0.0001f)
				{
					// A escala efetiva usa o eixo X dos dois fatores. Scale
					// nao-uniforme num socket e patologia, nao caso de uso — e
					// tratar os tres eixos aqui daria tres numeros que nao
					// respondem a pergunta ("que tamanho isso tem?").
					const float effective = s.Scale.x * cs.x;
					const float worldSize = m_SocketMeshSize * effective;

					ImGui::Spacing();
					ImGui::TextDisabled("Tamanho no mundo");

					ImGui::SetNextItemWidth(-1);

					static float s_TargetWorld = 0.0f;
					if (!ImGui::IsAnyItemActive())
						s_TargetWorld = worldSize;

					if (ImGui::DragFloat("##sockworld", &s_TargetWorld,
						0.005f, 0.001f, 100.0f, "%.3f m"))
					{
						BeginSocketEdit();

						// Resolve o Scale do socket para o tamanho pedido. A
						// escala do personagem entra na conta porque o socket
						// a HERDA — ignora-la daria um numero certo no painel
						// e errado na tela.
						const float denom = m_SocketMeshSize * cs.x;
						if (denom > 1e-6f)
						{
							const float k = s_TargetWorld / denom;
							s.Scale = glm::vec3(k);
							dirty = true;
						}
					}
					if (ImGui::IsItemDeactivatedAfterEdit())
					{
						commit = true;
						EndSocketEdit("Tamanho do socket");
					}

					ImGui::TextDisabled("malha %.3f m  x  %.3f  =  %.3f m",
						m_SocketMeshSize, effective, worldSize);
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip(
							"Tamanho real do asset (Asset Viewer > Importacao),\n"
							"multiplicado pelo Scale do socket e pela escala do\n"
							"personagem no preview (%.4f).\n\n"
							"O personagem veio em centimetros, por isso o fator\n"
							"~100 no Scale: ele cancela essa heranca. Nao e um\n"
							"numero errado, e a conversao de unidade do modelo.",
							cs.x);

					// Substitui o "Match mesh units". Faz a mesma conta que ele
					// fazia — e agora com um nome que diz o que ela significa.
					if (ui::AccentButton(ICON_EXPAND "  Tamanho real do asset",
						ui::Accent::Neutral,
						"Escreve o Scale que faz a malha aparecer exatamente\n"
						"com o tamanho que ela tem no Asset Viewer.",
						ImVec2(-1, 0)))
					{
						BeginSocketEdit();
						s.Scale = {
							cs.x != 0.0f ? 1.0f / cs.x : 1.0f,
							cs.y != 0.0f ? 1.0f / cs.y : 1.0f,
							cs.z != 0.0f ? 1.0f / cs.z : 1.0f
						};
						dirty = true;
						commit = true;
						EndSocketEdit("Tamanho real do socket");
					}
				}
			}
		}

		// ── Preview mesh ─────────────────────────────────────────────────────
		ImGui::Spacing();
		ui::SectionHeader(ICON_CUBE, "Malha de preview (so no editor)", ui::Accent::Neutral);

		if (AssetPicker::Draw("Mesh", s.PreviewMeshUUID,
			{ AssetType::Mesh }, [&](const AssetRecord&) { dirty = true; commit = true; }))
		{
			dirty = true;
			commit = true;
		}

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Serve para posicionar o socket aqui no editor.\n"
				"Ela nao e anexada em runtime.");

		// ═══════════════════════════════════════════════════════════════════
		//  SOCKET_UNITS_V2 — as DUAS escalas, lado a lado
		//
		//  ── O QUE ESTA NOTA CONSERTA ───────────────────────────────────────
		//
		//  O comentario do "Match mesh units", logo acima, termina dizendo que
		//  a raiz do problema e a falta de um fator de unidade no asset da
		//  malha, gravado na importacao. Esse fator agora EXISTE: e o
		//  `mesh_scale` do `.axemeta`.
		//
		//  Com ele, passaram a existir DOIS lugares que mexem no tamanho do
		//  que o socket mostra — a escala de importacao do asset e o Scale do
		//  socket — e nada na tela dizia isso. O sintoma real foi uma pistola
		//  reimportada com 1 m de comprimento aparecendo gigante na mao do
		//  personagem: o socket estava certo, ele so multiplicou fielmente uma
		//  malha que ja vinha grande do outro sistema.
		//
		//  ── POR QUE MOSTRAR EM VEZ DE CORRIGIR ────────────────────────────
		//
		//  Nao da para o socket "compensar" a escala de importacao: ela e a
		//  escala REAL do asset no mundo, e cancela-la aqui faria o preview
		//  mentir de novo — o inverso do defeito que o Match mesh units evita.
		//  O que faltava nao era conserto, era informacao.
		// ═══════════════════════════════════════════════════════════════════
		if (!s.PreviewMeshUUID.empty())
		{
			if (const AssetRecord* prec = AssetDatabase::Get().GetByUUID(s.PreviewMeshUUID))
			{
				const float importScale = prec->Import.MeshScale;

				if (std::abs(importScale - 1.0f) > 0.0001f)
				{
					ImGui::TextDisabled("Escala de importacao do asset: %.4f", importScale);
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip(
							"Vem do .axemeta da malha (Asset Viewer > Importacao).\n"
							"O Scale acima MULTIPLICA por cima dela.\n"
							"Se o objeto aparece grande demais aqui, confira\n"
							"primeiro o tamanho real do asset no Asset Viewer.");
				}
			}
		}

		// Grava so quando algo mudou de fato E o gesto acabou: este painel roda
		// por frame, e um Save por frame reescreveria o .axeskel continuamente.
		//
		// `dirty` sem `commit` NAO e perda de dado: o valor ja esta no asset em
		// memoria, o preview ja o reflete, e o proximo commit (soltar o mouse,
		// trocar de campo, Ctrl+S) o leva ao disco.
		(void)dirty;

		if (commit) m_Skeleton->Save();
	}

	// ── Centro: preview + timeline ───────────────────────────────────────	// ── Centro: preview + timeline ───────────────────────────────────────



	void AnimClipWindow::DrawTimeline()
	{
		auto clip = CurrentClip();

		if (!clip)
		{
			ImGui::TextDisabled("Select a clip.");
			return;
		}

		const float duration = std::max(clip->GetDuration(), 0.0001f);

		// Tempo de CLIPE pra tudo que o usuario ve e opera — o tempo cru do
		// player cresce sem limite no loop (ver nota no crossing).
		const float now = clip->WrapTime(PreviewTime());
		const int trackCount = std::max(1, clip->NotifyTrackCount);

		// ── Transporte (|< passo-a-passo >| Stop, como na Unreal) ────────
		const float frameStep = 1.0f / 30.0f;

		// Os mesmos icones e as mesmas cores do transporte do viewport: verde
		// para tocar, ambar para pausar, vermelho para parar. Duas barras de
		// transporte na mesma engine com aparencias diferentes fazem o usuario
		// reaprender o obvio a cada janela.
		if (ui::IconButton(ICON_ARROW_LEFT, "Ir para o inicio"))
		{
			SetPreviewTime(0.0f);
		}
		ImGui::SameLine();

		if (ui::IconButton(ICON_ROTATE_LEFT, "Frame anterior"))
		{
			m_Playing = false;
			SetPreviewTime(std::max(0.0f, now - frameStep));
		}
		ImGui::SameLine();

		if (ui::ToggleButton(m_Playing ? ICON_PAUSE : ICON_PLAY, m_Playing,
			m_Playing ? "Pausar" : "Tocar",
			m_Playing ? ui::Accent::Warning : ui::Accent::Add))
		{
			m_Playing = !m_Playing;
		}
		ImGui::SameLine();

		if (ui::IconButton(ICON_FORWARD_STEP, "Proximo frame"))
		{
			m_Playing = false;
			SetPreviewTime(std::min(duration, now + frameStep));
		}
		ImGui::SameLine();

		if (ui::IconButton(ICON_ARROW_RIGHT, "Ir para o fim"))
		{
			m_Playing = false;
			SetPreviewTime(duration);
		}
		ImGui::SameLine();

		if (ui::IconButton(ICON_STOP, "Parar e voltar ao inicio", ui::Accent::Danger))
		{
			m_Playing = false;
			SetPreviewTime(0.0f);
		}
		ImGui::SameLine();
		ImGui::Text("%.2fs / %.2fs   frame %d", now, duration, (int)(now * 30.0f));
		ImGui::SameLine(0.0f, 24.0f);
		ImGui::TextDisabled("Notifies: %d", (int)clip->Notifies.size());

		// ── Geometria: gutter de tracks a esquerda, regua + lanes ────────
		//
		// Tracks como na Unreal: linhas empilhadas, cada notify mora numa.
		// O gutter numera e hospeda o "+" (adicionar) e o menu de remover.
		ImDrawList* dl = ImGui::GetWindowDrawList();

		const float gutterW = 34.0f;
		const float rulerH = 24.0f;
		const float laneH = 26.0f;

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float w = std::max(60.0f, ImGui::GetContentRegionAvail().x - gutterW);

		const float lanesTop = origin.y + rulerH;
		const float lanesBottom = lanesTop + laneH * trackCount;
		const float rx = origin.x + gutterW;   // inicio da area de tempo

		auto timeToX = [&](float t) { return rx + (t / duration) * w; };
		auto xToTime = [&](float x)
			{
				const float t = ((x - rx) / w) * duration;
				return std::clamp(t, 0.0f, duration);
			};
		auto yToTrack = [&](float y)
			{
				const int tr = (int)((y - lanesTop) / laneH);
				return std::clamp(tr, 0, trackCount - 1);
			};

		// Fundos: regua + lanes (zebrado leve pra ler as linhas).
		dl->AddRectFilled(ImVec2(rx, origin.y), ImVec2(rx + w, origin.y + rulerH),
			IM_COL32(28, 28, 32, 255));

		for (int tr = 0; tr < trackCount; ++tr)
		{
			const float y0 = lanesTop + laneH * tr;
			const ImU32 bg = (tr % 2 == 0) ? IM_COL32(20, 20, 24, 255)
				: IM_COL32(24, 24, 29, 255);

			dl->AddRectFilled(ImVec2(rx, y0), ImVec2(rx + w, y0 + laneH), bg);

			// Gutter da track: numero + menu de contexto.
			dl->AddRectFilled(ImVec2(origin.x, y0), ImVec2(rx - 2.0f, y0 + laneH),
				IM_COL32(30, 30, 36, 255));

			char num[8];
			std::snprintf(num, sizeof(num), "%d", tr + 1);
			dl->AddText(ImVec2(origin.x + 12.0f, y0 + (laneH - ImGui::GetTextLineHeight()) * 0.5f),
				IM_COL32(150, 150, 160, 255), num);

			ImGui::SetCursorScreenPos(ImVec2(origin.x, y0));
			ImGui::PushID(tr + 500);
			ImGui::InvisibleButton("##gutter", ImVec2(gutterW - 2.0f, laneH));

			if (ImGui::BeginPopupContextItem("track_ctx"))
			{
				bool empty = true;

				for (const auto& n : clip->Notifies)
					if (n.Track == tr) { empty = false; break; }

				if (ImGui::MenuItem("Remove track", nullptr, false, empty && trackCount > 1))
				{
					for (auto& n : clip->Notifies)
						if (n.Track > tr) --n.Track;

					clip->NotifyTrackCount = trackCount - 1;
					MarkMetaEdited();
				}

				if (!empty && ImGui::IsItemHovered())
					ImGui::SetTooltip("Only empty tracks can be removed.");

				ImGui::EndPopup();
			}

			ImGui::PopID();
		}

		// ── Ticks da regua ───────────────────────────────────────────────
		for (float t = 0.0f; t <= duration + 0.0001f; t += 0.1f)
		{
			const bool major = (std::fmod(t + 0.0001f, 0.5f) < 0.01f);
			const float x = timeToX(t);

			dl->AddLine(ImVec2(x, origin.y + (major ? 4.0f : 12.0f)), ImVec2(x, origin.y + rulerH),
				IM_COL32(90, 90, 100, 255), 1.0f);

			if (major)
			{
				char buf[16];
				std::snprintf(buf, sizeof(buf), "%.1f", t);
				dl->AddText(ImVec2(x + 3.0f, origin.y + 1.0f), IM_COL32(140, 140, 150, 255), buf);
			}
		}

		// ── Regua: SCRUB (so). Criar notify e nas lanes ──────────────────
		ImGui::SetCursorScreenPos(ImVec2(rx, origin.y));
		ImGui::InvisibleButton("acw_ruler", ImVec2(w, rulerH));

		if (ImGui::IsItemActive())
		{
			SetPreviewTime(xToTime(ImGui::GetMousePos().x));
			m_Playing = false;   // mexer no tempo com ele correndo e briga
		}

		// ── Lanes: duplo-clique cria notify NAQUELA track ────────────────
		//
		// Submetidas ANTES dos losangos: no ImGui o ultimo item na mesma
		// posicao ganha o hover — os losangos ficam por cima.
		for (int tr = 0; tr < trackCount; ++tr)
		{
			ImGui::SetCursorScreenPos(ImVec2(rx, lanesTop + laneH * tr));
			ImGui::PushID(tr + 700);

			// CRITICO: com botoes SOBREPOSTOS, o ImGui da o CLIQUE pro
			// primeiro submetido na posicao. A lane cobre a linha inteira e
			// e submetida ANTES dos losangos — sem overlap ela COMIA o
			// clique de todo pin. E TEM que ser o SetNextItemAllowOverlap
			// (ANTES do item): a variante pos-item e a API obsoleta e nesta
			// versao do ImGui nao libera o roubo do clique — foi por isso
			// que o sintoma sobreviveu a primeira tentativa.
			ImGui::SetNextItemAllowOverlap();
			ImGui::InvisibleButton("##lane", ImVec2(w, laneH));

			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				AnimNotify n;
				n.Time = xToTime(ImGui::GetMousePos().x);
				n.Name = "Notify";
				n.Track = tr;
				clip->Notifies.push_back(n);

				m_SelectedNotify = (int)clip->Notifies.size() - 1;
				MarkMetaEdited();
			}

			// Right-click = menu "Add Notify" como na Unreal, com o tipo ja
			// escolhido. O tempo e capturado NO CLIQUE — dentro do popup o
			// mouse ja foi embora.
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				m_PendingAddTime = xToTime(ImGui::GetMousePos().x);

			if (ImGui::BeginPopupContextItem("lane_add"))
			{
				ImGui::TextDisabled("Add Notify");
				ImGui::Separator();

				auto addNotify = [&](const char* name, AnimNotify::Kind kind, glm::vec3 color)
					{
						AnimNotify n;
						n.Time = m_PendingAddTime;
						n.Name = name;
						n.Type = kind;
						n.Color = color;
						n.Track = tr;
						clip->Notifies.push_back(n);

						m_SelectedNotify = (int)clip->Notifies.size() - 1;
						MarkMetaEdited();
					};

				if (ImGui::MenuItem("New Notify (Event)"))
					addNotify("Notify", AnimNotify::Kind::Event, { 0.47f, 0.75f, 1.0f });

				if (ImGui::MenuItem("Play Sound"))
					addNotify("PlaySound", AnimNotify::Kind::Sound, { 0.47f, 0.86f, 0.51f });

				if (ImGui::MenuItem("Play Particle Effect"))
					addNotify("PlayParticleEffect", AnimNotify::Kind::Particle, { 1.0f, 0.67f, 0.31f });

				ImGui::EndPopup();
			}

			ImGui::PopID();
		}

		// ── Losangos ─────────────────────────────────────────────────────
		for (std::size_t i = 0; i < clip->Notifies.size(); ++i)
		{
			auto& n = clip->Notifies[i];

			n.Track = std::clamp(n.Track, 0, trackCount - 1);

			const float x = timeToX(n.Time);
			const float midY = lanesTop + laneH * n.Track + laneH * 0.5f;
			const bool selected = ((int)i == m_SelectedNotify);

			const ImU32 col = ImGui::ColorConvertFloat4ToU32(
				ImVec4(n.Color.x, n.Color.y, n.Color.z, 1.0f));

			const float r = selected ? 8.0f : 6.0f;

			ImGui::SetCursorScreenPos(ImVec2(x - 8.0f, midY - 10.0f));
			ImGui::PushID((int)i + 100);
			ImGui::InvisibleButton("##notif", ImVec2(16, 20));

			const bool hovered = ImGui::IsItemHovered();

			if (ImGui::IsItemActivated())
			{
				m_SelectedNotify = (int)i;
				m_DraggingNotify = (int)i;
			}

			if (m_DraggingNotify == (int)i && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			{
				// Horizontal move o TEMPO; vertical troca de TRACK — igual
				// arrastar notify entre tracks na Unreal.
				n.Time = xToTime(ImGui::GetMousePos().x);
				n.Track = yToTrack(ImGui::GetMousePos().y);
				m_Dirty = true;   // persiste (ordenado) no soltar
			}

			if (m_DraggingNotify == (int)i && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
			{
				m_DraggingNotify = -1;
				MarkMetaEdited();   // indices sao estaveis: nada a re-achar
			}

			ImGui::PopID();

			dl->AddQuadFilled(
				ImVec2(x, midY - r), ImVec2(x + r, midY),
				ImVec2(x, midY + r), ImVec2(x - r, midY), col);

			if (selected || hovered)
				dl->AddQuad(
					ImVec2(x, midY - r - 2), ImVec2(x + r + 2, midY),
					ImVec2(x, midY + r + 2), ImVec2(x - r - 2, midY),
					IM_COL32(255, 255, 255, 220), 1.5f);

			// Nome ao lado do pin, como na Unreal — de relance voce sabe o
			// que cada losango dispara sem clicar em nada.
			if (!n.Name.empty())
				dl->AddText(ImVec2(x + r + 5.0f, midY - ImGui::GetTextLineHeight() * 0.5f),
					IM_COL32(210, 210, 220, selected ? 255 : 170), n.Name.c_str());

			if (hovered)
				ImGui::SetTooltip("%s  (%.2fs, track %d)", n.Name.c_str(), n.Time, n.Track + 1);
		}

		// ── Playhead por cima de tudo ────────────────────────────────────
		{
			const float x = timeToX(now);   // ja em tempo de clipe
			dl->AddLine(ImVec2(x, origin.y), ImVec2(x, lanesBottom),
				IM_COL32(255, 90, 60, 255), 2.0f);
			dl->AddTriangleFilled(
				ImVec2(x - 5, origin.y), ImVec2(x + 5, origin.y), ImVec2(x, origin.y + 7),
				IM_COL32(255, 90, 60, 255));
		}

		// ── "+" adiciona track (no gutter, abaixo da ultima lane) ────────
		ImGui::SetCursorScreenPos(ImVec2(origin.x + 4.0f, lanesBottom + 4.0f));

		if (ImGui::SmallButton("+"))
		{
			clip->NotifyTrackCount = trackCount + 1;
			MarkMetaEdited();
		}

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Add notify track");

		ImGui::SetCursorScreenPos(ImVec2(origin.x, lanesBottom + 28.0f));
	}

	// ── Direita: Asset Details + notify selecionado ──────────────────────

	void AnimClipWindow::DrawRightPanel()
	{
		auto clip = CurrentClip();

		if (!clip)
		{
			ImGui::TextDisabled("Select a clip.");
			return;
		}

		ImGui::TextUnformatted("Asset Details");
		ImGui::Separator();

		ImGui::TextDisabled("%s", clip->GetName().c_str());
		ImGui::Text("Duration: %.2fs  (%d frames @30)", clip->GetDuration(),
			(int)(clip->GetDuration() * 30.0f));
		ImGui::Spacing();

		bool loop = clip->IsLooping();
		if (ImGui::Checkbox("Loop", &loop))
		{
			clip->SetLooping(loop);
			MarkMetaEdited();
		}

		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::DragFloat("Rate Scale", &clip->RateScale, 0.05f, 0.05f, 10.0f, "%.2fx"))
			MarkMetaEdited();

		if (ImGui::Checkbox("Root Motion", &clip->RootMotion))
			MarkMetaEdited();

		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Already saved to the .axeskel.\nRuntime consumption (moving the capsule)\nis the next step of the plan.");

		// ── Notify selecionado ───────────────────────────────────────────
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextUnformatted("Notify");
		ImGui::Separator();

		if (m_SelectedNotify < 0 || m_SelectedNotify >= (int)clip->Notifies.size())
		{
			ImGui::TextWrapped("Double-click a timeline lane to create a notify; click a diamond to edit it here.");
			return;
		}

		auto& n = clip->Notifies[m_SelectedNotify];

		char nameBuf[64];
		std::snprintf(nameBuf, sizeof(nameBuf), "%s", n.Name.c_str());

		ImGui::SetNextItemWidth(-1);
		if (ImGui::InputText("##nname", nameBuf, sizeof(nameBuf)))
		{
			n.Name = nameBuf;
			MarkMetaEdited();
		}

		const char* kinds[] = { "Event (script)", "Sound", "Particle" };
		int kind = (int)n.Type;

		ImGui::SetNextItemWidth(-1);
		if (ImGui::Combo("##nkind", &kind, kinds, 3))
		{
			n.Type = (AnimNotify::Kind)kind;

			// Trocar de tipo re-tinge o losango com a cor padrao do tipo
			// novo (o usuario pode personalizar depois no Notify Color).
			n.Color = { 0.47f, 0.75f, 1.0f };
			if (n.Type == AnimNotify::Kind::Sound)    n.Color = { 0.47f, 0.86f, 0.51f };
			if (n.Type == AnimNotify::Kind::Particle) n.Color = { 1.0f, 0.67f, 0.31f };

			MarkMetaEdited();
		}

		if (n.Type != AnimNotify::Kind::Event)
		{
			// Seletor de asset DE VERDADE, como na Unreal: lista so os assets
			// do tipo certo (Sound -> Audio, Particle -> ParticleSystem) no
			// mesmo modal do resto do editor. O Payload guarda o UUID — a
			// mesma moeda que materiais e cenas usam.
			const bool isSound = (n.Type == AnimNotify::Kind::Sound);

			// Notify de som aceita .wav cru E Sound Cue. E o caso de uso mais
			// forte que o cue tem: cinco variacoes de passo num asset so,
			// sem mexer na timeline.
			std::vector<AssetType> filter = {
				isSound ? AssetType::Audio : AssetType::ParticleSystem };

			if (isSound)
				filter.push_back(AssetType::SoundCue);

			if (AssetPicker::Draw(isSound ? "Sound" : "Particle", n.Payload, filter,
				[](const AssetRecord&) {}))
			{
				MarkMetaEdited();
			}

			// "Browse to Asset": navega o Asset Browser ate o asset do
			// notify e o seleciona.
			if (!n.Payload.empty() && m_AssetBrowser)
			{
				if (ImGui::Button("Find in Asset Browser", ImVec2(-1, 0)))
					m_AssetBrowser->RevealAsset(n.Payload);
			}

			// ── Ancoragem e transform (Anim Notify Details da Unreal) ────
			ImGui::Spacing();
			ui::SectionHeader(ICON_LINK, "Ancoragem", ui::Accent::Primary);

			// ═══════════════════════════════════════════════════════════════
			//  NOTIFY_SOCKET_V3 — o padrao silencioso que custou duas rodadas
			//
			//  O combo abaixo nascia em "(character origin)" sem rotulo e sem
			//  nenhum aviso. Quem posicionava o FX olhando o cano no preview
			//  do editor via a particula certa ali — porque o preview usava o
			//  offset cru — e errada no jogo, que somava esse offset a origem
			//  do personagem, la nos pes.
			//
			//  Um padrao que produz o resultado errado em silencio e pior que
			//  um erro: o erro voce ve. Entao o painel agora DIZ, em ambar, o
			//  que a escolha atual significa, e oferece o conserto de um
			//  clique quando o esqueleto tem socket.
			// ═══════════════════════════════════════════════════════════════
			if (n.Socket.empty() && m_Skeleton)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.35f, 1.0f),
					"Sai da ORIGEM do personagem (os pes).");
				ImGui::TextDisabled("O offset abaixo e medido a partir dali, e nao");
				ImGui::TextDisabled("da arma — por isso o FX nao acompanha a mao.");

				const auto& sks = m_Skeleton->GetSockets();

				if (!sks.empty())
				{
					char btn[128];
					std::snprintf(btn, sizeof(btn), ICON_LINK "  Ancorar em '%s'",
						sks[0].Name.c_str());

					if (ui::AccentButton(btn, ui::Accent::Primary,
						"Faz o FX sair do socket, na pose de cada frame",
						ImVec2(-1, 0)))
					{
						n.Socket = sks[0].Name;
						MarkMetaEdited();
					}
				}
				else
				{
					ImGui::TextDisabled("Este esqueleto ainda nao tem socket.");
					ImGui::TextDisabled("Crie um na aba Skeleton, no osso da mao.");
				}
			}

			{
				const char* current = n.Socket.empty()
					? "(origem do personagem)" : n.Socket.c_str();

				ImGui::SetNextItemWidth(-1);

				if (ImGui::BeginCombo("##nsocket", current))
				{
					if (ImGui::Selectable("(origem do personagem)", n.Socket.empty()))
					{
						n.Socket.clear();
						MarkMetaEdited();
					}

					if (const auto& skel = m_Skeleton->GetSkeleton())
					{
						// ── Sockets primeiro ────────────────────────────
						//
						// Um socket ja carrega posicao, giro e escala
						// autorados no painel Skeleton — e onde a ponta do
						// cano realmente esta. Escolher o OSSO cru poe o FX no
						// punho, e o autor tem de reencontrar o cano na mao
						// com tres floats. Os ossos continuam na lista logo
						// abaixo para quem quer exatamente isso.
						for (const auto& sk : m_Skeleton->GetSockets())
						{
							std::string label = ICON_LINK "  " + sk.Name;
							if (ImGui::Selectable(label.c_str(), n.Socket == sk.Name))
							{
								n.Socket = sk.Name;
								MarkMetaEdited();
							}
							if (ImGui::IsItemHovered())
								ImGui::SetTooltip("Socket em '%s'", sk.BoneName.c_str());
						}

						if (!m_Skeleton->GetSockets().empty())
							ImGui::Separator();

						for (const auto& b : skel->GetBones())
						{
							if (ImGui::Selectable(b.Name.c_str(), n.Socket == b.Name))
							{
								n.Socket = b.Name;
								MarkMetaEdited();
							}
						}
					}

					ImGui::EndCombo();
				}
			}

			if (ImGui::Checkbox("Attached", &n.Attached))
				MarkMetaEdited();

			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Attached: follows the bone while playing.\nUnchecked: spawns and stays in the world.");

			// Volume/Pitch so para som — para particula nao significam nada.
			//
			// Ficam ANTES do transform de propósito: sao o que o usuario mais
			// mexe num notify de som, e enterra-los abaixo de nove floats de
			// Location/Rotation/Scale os tornaria invisiveis.
			if (isSound)
			{
				if (ImGui::SliderFloat("Volume##notify", &n.Volume, 0.0f, 2.0f, "%.2f"))
					m_Dirty = true;

				if (ImGui::SliderFloat("Pitch##notify", &n.Pitch, 0.25f, 4.0f, "%.2f"))
					m_Dirty = true;

				ImGui::TextDisabled("Multiplicam o Sound Cue, nao o substituem.");
				ImGui::Separator();
			}

			if (ImGui::DragFloat3("Location", &n.LocationOffset.x, 0.01f))  m_Dirty = true;
			if (ImGui::IsItemDeactivatedAfterEdit()) MarkMetaEdited();

			if (ImGui::DragFloat3("Rotation", &n.RotationOffset.x, 0.5f))   m_Dirty = true;
			if (ImGui::IsItemDeactivatedAfterEdit()) MarkMetaEdited();

			if (ImGui::DragFloat3("Scale", &n.Scale.x, 0.01f, 0.01f, 100.0f)) m_Dirty = true;
			if (ImGui::IsItemDeactivatedAfterEdit()) MarkMetaEdited();
		}

		// Cor do losango — pra todos os tipos, como o Notify Color da UE.
		if (ImGui::ColorEdit3("Notify Color", &n.Color.x, ImGuiColorEditFlags_NoInputs))
			MarkMetaEdited();

		ImGui::SetNextItemWidth(120.0f);
		float t = n.Time;
		if (ImGui::DragFloat("Time", &t, 0.01f, 0.0f, clip->GetDuration(), "%.2fs"))
		{
			n.Time = t;
			m_Dirty = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit())
			MarkMetaEdited();

		ImGui::Spacing();

		if (ImGui::Button("Remove notify", ImVec2(-1, 0)))
		{
			clip->Notifies.erase(clip->Notifies.begin() + m_SelectedNotify);
			m_SelectedNotify = -1;
			MarkMetaEdited();
		}
	}

} // namespace axe