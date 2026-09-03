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

#include "axe_editor/ui/view_gizmo.hpp"   // VIEW_GIZMO_V1
#include "control_rig_window.hpp"
#include "editor/axe_editor/rig/rig_control_gizmos.hpp"
#include "editor/axe_editor/ui/editor_widgets.hpp"

#include "axe/scene/components.hpp"
#include "axe/animation/animation_world.hpp"
#include "axe/animation/animation_sampler.hpp"
#include "editor/axe_editor/viewport_renderer.hpp"
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
		if (m_FuncEdCtx)
		{
			ed::DestroyEditor(m_FuncEdCtx);
			m_FuncEdCtx = nullptr;
		}

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
	RigHierarchy& ControlRigWindow::PreviewHierarchy()
	{
		// Reclona quando a DEFINICAO mudou. A pose que estava aqui se perde
		// junto, e isso e o certo: criar um controle ou espelhar um lado muda a
		// forma do rig, e manter uma pose antiga sobre uma hierarquia nova
		// produziria um resultado que nao corresponde a nenhum dos dois.
		if (!m_PreviewCloned || m_PreviewVersion != m_Asset->GetVersion())
		{
			m_PreviewHierarchy = m_Asset->GetHierarchy();
			m_PreviewVersion = m_Asset->GetVersion();
			m_PreviewCloned = true;
		}

		return m_PreviewHierarchy;
	}

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

		RigHierarchy& h = PreviewHierarchy();

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

		// Funcoes do asset: no preview nao ha copia de trabalho, entao o
		// resolvedor aponta direto pra biblioteca.
		m_Asset->BindFunctionLibrary(ctx);

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

		// ── O DESENHO SAIU DAQUI ─────────────────────────────────────────────
		//
		// O corpo desta funcao virou `ui::DrawRigControlGizmos`. Nao foi
		// arrumacao: era o unico lugar do editor que sabia desenhar um Control,
		// e por isso "clicar num controle do rig no viewport" ficou aberto
		// desde o comeco do Sequencer.
		//
		// Nada mudou aqui: a matriz, a projecao, as formas e o hit-test sao os
		// mesmos. O que mudou e quem mais pode chamar.
		const glm::mat4 model =
			glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, m_PreviewOffsetY, 0.0f))
			* glm::scale(glm::mat4(1.0f), glm::vec3(m_PreviewScale));

		return ui::DrawRigControlGizmos(
			ImGui::GetWindowDrawList(),
			PreviewHierarchy(),
			m_PreviewRenderer->m_Camera->GetViewProjectionMatrix(),
			model,
			imgMin,
			imgSize,
			m_SelectedElement);
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

		auto& h = PreviewHierarchy();

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

		// Em Setup o gizmo pega o REPOUSO; em Pose, onde o elemento esta DE
		// FATO agora (ja com o solve e com a pose que houver). Sao coisas
		// diferentes assim que o grafo mexe no elemento, e mostrar a errada
		// faria o gizmo aparecer longe do desenho do controle.
		glm::mat4 model = previewModel * (m_PoseMode
			? h.GetGlobal(m_SelectedElement)
			: h.GetInitialGlobal(m_SelectedElement));

		m_ManipulatorDrawn = true;

		const bool used = ImGuizmo::Manipulate(
			glm::value_ptr(view), glm::value_ptr(proj),
			(ImGuizmo::OPERATION)m_GizmoOp, ImGuizmo::LOCAL,
			glm::value_ptr(model));

		if (!used)
			return;

		// Volta pro espaco do rig antes de gravar.
		const glm::mat4 wanted = glm::inverse(previewModel) * model;

		if (m_PoseMode)
		{
			// POSE fica so no preview. E o que permite deixar o braco pra cima
			// aqui sem que o personagem em cena levante o braco junto.
			h.SetValueFromGlobal(m_SelectedElement, wanted);
		}
		else
		{
			// SETUP e DEFINICAO do rig: onde o controle repousa. Isso PRECISA
			// ir pro asset, senao nada do que voce monta se salva.
			//
			// Escreve nos DOIS: no asset porque e o molde, e no preview porque
			// senao voce so veria o resultado depois do reclone — o gizmo
			// pareceria travado enquanto arrasta.
			m_Asset->GetHierarchy().SetInitialGlobal(m_SelectedElement, wanted);
			h.SetInitialGlobal(m_SelectedElement, wanted);

			// A copia continua valida: o valor foi aplicado nos dois lados.
			m_PreviewVersion = m_Asset->GetVersion();
		}

		// O campo de rotacao do painel precisa re-derivar o Euler: o valor
		// mudou por FORA dele, e o cache ainda tem o que estava la antes.
		m_EulerOwner[0] = -1;

		MarkEdited(m_PoseMode ? "Pose control" : "Move control");
	}

	// ── Backward Solve ───────────────────────────────────────────────────────
	//
	// Roda UMA vez, a pedido. Le os ossos e encosta os controles neles, pelo
	// Set Control Pose — que escreve na pose do controle, nao no Current, e por
	// isso o resultado sobrevive ao ResetToInitial do proximo frame.
	//
	// NAO chama ResetToInitial antes: os ossos precisam estar com a pose que
	// esta na tela, que e justamente o que queremos capturar. Zerar aqui
	// encostaria os controles no repouso — o oposto do objetivo.
	void ControlRigWindow::RunBackwardSolve()
	{
		if (!m_Asset)
			return;

		RigHierarchy& h = PreviewHierarchy();

		RigExecContext ctx;
		ctx.Hierarchy = &h;
		ctx.Skel = (m_Skeleton && m_Skeleton->GetSkeleton())
			? m_Skeleton->GetSkeleton().get()
			: nullptr;

		// Zero: nao ha "entre dois frames" numa operacao pontual. Os Damp
		// congelam onde estao em vez de dar um salto proporcional a um dt que
		// nao existe.
		ctx.DeltaTime = 0.0f;

		ctx.WorldTransform =
			glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, m_PreviewOffsetY, 0.0f))
			* glm::scale(glm::mat4(1.0f), glm::vec3(m_PreviewScale));

		ctx.AllowWorldQueries = false;
		ctx.UseEditorGround = true;

		m_Asset->BindFunctionLibrary(ctx);

		m_Asset->GetGraph().Execute(ctx, "BackwardsSolve");

		MarkEdited("Backward solve");
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

			// ── Setup x Pose ─────────────────────────────────────────────────
			//
			// Sem isto voce arrasta um gizmo e nada na tela diz que aquilo
			// gravou no repouso do ASSET, visivel no jogo na hora. O botao e
			// COLORIDO em Pose de proposito: modo perigoso e modo neutro nao
			// podem parecer a mesma coisa.
			{
				// ── O TOOLTIP ANTIGO MENTIA ──────────────────────────────
				//
				// Ele dizia que a pose era "por instancia". Nao e: o Value vai
				// pro .axerig junto com o resto, entao uma pose SALVA vira o
				// padrao do asset e aparece no jogo — igual ao repouso.
				//
				// O que o modo Pose de fato garante e a separacao dos
				// SIGNIFICADOS: alinhar o controle (Initial) deixou de ser a
				// mesma coisa que posa-lo (Value), o delta do rig para de sair
				// errado, e da pra limpar a pose sem perder o alinhamento.
				//
				// O isolamento por personagem so existe quando o Sequencer
				// escrever no clone de runtime. Ate la, o tooltip descreve o
				// que a engine faz hoje.
				const bool pose = m_PoseMode;

				if (ui::ToggleButton(pose ? ICON_PERSON_RUNNING "  Pose"
					: ICON_BONE "  Setup", true, nullptr,
					pose ? ui::Accent::Warning : ui::Accent::Primary))
				{
					m_PoseMode = !m_PoseMode;
				}

				// Capturado AGORA, antes de qualquer outro widget: o
				// IsItemHovered fala sempre do ULTIMO item submetido, e o aviso
				// abaixo passa a ser esse ultimo item.
				const bool hoveringButton = ImGui::IsItemHovered();

				// Aviso PERMANENTE ao lado do botao, nao so no tooltip: em Setup
				// cada arraste do gizmo altera o asset, e o asset e o mesmo
				// objeto que a cena viva usa. Quem passa por aqui sem querer
				// precisa ver o risco sem ter que procurar.
				if (!pose)
				{
					ImGui::SameLine(0.0f, 6.0f);
					ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.20f, 1.0f),
						ICON_TRIANGLE_EXCLAMATION " asset");

					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("O gizmo esta alterando a DEFINICAO do rig.\n"
							"Isso vai pro arquivo e aparece na cena em execucao.");
					}
				}

				if (hoveringButton)
				{
					ImGui::SetTooltip(pose
						? "POSE — o gizmo escreve a POSE do controle.\n"
						"Nao toca no repouso, e da pra limpar depois\n"
						"(botao 'Clear pose', no Details).\n\n"
						"ATENCAO: uma pose SALVA vai pro asset e aparece\n"
						"no jogo, como o repouso. Isso muda quando o\n"
						"Sequencer existir.\n\n"
						"Clique pra voltar a Setup."
						: "SETUP — o gizmo escreve o REPOUSO do controle.\n"
						"Vai pro asset, vale pra todas as instancias e\n"
						"aparece no jogo na hora. E o modo de MONTAR o rig.\n\n"
						"Pra alinhar so o DESENHO do gizmo, use o\n"
						"Shape offset no Details — nao o repouso.\n\n"
						"Clique pra passar a Pose.");
				}

				ImGui::SameLine();
			}

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
				m_SelectedElement < (int)PreviewHierarchy().Size())
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "|  %s",
					PreviewHierarchy()[m_SelectedElement].Name.c_str());
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
			// VIEW_GIZMO_V1 — o gizmo de navegacao entra na mesma guarda do
			// ImGuizmo, e pelo mesmo motivo: sem ela, clicar num eixo do
			// gizmo selecionaria o controle do rig que estiver atras dele.
			if (m_PreviewHovered && hovered >= 0 && !overGizmo &&
				!ui::ViewGizmoCapturesMouse() &&
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