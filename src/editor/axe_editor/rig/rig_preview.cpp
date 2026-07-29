// rig_preview.cpp
//
// Preview 3D do Control Rig: cena PROPRIA com o personagem, e as formas dos
// Controls desenhadas por cima.
//
// A cena e propria pelo mesmo motivo do AnimGraph: assim voce monta o rig de um
// personagem que nem foi colocado na cena ainda, e mexer aqui nao mexe no jogo.
//
// ── POR QUE OS GIZMOS SAO DESENHADOS EM 2D ───────────────────────────────
//
// O caminho "certo" seria malha 3D por forma, com depth test. Mas um controle
// que some atras da perna e um controle que voce nao consegue clicar — e a
// razao de existir do controle e ser clicavel. Por isso as formas sao
// PROJETADAS e desenhadas no draw list do ImGui, sempre por cima: e o que a
// Unreal faz com os controles dela, e pelo mesmo motivo.

#include "control_rig_window.hpp"

#include "axe/scene/components.hpp"
#include "axe/animation/animation_world.hpp"
#include "axe/animation/animation_sampler.hpp"
#include "axe/graphics/renderer/viewport_renderer.hpp"
#include "axe/renderer/scene_renderer.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/scene_environment.hpp"
#include "axe/graphics/editor_camera.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/log/log.hpp"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>

namespace axe
{
	// O DESTRUTOR mora aqui, e nao no control_rig_window.cpp, de proposito.
	//
	// A janela guarda unique_ptr de ViewportRenderer / Scene / SceneEnvironment,
	// que sao apenas FORWARD-DECLARADOS no header. Pra destruir um unique_ptr o
	// compilador precisa do tipo COMPLETO — e este e o unico .cpp que inclui
	// todos eles. Definir o destrutor no outro arquivo daria
	// "invalid application of sizeof to incomplete type".
	ControlRigWindow::~ControlRigWindow()
	{
		if (m_EdCtx)
		{
			ed::DestroyEditor(m_EdCtx);
			m_EdCtx = nullptr;
		}
	}

	void ControlRigWindow::InitPreviewScene()
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
		m_PreviewEntity = m_PreviewScene->CreateEntity("RigPreview");

		auto& reg = m_PreviewScene->GetRegistry();

		auto light = m_PreviewScene->CreateEntity("PreviewLight");
		auto& lc = reg.emplace<LightComponent>(light);
		lc.Data = std::make_shared<DirectionalLight>();
		lc.Data->Direction = glm::vec3(0.3f, -1.0f, -0.6f);
		lc.Data->Color = glm::vec3(1.0f);
		lc.Data->Intensity = 3.0f;
		lc.Data->AmbientStrength = 0.35f;

		m_PreviewRenderer->SetScene(m_PreviewScene.get());

		m_PreviewEnvironment = std::make_unique<SceneEnvironment>();
		m_PreviewEnvironment->LoadHDRI("resources/quarry_04_puresky_2k.hdr");
		m_PreviewRenderer->SetEnvironment(m_PreviewEnvironment.get());

		if (auto* sr = m_PreviewRenderer->GetSceneRenderer())
			sr->SetEnvironment(m_PreviewEnvironment.get());

		m_PreviewAnim = std::make_unique<AnimationWorld>();

		m_PreviewInit = true;
	}

	void ControlRigWindow::SyncPreviewCharacter()
	{
		if (!m_PreviewScene || !m_Skeleton || m_PreviewEntity == entt::null)
			return;

		if (m_PreviewSynced == m_Skeleton)
			return;

		m_PreviewSynced = m_Skeleton;

		auto& reg = m_PreviewScene->GetRegistry();

		auto* existing = reg.try_get<SkeletalMeshComponent>(m_PreviewEntity);
		auto& sk = existing ? *existing : reg.emplace<SkeletalMeshComponent>(m_PreviewEntity);

		sk.Asset = m_Skeleton;
		sk.Data = m_Skeleton->GetMesh();
		sk.Clips = m_Skeleton->GetClips();
		sk.CurrentClip = -1;

		// SEM isto o AnimationWorld ignora a entidade: ele so processa em modo
		// de edicao o que estiver marcado como preview.
		sk.PreviewInEditor = true;

		sk.ShowSkeleton = m_ShowSkeleton;

		// ── Enquadramento ────────────────────────────────────────────────────
		//
		// Identico ao da janela de clipe. Medimos a MALHA (nao os ossos): o
		// bounding box dos vertices e a unica medida confiavel da altura real.
		//
		// FBX da Mixamo vem em CENTIMETROS — o Y Bot tem ~180 unidades. Sem
		// normalizar, a camera nasce DENTRO do personagem, que e exatamente o
		// "nao aparece nada" que voce viu.
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

					m_PreviewScale = kTargetHeight / height;
					m_PreviewOffsetY = -mn.y * m_PreviewScale;

					// emplace se nao existir: com try_get puro, uma entidade sem
					// TransformComponent ficaria em escala 1 e sumiria da tela
					// sem nenhum aviso.
					auto* tcp = reg.try_get<TransformComponent>(m_PreviewEntity);
					auto& tc = tcp ? *tcp : reg.emplace<TransformComponent>(m_PreviewEntity);

					tc.Data.Scale = glm::vec3(m_PreviewScale);
					tc.Data.Position = glm::vec3(0.0f, m_PreviewOffsetY, 0.0f);

					if (m_PreviewRenderer && m_PreviewRenderer->m_Camera)
					{
						m_PreviewRenderer->m_Camera->SetView(
							glm::vec3(0.0f, kTargetHeight * 0.5f, 0.0f),
							kTargetHeight * 1.9f);
					}
				}
			}
		}

		AXE_EDITOR_INFO("Control Rig preview: personagem sincronizado (escala {:.4f}).",
			m_PreviewScale);
	}

	void ControlRigWindow::RenderPreview()
	{
		if (!m_Open || !m_Asset)
			return;

		if (!m_PreviewInit)
			InitPreviewScene();

		if (!m_PreviewRenderer || !m_PreviewFramebuffer || !m_PreviewScene)
			return;

		SyncPreviewCharacter();

		// ── O TICK ───────────────────────────────────────────────────────────
		//
		// E ele que produz as matrizes de skinning. Sem este passo o
		// SkeletalMeshComponent nunca e resolvido e o renderer nao tem o que
		// desenhar — o personagem some por completo, que foi o bug.
		//
		// inPlay = false: modo EDICAO, como nos outros previews.
		if (m_PreviewAnim)
			m_PreviewAnim->OnUpdate(*m_PreviewScene, ImGui::GetIO().DeltaTime, false);

		// O SOLVE roda TODO FRAME, depois do tick e por cima dele: a palette
		// que o AnimationWorld acabou de montar e substituida pela do rig.
		//
		// Ele estava caindo dentro do InitPreviewScene por engano — rodava uma
		// vez so, entao o ResetToInitial nunca sincronizava o Current com o
		// Initial que o gizmo escreve, e a FORMA do controle ficava parada
		// enquanto o gizmo se afastava sozinho.
		SolveRigIntoPreview();

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

	// ── O SOLVE ──────────────────────────────────────────────────────────────
	//
	// Aqui o rig deixa de ser desenho e vira deformacao.
	//
	// ── POR QUE PARTIR DO REPOUSO, E NAO DE UMA ANIMACAO ─────────────────
	//
	// No editor de rig o que interessa e o efeito do SEU grafo, isolado. Se o
	// solve rodasse por cima de um clipe tocando, voce nunca saberia qual
	// parte do movimento e sua e qual e da animacao. Por isso partimos da pose
	// de repouso todo frame — e o mesmo padrao do editor da Unreal.
	//
	// Quando o rig for chamado de DENTRO do AnimGraph (a etapa seguinte), o
	// unico ponto que muda e este: em vez de ResetToInitial, entra
	// ApplyPose(pose que veio da animacao). O resto do caminho e identico.
	void ControlRigWindow::SolveRigIntoPreview()
	{
		if (!m_Asset || !m_Skeleton || !m_PreviewScene || m_PreviewEntity == entt::null)
			return;

		const auto& skeleton = m_Skeleton->GetSkeleton();

		if (!skeleton)
			return;

		auto& reg = m_PreviewScene->GetRegistry();
		auto* sk = reg.try_get<SkeletalMeshComponent>(m_PreviewEntity);

		if (!sk)
			return;

		RigHierarchy& h = m_Asset->GetHierarchy();

		// Todo frame parte do repouso. Sem isto o resultado do frame anterior
		// vira entrada do proximo e o rig "escorre" — um Set Transform que
		// soma 1cm somaria 60cm por segundo.
		h.ResetToInitial();

		RigExecContext ctx;
		ctx.Hierarchy = &h;
		ctx.Skel = skeleton.get();
		ctx.DeltaTime = ImGui::GetIO().DeltaTime;

		// A MESMA transformacao da entidade do preview, pra que nos que falam
		// com o mundo (Ground Trace) enxerguem a escala certa.
		ctx.WorldTransform =
			glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, m_PreviewOffsetY, 0.0f))
			* glm::scale(glm::mat4(1.0f), glm::vec3(m_PreviewScale));

		// Preview roda numa cena SEM fisica propria: um raycast aqui cairia no
		// mundo da cena principal e responderia bobagem. O Ground Trace
		// devolve "nao acertou" e o grafo segue sem quebrar.
		ctx.AllowWorldQueries = false;

		// ...mas o Ground Trace ganha um CHAO VIRTUAL em Y = 0 (o grid do
		// preview). Sem ele o no devolvia zero e qualquer grafo de Foot IK
		// ficava morto aqui dentro, sem nenhuma pista do motivo.
		ctx.UseEditorGround = true;

		m_Asset->GetGraph().Execute(ctx, "ForwardsSolve");

		// Hierarquia -> Pose -> matrizes de skinning.
		//
		// FromBindPose primeiro porque a hierarquia pode nao ter TODOS os
		// ossos do esqueleto (um osso adicionado depois, ainda nao
		// sincronizado): os que faltarem ficam na bind pose em vez de virem
		// com lixo.
		Pose::FromBindPose(*skeleton, m_RigPose);
		h.WritePose(*skeleton, m_RigPose);

		AnimationSampler::BuildSkinningMatrices(*skeleton, m_RigPose,
			sk->BonePalette,
			sk->ShowSkeleton ? &sk->BoneGlobals : nullptr);
	}

	// ── Gizmos dos Controls ──────────────────────────────────────────────────
	//
	// Projeta cada Control da hierarquia e desenha a forma dele por cima da
	// imagem. Devolve o indice sob o mouse, ou -1.
	int ControlRigWindow::DrawControlGizmos(const ImVec2& imgMin, const ImVec2& imgSize)
	{
		if (!m_ShowControlGizmos || !m_PreviewRenderer || !m_PreviewRenderer->m_Camera)
			return -1;

		const auto& h = m_Asset->GetHierarchy();

		const glm::mat4 vp = m_PreviewRenderer->m_Camera->GetViewProjectionMatrix();

		// A MESMA transformacao aplicada a entidade — escala E deslocamento.
		const glm::mat4 model =
			glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, m_PreviewOffsetY, 0.0f))
			* glm::scale(glm::mat4(1.0f), glm::vec3(m_PreviewScale));

		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 mouse = ImGui::GetMousePos();

		int hovered = -1;
		float hoveredDist = 1e9f;

		for (std::size_t i = 0; i < h.Size(); ++i)
		{
			const RigElement& e = h[(int)i];

			// Canal nao tem forma: e um valor, nao um objeto no espaco.
			// Desenhar um circulo pra ele so daria algo pra clicar sem efeito.
			if (e.Type != RigElementType::Control
				|| e.ValueType != RigControlValue::Transform
				|| !e.Visible)
				continue;

			// ── A MATRIZ COMPLETA DO DESENHO ─────────────────────────────
			//
			// Antes eu usava so a POSICAO (g[3]) e desenhava um circulo 2D
			// chapado. Por isso rotacionar o controle nao mudava nada na tela
			// e a escala do Shape offset nao fazia efeito: a forma nao tinha
			// orientacao nem tamanho proprio, so um raio em pixels.
			//
			// Agora os pontos da forma vivem no espaco LOCAL do controle e sao
			// projetados um a um. Rotacao, escala e offset entram de graca,
			// porque estao todos nesta matriz.
			const glm::mat4 world = model * h.GetControlShapeMatrix((int)i);

			// Projeta um ponto local -> tela. Devolve false atras da camera:
			// sem esse teste o ponto aparece ESPELHADO do lado oposto.
			bool anyBehind = false;

			auto project = [&](const glm::vec3& local, ImVec2& out) -> bool
				{
					const glm::vec4 clip = vp * world * glm::vec4(local, 1.0f);

					if (clip.w <= 0.0001f)
					{
						anyBehind = true;
						return false;
					}

					const glm::vec3 ndc = glm::vec3(clip) / clip.w;

					out = ImVec2(
						imgMin.x + (ndc.x * 0.5f + 0.5f) * imgSize.x,
						imgMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * imgSize.y);

					return true;
				};

			ImVec2 center;

			if (!project(glm::vec3(0.0f), center))
				continue;

			// Fora da tela com folga: nem desenha.
			if (center.x < imgMin.x - 400.0f || center.x > imgMin.x + imgSize.x + 400.0f ||
				center.y < imgMin.y - 400.0f || center.y > imgMin.y + imgSize.y + 400.0f)
				continue;

			const bool selected = ((int)i == m_SelectedElement);

			const ImU32 col = ImGui::ColorConvertFloat4ToU32(
				ImVec4(e.ShapeColor.r, e.ShapeColor.g, e.ShapeColor.b, selected ? 1.0f : 0.85f));

			const float thick = selected ? 2.6f : 1.6f;
			const float r = std::max(0.0001f, e.ShapeSize);

			// Guarda o quanto a forma ocupa na tela, pro hit-test.
			float screenRadius = 6.0f;

			auto note = [&](const ImVec2& p)
				{
					const float dx = p.x - center.x;
					const float dy = p.y - center.y;
					screenRadius = std::max(screenRadius, std::sqrt(dx * dx + dy * dy));
				};

			// Desenha um anel no plano definido por dois eixos locais.
			auto ring = [&](const glm::vec3& ax, const glm::vec3& ay, ImU32 c, float t)
				{
					constexpr int kSeg = 28;

					ImVec2 pts[kSeg];
					bool ok = true;

					for (int k = 0; k < kSeg; ++k)
					{
						const float a2 = (float)k / (float)kSeg * 6.2831853f;
						const glm::vec3 lp = (ax * std::cos(a2) + ay * std::sin(a2)) * r;

						if (!project(lp, pts[k])) { ok = false; break; }

						note(pts[k]);
					}

					if (ok)
						dl->AddPolyline(pts, kSeg, c, ImDrawFlags_Closed, t);
				};

			auto seg = [&](const glm::vec3& a2, const glm::vec3& b2, ImU32 c, float t)
				{
					ImVec2 pa, pb;

					if (!project(a2 * r, pa) || !project(b2 * r, pb))
						return;

					note(pa);
					note(pb);

					dl->AddLine(pa, pb, c, t);
				};

			const glm::vec3 X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);

			switch (e.Shape)
			{
			case RigControlShape::Sphere:
				// TRES aneis ortogonais. Um circulo so nunca vai parecer uma
				// esfera — era esse o "a esfera nao parece uma esfera".
				ring(X, Z, col, thick);
				ring(X, Y, col, thick);
				ring(Y, Z, col, thick);
				break;

			case RigControlShape::Box:
			{
				// 12 arestas de um cubo.
				const glm::vec3 c8[8] = {
					{-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1},
					{-1,-1, 1}, {1,-1, 1}, {1,1, 1}, {-1,1, 1} };

				const int edges[12][2] = {
					{0,1},{1,2},{2,3},{3,0},
					{4,5},{5,6},{6,7},{7,4},
					{0,4},{1,5},{2,6},{3,7} };

				for (const auto& ed2 : edges)
					seg(c8[ed2[0]], c8[ed2[1]], col, thick);

				break;
			}

			case RigControlShape::Diamond:
			{
				// Octaedro: 6 vertices, 12 arestas.
				const glm::vec3 v6[6] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };

				const int edges[12][2] = {
					{0,2},{2,1},{1,3},{3,0},
					{0,4},{4,1},{1,5},{5,0},
					{2,4},{4,3},{3,5},{5,2} };

				for (const auto& ed2 : edges)
					seg(v6[ed2[0]], v6[ed2[1]], col, thick);

				break;
			}

			case RigControlShape::Arrow:
			{
				seg(glm::vec3(0.0f), Y, col, thick);

				// Ponta em V, nos dois planos, pra a seta ser legivel de
				// qualquer angulo.
				seg(Y, glm::vec3(0.25f, 0.7f, 0.0f), col, thick);
				seg(Y, glm::vec3(-0.25f, 0.7f, 0.0f), col, thick);
				seg(Y, glm::vec3(0.0f, 0.7f, 0.25f), col, thick);
				seg(Y, glm::vec3(0.0f, 0.7f, -0.25f), col, thick);
				break;
			}

			default:
				// Circle: um anel no plano XZ (horizontal no espaco do
				// controle) — o formato de cinto que se usa em quadril e peito.
				ring(X, Z, col, thick);
				break;
			}

			if (anyBehind)
				continue;

			if (selected)
				dl->AddCircle(center, screenRadius + 5.0f, IM_COL32(255, 255, 255, 190), 0, 1.2f);

			// Hit-test pelo raio REAL que a forma ocupou na tela — antes era um
			// numero fixo, que errava feio em formas grandes ou de perfil.
			const float dx = mouse.x - center.x;
			const float dy = mouse.y - center.y;
			const float d2 = dx * dx + dy * dy;

			const float pick = std::max(10.0f, screenRadius + 4.0f);

			if (d2 <= pick * pick && d2 < hoveredDist)
			{
				hoveredDist = d2;
				hovered = (int)i;
			}
		}

		return hovered;
	}

	// ── Manipulacao ──────────────────────────────────────────────────────────
	//
	// ── O QUE O ARRASTO ESCREVE, E POR QUE ───────────────────────────────
	//
	// Escreve no INITIAL, nao no Current. O solve faz ResetToInitial no comeco
	// de cada frame, entao qualquer coisa escrita no Current seria apagada no
	// frame seguinte — voce arrastaria e o controle voltaria sozinho.
	//
	// Initial tambem e o que vai pro disco, entao a pose que voce montar
	// sobrevive ao fechar a janela. A contrapartida honesta: sem um sistema de
	// chaves/animacao para os controles, "posicao do controle" e "repouso do
	// controle" sao a mesma coisa aqui. Quando existir keyframe, este e o
	// ponto que se separa em dois.
	//
	// Usa GetInitialGlobal/SetInitialGlobal (ambos AXE_API) de proposito: a
	// decomposicao da matriz acontece DENTRO da dll, porque BoneTransform nao
	// e exportado e o editor nao pode chamar FromMatrix.
	void ControlRigWindow::DrawManipulator(const ImVec2& imgMin, const ImVec2& imgSize)
	{
		// Assume que NAO desenhou; so o caminho que chega no Manipulate liga.
		m_ManipulatorDrawn = false;

		if (!m_Asset || !m_PreviewRenderer || !m_PreviewRenderer->m_Camera)
			return;

		if (imgSize.x <= 4.0f || imgSize.y <= 4.0f)
			return;

		auto& h = m_Asset->GetHierarchy();

		if (m_SelectedElement < 0 || m_SelectedElement >= (int)h.Size())
			return;

		// So Control e Null se arrastam. Um OSSO e saida do solve: arrastar
		// nele seria sobrescrito no frame seguinte, e a impressao seria de
		// gizmo quebrado.
		if (h[m_SelectedElement].Type == RigElementType::Bone)
			return;

		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
		ImGuizmo::SetRect(imgMin.x, imgMin.y, imgSize.x, imgSize.y);

		const glm::mat4 view = m_PreviewRenderer->m_Camera->GetViewMatrix();
		const glm::mat4 proj = m_PreviewRenderer->m_Camera->GetProjectionMatrix();

		// A MESMA transformacao da entidade do preview: o gizmo tem que
		// aparecer onde o controle esta desenhado, nao onde ele estaria sem o
		// enquadramento.
		const glm::mat4 previewModel =
			glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, m_PreviewOffsetY, 0.0f))
			* glm::scale(glm::mat4(1.0f), glm::vec3(m_PreviewScale));

		glm::mat4 model = previewModel * h.GetInitialGlobal(m_SelectedElement);

		m_ManipulatorDrawn = true;

		const bool used = ImGuizmo::Manipulate(
			glm::value_ptr(view), glm::value_ptr(proj),
			(ImGuizmo::OPERATION)m_GizmoOp, ImGuizmo::LOCAL,
			glm::value_ptr(model));

		if (!used)
			return;

		// Volta pro espaco do rig antes de gravar.
		h.SetInitialGlobal(m_SelectedElement, glm::inverse(previewModel) * model);

		// O campo de rotacao do painel precisa re-derivar o Euler: o valor
		// mudou por FORA dele, e o cache ainda tem o que estava la antes.
		m_EulerOwner[0] = -1;

		MarkEdited("Move control");
	}

	void ControlRigWindow::HandlePreviewInput()
	{
		if (!m_PreviewHovered || !m_PreviewRenderer)
			return;

		ImGuiIO& io = ImGui::GetIO();

		// MESMO esquema das outras janelas: Alt + arrastar. Um preview que se
		// controla diferente dos outros forca a reaprender o dedo a cada
		// janela.
		const ImVec2 mousePos = ImGui::GetMousePos();
		static ImVec2 lastMousePos = mousePos;

		const ImVec2 raw(mousePos.x - lastMousePos.x, mousePos.y - lastMousePos.y);
		lastMousePos = mousePos;

		// Roda do mouse: zoom SEM Alt. E o gesto que todo mundo tenta primeiro,
		// e exigir modificador pra ele so gera a impressao de "nao da zoom".
		if (io.MouseWheel != 0.0f)
			m_PreviewRenderer->OnMouseZoom(io.MouseWheel);

		if (!io.KeyAlt)
			return;

		glm::vec2 delta(raw.x, raw.y);
		delta *= 0.003f;

		if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
			m_PreviewRenderer->OnMouseRotate(delta);
		else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
			m_PreviewRenderer->OnMousePan(delta);
		else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
			m_PreviewRenderer->OnMouseZoom(delta.y * 10.0f);
	}

	void ControlRigWindow::DrawPreviewWindow()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

		// Tamanho inicial util caso a janela apareca FLUTUANDO (dockspace de
		// versao anterior, ou o usuario a soltou de proposito).
		ImGui::SetNextWindowSize(ImVec2(420.0f, 380.0f), ImGuiCond_FirstUseEver);

		if (ImGui::Begin("Preview###RigPreview"))
		{
			// Barra ACIMA da imagem, num child de altura fixa. Botoes
			// sobrepostos com SetCursorPos ficam ATRAS do ImGui::Image, que e
			// desenhado depois — foi um bug real no preview do AnimGraph.
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
			ImGui::BeginChild("rig_preview_bar", ImVec2(0, 30), false);

			ImGui::Checkbox("Controls", &m_ShowControlGizmos);

			ImGui::SameLine();

			if (ImGui::Checkbox("Skeleton", &m_ShowSkeleton))
			{
				// Aplicado direto: o Sync so roda quando o esqueleto muda.
				if (m_PreviewScene && m_PreviewEntity != entt::null)
				{
					auto& reg = m_PreviewScene->GetRegistry();

					if (auto* sk = reg.try_get<SkeletalMeshComponent>(m_PreviewEntity))
						sk->ShowSkeleton = m_ShowSkeleton;
				}
			}

			ImGui::SameLine();
			ImGui::TextDisabled("|");
			ImGui::SameLine();

			// T / R / S como no resto do editor — mesma tecla, mesmo lugar.
			auto opBtn = [&](const char* label, int op, const char* tip)
				{
					const bool active = (m_GizmoOp == op);

					if (active)
						ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.75f, 1.0f));

					if (ImGui::Button(label, ImVec2(24.0f, 0.0f)))
						m_GizmoOp = op;

					if (active)
						ImGui::PopStyleColor();

					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", tip);

					ImGui::SameLine();
				};

			opBtn("T", 7, "Mover");
			opBtn("R", 120, "Rotacionar");
			opBtn("S", 896, "Escalar");

			ImGui::TextDisabled("|  Alt + drag = camera  |  wheel = zoom");

			if (m_SelectedElement >= 0 &&
				m_SelectedElement < (int)m_Asset->GetHierarchy().Size())
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "|  %s",
					m_Asset->GetHierarchy()[m_SelectedElement].Name.c_str());
			}

			ImGui::EndChild();
			ImGui::PopStyleVar();

			// ── A imagem ─────────────────────────────────────────────────
			const ImVec2 avail = ImGui::GetContentRegionAvail();

			m_PreviewSize = ImVec2(std::max(16.0f, avail.x), std::max(16.0f, avail.y));

			const ImVec2 imgMin = ImGui::GetCursorScreenPos();

			if (m_PreviewFramebuffer)
			{
				const uint64_t tex = (uint64_t)m_PreviewFramebuffer->GetColorAttachmentRendererID(0);

				ImGui::Image((ImTextureID)tex, m_PreviewSize, ImVec2(0, 1), ImVec2(1, 0));
			}
			else
			{
				ImGui::Dummy(m_PreviewSize);
			}

			m_PreviewHovered = ImGui::IsItemHovered();

			// Gizmos por CIMA da imagem.
			const int hovered = DrawControlGizmos(imgMin, m_PreviewSize);

			// ── O MANIPULADOR VEM ANTES DO TESTE DE SELECAO ──────────────
			//
			// Nao e detalhe de arrumacao: o ImGuizmo tem UM contexto GLOBAL,
			// compartilhado com o viewport principal do editor. Perguntar
			// IsOver() antes de desenhar o nosso gizmo le o estado que sobrou
			// — do frame anterior, ou pior, do gizmo de OUTRA JANELA.
			//
			// Era isso que travava a selecao: com uma entidade selecionada na
			// cena principal, o IsOver() respondia sobre o gizmo DELA e
			// recusava todo clique aqui, mesmo em cima do controle.
			DrawManipulator(imgMin, m_PreviewSize);

			// E so confiamos na resposta se HOUVER um gizmo nosso na tela.
			const bool overGizmo = m_ManipulatorDrawn
				&& (ImGuizmo::IsOver() || ImGuizmo::IsUsing());

			// Clique simples (sem Alt, que e da camera) seleciona o controle.
			// O teste do gizmo evita que clicar numa seta que passa por cima de
			// outro controle selecione o outro e comece a arrastar o errado.
			if (m_PreviewHovered && hovered >= 0 && !overGizmo &&
				ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyAlt)
			{
				m_SelectedElement = hovered;
				m_SelectedNode = -1;
			}

			if (hovered >= 0 && !overGizmo)
				ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

			HandlePreviewInput();
		}

		ImGui::End();
		ImGui::PopStyleVar();
	}

} // namespace axe