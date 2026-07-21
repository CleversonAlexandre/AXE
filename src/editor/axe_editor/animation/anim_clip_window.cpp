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

#include "anim_clip_window.hpp"

#include "axe/scene/components.hpp"
#include "axe/graphics/renderer/viewport_renderer.hpp"
#include "axe/renderer/scene_renderer.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/editor_camera.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/log/log.hpp"

#include "editor/axe_editor/asset/asset_picker.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/particles/particle_system_asset.hpp"
#include "axe/particles/particle_system_component.hpp"
#include "editor/axe_editor/asset_browser.hpp"

#include <imgui_internal.h>   // DockBuilder — layout padrao do dockspace, uma vez

#include <algorithm>
#include <cmath>
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

		m_Skeleton = skeleton;
		m_Open = (skeleton != nullptr);
		m_Dirty = false;
		m_SelectedNotify = -1;
		m_DraggingNotify = -1;

		// Primeiro clipe ja selecionado: abrir num editor vazio e abrir
		// numa pergunta ("e agora?").
		m_SelectedClip = (m_Skeleton && !m_Skeleton->GetClips().empty()) ? 0 : -1;

		SyncPreviewCharacter();
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

		// Origem do personagem + offset autorado. A ancoragem no OSSO
		// (Socket/Attached) entra junto com o disparo no runtime — la os
		// skinning matrices ja estao na mao; aqui o offset da a posicao.
		tc.Data.Position = n.LocationOffset;
		tc.Data.Rotation = glm::radians(n.RotationOffset);   // Transform guarda radianos
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
		if (ImGui::Button("Save"))
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
		ImGui::TextDisabled("|  double-click / right-click a lane = add notify  |  drag diamond = move / change track  |  Alt+drag in viewport = camera");
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
		}
		else
		{
			ImGui::Dummy(pavail);
		}

		m_PreviewHovered = ImGui::IsItemHovered();
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

		ImGui::TextDisabled("%d bones  (sockets: coming soon)", (int)bones.size());
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

			void Walk(int idx) const
			{
				const auto& kids = (*Children)[idx];

				ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;

				if (kids.empty())
					flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
				else if (idx == 0)
					flags |= ImGuiTreeNodeFlags_DefaultOpen;

				const bool open = ImGui::TreeNodeEx(
					(*Bones)[idx].Name.c_str(), flags);

				if (open && !kids.empty())
				{
					for (int c : kids)
						Walk(c);

					ImGui::TreePop();
				}
			}
		};

		Walker w{ &bones, &children };

		for (std::size_t i = 0; i < bones.size(); ++i)
			if (bones[i].ParentIndex < 0)
				w.Walk((int)i);
	}

	// ── Centro: preview + timeline ───────────────────────────────────────



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

		if (ImGui::Button("|<")) { SetPreviewTime(0.0f); }
		ImGui::SameLine();

		if (ImGui::Button("<")) { m_Playing = false; SetPreviewTime(std::max(0.0f, now - frameStep)); }
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous frame");
		ImGui::SameLine();

		if (ImGui::Button(m_Playing ? "Pause" : "Play "))
			m_Playing = !m_Playing;
		ImGui::SameLine();

		if (ImGui::Button(">")) { m_Playing = false; SetPreviewTime(std::min(duration, now + frameStep)); }
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next frame");
		ImGui::SameLine();

		if (ImGui::Button(">|")) { m_Playing = false; SetPreviewTime(duration); }
		ImGui::SameLine();

		if (ImGui::Button("Stop")) { m_Playing = false; SetPreviewTime(0.0f); }
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

			const std::vector<AssetType> filter = {
				isSound ? AssetType::Audio : AssetType::ParticleSystem };

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
			ImGui::TextDisabled("Attach");
			ImGui::Separator();

			// Socket: combo com os OSSOS reais do esqueleto. "" = origem do
			// personagem. (No preview, o spawn ainda usa a origem + offset;
			// a ancoragem no osso entra junto com o disparo no runtime.)
			{
				const char* current = n.Socket.empty() ? "(character origin)" : n.Socket.c_str();

				ImGui::SetNextItemWidth(-1);

				if (ImGui::BeginCombo("##nsocket", current))
				{
					if (ImGui::Selectable("(character origin)", n.Socket.empty()))
					{
						n.Socket.clear();
						MarkMetaEdited();
					}

					if (const auto& skel = m_Skeleton->GetSkeleton())
					{
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