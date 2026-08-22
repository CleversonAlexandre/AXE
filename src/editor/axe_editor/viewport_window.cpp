#include "viewport_window.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/renderer/triangle_renderer.hpp"
#include "axe/log/log.hpp"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/glm.hpp>
#include "editor/axe_editor/viewport_renderer.hpp"

// A barra do viewport era o unico lugar do editor que ainda desenhava
// ImGui::Button com texto puro e cores digitadas a mao. Ver a nota em
// editor_widgets.hpp: o custo de duplicar nao e o codigo, e a interface ficar
// com tres azuis ligeiramente diferentes.
#include "editor/axe_editor/ui/editor_widgets.hpp"
#include "editor/axe_editor/ui/editor_icons.hpp"

#include <initializer_list>

namespace axe
{
	namespace
	{
		// Largura que um botao vai ocupar, para poder ancorar um GRUPO a
		// direita/ao centro antes de desenhar o primeiro item dele.
		//
		// Medida, e nao constante: os rotulos agora levam glifo de icone, e a
		// fonte de icone e um subset carregado em runtime — chutar 60px daria
		// grupos desalinhados em qualquer DPI diferente do da maquina de quem
		// escreveu o numero.
		float BtnW(const char* label)
		{
			return ImGui::CalcTextSize(label, nullptr, true).x
				+ ImGui::GetStyle().FramePadding.x * 2.0f;
		}

		float GroupW(std::initializer_list<const char*> labels, float extra = 0.0f)
		{
			float w = 0.0f;
			const float gap = ImGui::GetStyle().ItemSpacing.x;
			for (const char* l : labels) w += BtnW(l) + gap;
			return (w > 0.0f ? w - gap : 0.0f) + extra;
		}
	}
	ViewportWindow::ViewportWindow()
	{
		//AXE_CORE_INFO("ViewportWindow created (no GPU resources yet)");
	}

	ViewportWindow::~ViewportWindow()
	{
		//AXE_CORE_INFO("ViewportWindow destroyed");
	}

	void ViewportWindow::Initialize()
	{
		if (m_Initialized)
		{
			//AXE_CORE_WARN("Viewport already initialized");
			return;
		}

		m_Camera = std::make_unique<EditorCamera>(45.0f, 1.0f, 0.1f, 100.0f);

		//AXE_CORE_INFO("Initializing ViewportWindow resources...");

		FramebufferSpecification spec;
		spec.Width = 1280;
		spec.Height = 720;
		spec.HDR = true;

		m_Framebuffer = Framebuffer::Create(spec);

		if (!m_Framebuffer)
		{
			AXE_CORE_ERROR("Failed to create framebuffer");
			return;
		}

		m_Initialized = true;

		//AXE_CORE_INFO("ViewportWindow initialized successfully");
	}



	void ViewportWindow::Draw()
	{
		//ImGui::PushStyleColor(ImGuiCol_WindowBg, (ImVec4)ImColor(0.35f, 0.3f, 0.3f));
		if (!ImGui::Begin("Viewport"))
		{
			ImGui::End();
			//ImGui::PopStyleColor(1);
			return;
		}
		DrawToolbar();

		ImVec2 viewportSize = ImGui::GetContentRegionAvail();
		uint32_t width = static_cast<uint32_t>(viewportSize.x);
		uint32_t height = static_cast<uint32_t>(viewportSize.y);

		if (width > 0 && height > 0 && (width != m_Width || height != m_Height))
		{
			OnResize(width, height);
		}


		//m_IsHovered = ImGui::IsWindowHovered();
		m_IsFocused = ImGui::IsWindowFocused();

		ImVec2 mousePos = ImGui::GetMousePos();
		m_MousePosition = { mousePos.x, mousePos.y };
		m_MouseDelta = m_MousePosition - m_LastMousePosition;
		m_LastMousePosition = m_MousePosition;

		if (m_Initialized && m_Framebuffer)
		{
			ImTextureID textureID = GetTextureID();
			if (textureID != (ImTextureID)0)
			{
				ImVec2 imagePos = ImGui::GetCursorScreenPos();

				ImGui::Image(textureID, viewportSize, ImVec2(0, 1), ImVec2(1, 0));
				m_IsHovered = ImGui::IsItemHovered();

				m_BoundsMin = { imagePos.x, imagePos.y };
				m_BoundsMax = { imagePos.x + viewportSize.x, imagePos.y + viewportSize.y };

				if (m_GuizmoCallback)
					m_GuizmoCallback(m_BoundsMin, m_BoundsMax);
			}
		}
		else
		{
			ImGui::Text("Initializing viewport...");
			m_IsHovered = false;
		}

		// Drag and drop do Asset Browser para o viewport
		// Preview visual durante o drag (tooltip com ícone + nome do asset)
		if (ImGui::BeginDragDropTarget())
		{
			// Peek sem consumir — mostra preview enquanto o mouse está no viewport
			if (const ImGuiPayload* preview = ImGui::AcceptDragDropPayload(
				"ASSET_UUID", ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
			{
				const char* uuid = (const char*)preview->Data;

				// Destaca a borda do viewport com cor de drop
				ImDrawList* dl = ImGui::GetWindowDrawList();
				dl->AddRect(ImVec2(m_BoundsMin.x, m_BoundsMin.y), ImVec2(m_BoundsMax.x, m_BoundsMax.y),
					IM_COL32(100, 180, 255, 200), 4.0f, 0, 2.5f);

				// Tooltip com nome do asset
				if (m_DragPreviewCallback)
				{
					std::string info = m_DragPreviewCallback(std::string(uuid));
					if (!info.empty())
					{
						ImGui::SetNextWindowBgAlpha(0.80f);
						ImGui::BeginTooltip();
						ImGui::TextUnformatted(info.c_str());
						ImGui::EndTooltip();
					}
				}

				// Drop confirmado (mouse solto)
				if (preview->IsDelivery())
				{
					if (m_AssetDropCallback)
					{
						ImVec2 mousePos = ImGui::GetMousePos();
						float localX = mousePos.x - m_BoundsMin.x;
						float localY = mousePos.y - m_BoundsMin.y;
						m_AssetDropCallback(std::string(uuid), localX, localY);
					}
					// Limpa ghost após o drop
					if (m_DragEndCallback) m_DragEndCallback();
				}
			}
			ImGui::EndDragDropTarget();
		}

		// Se não há drag ativo mas havia ghost, limpa
		if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left) && m_DragEndCallback)
		{
			// Verifica se algum drag estava ativo — usa flag interna
			if (m_WasDragging)
			{
				m_DragEndCallback();
				m_WasDragging = false;
			}
		}
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			m_WasDragging = true;

		ImGui::End();
		//ImGui::PopStyleColor(1);
	}

	void ViewportWindow::OnResize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0) return;
		m_Width = width;
		m_Height = height;
		if (m_Initialized && m_Framebuffer)
			m_Framebuffer->Resize(width, height);
		if (m_ViewportRenderer)
			m_ViewportRenderer->Resize(width, height);
	}

	ImTextureID ViewportWindow::GetTextureID() const
	{
		if (m_Framebuffer)
			return (ImTextureID)(uintptr_t)m_Framebuffer->GetColorAttachmentRendererID();
		return (ImTextureID)0;
	}

	// ═══════════════════════════════════════════════════════════════════════
	//  BARRA DO VIEWPORT
	//
	//  ── DUAS MUDANCAS, E POR QUE ─────────────────────────────────────────
	//
	//  1. ICONES E WIDGETS COMPARTILHADOS. Os botoes eram ImGui::Button com
	//     texto puro e seis cores digitadas a mao. O resto do editor (Sequencer,
	//     Control Rig, Anim Graph, Script) usa ui::IconButton/ToggleButton com
	//     os ICON_* da Font Awesome subsetada. A barra do viewport era a ultima
	//     ilha, e era a mais visivel de todas.
	//
	//  2. ESPACO DO GIZMO (Local/World) e as operacoes T/R/S. O espaco era
	//     LOCAL fixo dentro do viewport_renderer.cpp; T/R/S so existiam como
	//     tecla, e tecla e coisa que metade das pessoas nunca descobre.
	//
	//  ── LAYOUT ────────────────────────────────────────────────────────────
	//
	//  Ferramentas a ESQUERDA, transporte ao CENTRO. Nao e gosto: e onde a
	//  Unity e a Unreal poem, e um grupo ancorado a direita colidia com o
	//  transporte assim que a janela encolhia — que era o comportamento antigo,
	//  com os 220px fixos.
	//
	//  As larguras sao MEDIDAS (ver BtnW/GroupW). Com glifo de icone dentro do
	//  rotulo e fonte carregada em runtime, largura constante desalinha em
	//  qualquer DPI que nao seja o de quem escreveu o numero.
	// ═══════════════════════════════════════════════════════════════════════
	void ViewportWindow::DrawToolbar()
	{
		if (!m_PlayStateCallback || !m_PlayActionCallback) return;

		const int state = m_PlayStateCallback(); // 0=Edit, 1=Play, 2=Pause

		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImVec2 wpos = ImGui::GetWindowPos();
		const ImVec2 wsize = ImGui::GetWindowSize();

		const float btnH = ImGui::GetFrameHeight();
		const float startY = wpos.y + 28.0f;   // abaixo do titulo da janela

		// ── Grupo de TRANSPORTE, centralizado ────────────────────────────────
		const char* kPlay = ICON_PLAY "  Play";
		const char* kPause = ICON_PAUSE "  Pause";
		const char* kStop = ICON_STOP "  Stop";

		const float transportW = GroupW({ kPlay, kPause, kStop });
		const float transportX = wpos.x + (wsize.x - transportW) * 0.5f;

		draw->AddRectFilled(
			ImVec2(transportX - 6, startY - 4),
			ImVec2(transportX + transportW + 6, startY + btnH + 4),
			IM_COL32(30, 30, 30, 200), 4.0f);

		ImGui::SetCursorScreenPos(ImVec2(transportX, startY));

		// Play — verde quando rodando. Aceita clique tanto em Edit quanto em
		// Pause (retomar), que e o comportamento que ja existia.
		if (ui::ToggleButton(kPlay, state == 1,
			"Entrar no modo Play (ou retomar de Pause)", ui::Accent::Add))
		{
			if (state == 0 || state == 2) m_PlayActionCallback(0);
		}
		ImGui::SameLine();

		// Pause — ambar quando pausado, e so responde durante o Play.
		if (ui::ToggleButton(kPause, state == 2,
			"Pausar a simulacao", ui::Accent::Warning) && state == 1)
		{
			m_PlayActionCallback(1);
		}
		ImGui::SameLine();

		// Stop — desabilitado no modo editor: nao ha o que parar, e um botao
		// que aceita clique sem fazer nada e pior que um botao apagado.
		if (state == 0)
		{
			ImGui::BeginDisabled(true);
			ui::AccentButton(kStop, ui::Accent::Neutral);
			ImGui::EndDisabled();
		}
		else if (ui::AccentButton(kStop, ui::Accent::Danger,
			"Parar e restaurar a cena de edicao"))
		{
			m_PlayActionCallback(2);
		}

		// Aviso visual durante Play — so desenho, nao afeta o layout.
		if (state == 1 || state == 2)
		{
			const char* warn = "Modo Play - modificacoes serao descartadas ao dar Stop";
			const float warnW = ImGui::CalcTextSize(warn).x + 16.0f;
			const float warnX = wpos.x + (wsize.x - warnW) * 0.5f;
			const float warnY = startY + btnH + 6.0f;

			draw->AddRectFilled(
				ImVec2(warnX, warnY),
				ImVec2(warnX + warnW, warnY + ImGui::GetTextLineHeight() + 6.0f),
				IM_COL32(160, 70, 0, 200), 3.0f);
			draw->AddText(
				ImVec2(warnX + 8, warnY + 3),
				IM_COL32(255, 220, 100, 255), warn);
		}

		// ── Grupo de FERRAMENTAS, a esquerda ─────────────────────────────────
		if (m_ViewportRenderer)
		{
			const char* kMove = ICON_ARROWS;
			const char* kRot = ICON_ROTATE;
			const char* kScale = ICON_EXPAND;
			const char* kLocal = ICON_CUBE "  Local";
			const char* kWorld = ICON_BORDER_ALL "  World";
			const char* kGrid = ICON_TABLE_CELLS "  Grid";
			const char* kSnap = ICON_MAGNET "  Snap";

			auto& vr = *m_ViewportRenderer;
			const bool snapOn = vr.SnapEnabled;

			// O campo de valor do snap so existe quando o snap esta ligado —
			// por isso entra na medida condicionalmente, senao o grupo "reserva"
			// espaco de um widget que nao esta la.
			const float toolsW = GroupW({ kMove, kRot, kScale, kLocal, kWorld, kGrid, kSnap },
				snapOn ? 90.0f + ImGui::GetStyle().ItemSpacing.x : 0.0f)
				+ 48.0f;   // os dois separadores

			const float toolsX = wpos.x + 8.0f;

			// Se nao ha largura para os dois grupos lado a lado, as ferramentas
			// cedem: perder o Play por sobreposicao seria muito pior que perder
			// os toggles, que tem tecla equivalente.
			const bool roomForTools =
				(toolsX + toolsW + 16.0f) < transportX;

			if (roomForTools)
			{
				draw->AddRectFilled(
					ImVec2(toolsX - 6, startY - 4),
					ImVec2(toolsX + toolsW + 6, startY + btnH + 4),
					IM_COL32(30, 30, 30, 200), 4.0f);

				ImGui::SetCursorScreenPos(ImVec2(toolsX, startY));

				// Operacao do gizmo. As mesmas teclas T/R/S do editor_layer —
				// os botoes nao substituem o atalho, dao a ele um rosto.
				if (ui::ToggleButton(kMove, vr.m_GuizmoOperation == ImGuizmo::TRANSLATE,
					"Mover (T)"))
					vr.m_GuizmoOperation = ImGuizmo::TRANSLATE;
				ImGui::SameLine();

				if (ui::ToggleButton(kRot, vr.m_GuizmoOperation == ImGuizmo::ROTATE,
					"Girar (R)"))
					vr.m_GuizmoOperation = ImGuizmo::ROTATE;
				ImGui::SameLine();

				if (ui::ToggleButton(kScale, vr.m_GuizmoOperation == ImGuizmo::SCALE,
					"Escalar (S)"))
					vr.m_GuizmoOperation = ImGuizmo::SCALE;

				ui::ToolbarSeparator();   // ja faz SameLine dos dois lados

				// ── ESPACO DO GIZMO ──────────────────────────────────────────
				//
				// Em SCALE o ImGuizmo ignora o modo e opera sempre em local —
				// escalar nos eixos do mundo produziria shear, que a
				// decomposicao T/R/S nao representa. Desabilitar ali e mais
				// honesto que deixar o usuario clicar em World e nao ver
				// diferenca nenhuma.
				const bool spaceMatters = (vr.m_GuizmoOperation != ImGuizmo::SCALE);
				ImGui::BeginDisabled(!spaceMatters);

				if (ui::ToggleButton(kLocal, vr.m_GuizmoMode == ImGuizmo::LOCAL,
					"Eixos do PROPRIO alvo.\n"
					"E o que se quer para animar: 'dobra o cotovelo' e o eixo do osso."))
					vr.m_GuizmoMode = ImGuizmo::LOCAL;
				ImGui::SameLine();

				if (ui::ToggleButton(kWorld, vr.m_GuizmoMode == ImGuizmo::WORLD,
					"Eixos do MUNDO, alinhados com o grid.\n"
					"E o que se quer para posicionar: 'desce meio metro' nao depende\n"
					"de como o alvo esta girado."))
					vr.m_GuizmoMode = ImGuizmo::WORLD;

				ImGui::EndDisabled();

				if (!spaceMatters && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("Escala so existe em espaco local - o ImGuizmo\n"
						"ignora World aqui de proposito.");

				ui::ToolbarSeparator();   // ja faz SameLine dos dois lados

				if (ui::ToggleButton(kGrid, vr.ShowGrid, "Grid do chao"))
					vr.ShowGrid = !vr.ShowGrid;
				ImGui::SameLine();

				if (ui::ToggleButton(kSnap, vr.SnapEnabled,
					"Prender o gizmo a incrementos", ui::Accent::Warning))
					vr.SnapEnabled = !vr.SnapEnabled;

				// Valor do snap — o campo segue a operacao ativa, porque
				// "0.5" quer dizer meio metro em translacao e meio grau em
				// rotacao, e um numero so para os tres seria sempre errado
				// para dois deles.
				if (vr.SnapEnabled)
				{
					ImGui::SameLine();
					ImGui::SetNextItemWidth(90.0f);

					if (vr.m_GuizmoOperation == ImGuizmo::ROTATE)
						ImGui::DragFloat("##snap", &vr.SnapAngle, 1.0f, 1.0f, 90.0f, "%.0f deg");
					else if (vr.m_GuizmoOperation == ImGuizmo::SCALE)
						ImGui::DragFloat("##snap", &vr.SnapScale, 0.05f, 0.05f, 2.0f, "%.2f");
					else
						ImGui::DragFloat("##snap", &vr.SnapValue, 0.1f, 0.1f, 10.0f, "%.1f");
				}
			}
		}

		// Cursor num lugar DETERMINISTICO antes de devolver.
		//
		// Os grupos acima sao posicionados com SetCursorScreenPos, e onde o
		// cursor para depende de qual foi o ultimo widget desenhado — que agora
		// varia (o campo de snap aparece e some). Sem esta linha, a imagem do
		// viewport mudaria de altura conforme o snap estivesse ligado ou nao.
		ImGui::SetCursorScreenPos(ImVec2(
			wpos.x + ImGui::GetStyle().WindowPadding.x,
			startY + btnH + 8.0f));
	}




}