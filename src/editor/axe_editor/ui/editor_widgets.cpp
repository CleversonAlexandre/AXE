#include "editor_widgets.hpp"

#include <cmath>
#include <cstdio>

namespace axe::ui
{
	namespace
	{
		// As cores vivem AQUI, e nao espalhadas em cada janela. Foram tiradas
		// dos valores que ja estavam sendo digitados a mao no Script Editor e no
		// Control Rig — nao sao uma paleta nova, sao a paleta existente reunida.
		constexpr float kAddR = 0.13f, kAddG = 0.38f, kAddB = 0.35f;
		constexpr float kDangerR = 0.45f, kDangerG = 0.16f, kDangerB = 0.16f;
		constexpr float kPrimaryR = 0.20f, kPrimaryG = 0.45f, kPrimaryB = 0.75f;
		constexpr float kWarnR = 0.55f, kWarnG = 0.35f, kWarnB = 0.12f;

		// Um botao aceso precisa parecer aceso, nao so pintado. Clarear a mesma
		// cor mantem a familia e evita inventar um segundo tom pra cada estado.
		ImVec4 Lighten(const ImVec4& c, float amount)
		{
			return ImVec4(
				c.x + (1.0f - c.x) * amount,
				c.y + (1.0f - c.y) * amount,
				c.z + (1.0f - c.z) * amount,
				c.w);
		}

		// Empilha as tres cores de um botao de uma vez. Sempre TRES: sem
		// Hovered e Active proprios, o botao colorido volta ao cinza do tema no
		// instante em que o mouse encosta — e parece que quebrou.
		int PushAccent(Accent a)
		{
			if (a == Accent::Neutral)
				return 0;

			const ImVec4 base = AccentColor(a);

			ImGui::PushStyleColor(ImGuiCol_Button, base);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Lighten(base, 0.18f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, Lighten(base, 0.30f));

			return 3;
		}
	}

	ImVec4 AccentColor(Accent a)
	{
		switch (a)
		{
		case Accent::Add:     return ImVec4(kAddR, kAddG, kAddB, 1.0f);
		case Accent::Danger:  return ImVec4(kDangerR, kDangerG, kDangerB, 1.0f);
		case Accent::Primary: return ImVec4(kPrimaryR, kPrimaryG, kPrimaryB, 1.0f);
		case Accent::Warning: return ImVec4(kWarnR, kWarnG, kWarnB, 1.0f);
		default:              return ImGui::GetStyle().Colors[ImGuiCol_Button];
		}
	}

	bool AccentButton(const char* label, Accent accent, const char* tooltip,
		const ImVec2& size)
	{
		const int pushed = PushAccent(accent);

		const bool clicked = ImGui::Button(label, size);

		if (pushed)
			ImGui::PopStyleColor(pushed);

		if (tooltip && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", tooltip);

		return clicked;
	}

	bool IconButton(const char* icon, const char* tooltip, Accent accent, bool active)
	{
		// Quadrado a partir da altura da linha: acompanha a fonte e o
		// FramePadding do tema, em vez de um numero fixo que sai de escala
		// quando alguem mexer no estilo.
		const float h = ImGui::GetFrameHeight();

		const int pushed = active
			? PushAccent(accent == Accent::Neutral ? Accent::Primary : accent)
			: PushAccent(accent);

		// ── CENTRALIZACAO A MAO ──────────────────────────────────────────────
		//
		// O ImGui centraliza o rotulo pelo AVANCO do texto, nao pela area que
		// ele de fato ocupa. Pra uma palavra os dois quase coincidem; pra um
		// glifo de icone nao — a Font Awesome tem avanco maior que o desenho, e
		// o icone assenta visivelmente pra esquerda e pra baixo dentro da
		// caixa.
		//
		// Entao: botao VAZIO pra pegar o clique e o fundo, e o glifo desenhado
		// por cima, centrado pela medida real. ButtonBehavior daria no mesmo com
		// mais codigo; um botao vazio ja traz hover, active e teclado de graca.
		//
		// ── O ID SAI DO ICONE, NAO DE UM LITERAL ─────────────────────────────
		//
		// Aqui havia "##icon" fixo, e o ImGui identifica widget por ROTULO: os
		// cinco botoes da barra do rig ficavam com o MESMO id, disputando um so.
		// O sintoma era brutal e mudo — Undo, Redo e Reset pose simplesmente
		// nao respondiam ao clique.
		//
		// Com o proprio glifo como id, cada icone e unico. Dois botoes com o
		// MESMO icone na mesma janela ainda colidiriam; nesse caso, envolva com
		// ImGui::PushID/PopID no chamador, como se faz com qualquer widget.
		const ImVec2 pos = ImGui::GetCursorScreenPos();

		char id[64];
		std::snprintf(id, sizeof(id), "##ico_%s", icon);

		const bool clicked = ImGui::Button(id, ImVec2(h, h));

		const ImVec2 sz = ImGui::CalcTextSize(icon);

		// floorf: em meio pixel o glifo sai borrado, e num icone de 13px isso e
		// a diferenca entre nitido e sujo.
		const ImVec2 at(
			pos.x + floorf((h - sz.x) * 0.5f),
			pos.y + floorf((h - sz.y) * 0.5f));

		// GetColorU32 aplica o alpha do estilo corrente, entao dentro de um
		// BeginDisabled o glifo apaga junto com o botao. Um AddText com cor
		// crua deixaria o Undo desabilitado parecendo ativo — o botao cinza e a
		// seta branca, viva.
		ImGui::GetWindowDrawList()->AddText(at,
			ImGui::GetColorU32(ImGuiCol_Text), icon);

		if (pushed)
			ImGui::PopStyleColor(pushed);

		if (tooltip && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", tooltip);

		return clicked;
	}

	bool ToggleButton(const char* label, bool active, const char* tooltip,
		Accent onAccent)
	{
		const int pushed = active ? PushAccent(onAccent) : 0;

		const bool clicked = ImGui::Button(label);

		if (pushed)
			ImGui::PopStyleColor(pushed);

		if (tooltip && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", tooltip);

		return clicked;
	}

	void SectionHeader(const char* icon, const char* label, Accent accent)
	{
		const ImVec4 col = AccentColor(accent);

		ImGui::PushStyleColor(ImGuiCol_Header, col);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, col);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, col);

		char buf[128];

		if (icon && icon[0])
			std::snprintf(buf, sizeof(buf), "%s  %s", icon, label);
		else
			std::snprintf(buf, sizeof(buf), "%s", label);

		// Selectable e nao Text: e o Selectable que pinta a faixa inteira ate a
		// borda do painel. Sempre "selecionado", nunca clicavel.
		ImGui::Selectable(buf, true, ImGuiSelectableFlags_Disabled);

		ImGui::PopStyleColor(3);
	}

	void ToolbarSeparator()
	{
		ImGui::SameLine(0.0f, 8.0f);
		ImGui::TextDisabled("|");
		ImGui::SameLine(0.0f, 8.0f);
	}

} // namespace axe::ui