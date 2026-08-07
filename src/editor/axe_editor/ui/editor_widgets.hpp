#pragma once
#include "editor_icons.hpp"

#include <imgui.h>
#include <vector>

// ═════════════════════════════════════════════════════════════════════════════
//  WIDGETS COMPARTILHADOS DO EDITOR
//
//  ── POR QUE ISTO EXISTE ──────────────────────────────────────────────────
//
//  O botao "+ Func" verde do Script Members e o do Rig Members sao o mesmo
//  widget escrito duas vezes, com as cores digitadas a mao nos dois lugares.
//  Cada janela do editor repete esse padrao: um PushStyleColor, um botao, um
//  PopStyleColor, um tooltip. Sao cinco editores.
//
//  Enquanto sao dois, duplicar e barato. Quando o mesmo verde estiver em oito
//  arquivos, mudar o verde vira uma caca — e o que acontece na pratica e que
//  ninguem muda, e a interface fica com tres verdes ligeiramente diferentes.
//
//  Isto nao e abstracao especulativa: e a extracao do que JA esta duplicado.
//
//  ── O QUE NAO ENTRA AQUI ─────────────────────────────────────────────────
//
//  Widget usado em UM lugar so. A regra pra promover algo pra ca e ter dois
//  usos reais — nao um uso e uma suposicao de que havera outro.
// ═════════════════════════════════════════════════════════════════════════════

namespace axe::ui
{
	// ── Intencao de um botao ─────────────────────────────────────────────────
	//
	// A cor comunica o QUE ACONTECE, nao a estetica. Sao poucas de proposito:
	// uma paleta aberta em cada janela e como se chega a tres verdes diferentes.
	enum class Accent
	{
		Neutral,   // acao comum: cor padrao do tema
		Add,       // cria alguma coisa (verde-agua)
		Danger,    // destroi alguma coisa (vermelho)
		Primary,   // a acao principal da janela (azul do tema)
		Warning,   // acao que muda estado global (ambar)
	};

	ImVec4 AccentColor(Accent a);

	// Botao com cor de intencao. `tooltip` nulo = sem tooltip.
	bool AccentButton(const char* label, Accent accent = Accent::Neutral,
		const char* tooltip = nullptr, const ImVec2& size = ImVec2(0, 0));

	// Botao QUADRADO so com icone, pra barra de ferramentas.
	//
	// O tooltip NAO e opcional aqui: um icone sozinho e um enigma pra quem nao
	// conhece o editor, e quem conhece nao le o tooltip mesmo. O custo de exigir
	// e uma string; o custo de esquecer e um botao que ninguem descobre.
	bool IconButton(const char* icon, const char* tooltip,
		Accent accent = Accent::Neutral, bool active = false);

	// Botao de alternancia: acende quando ligado. Pros T/R/S do gizmo, Grid,
	// Snap, Setup/Pose — tudo que tem estado visivel.
	bool ToggleButton(const char* label, bool active,
		const char* tooltip = nullptr, Accent onAccent = Accent::Primary);

	// Cabecalho de secao com faixa colorida. E o "Functions" e o "Variables" dos
	// paineis de membros.
	void SectionHeader(const char* icon, const char* label, Accent accent);

	// ── Forma de onda ────────────────────────────────────────────────────────
	//
	// Desenha o envelope de um som (a tabela de picos do AudioClip) espelhado
	// no eixo horizontal, com um cursor opcional de reproducao.
	//
	// Espelhado, e nao meia onda: e como todo editor de audio desenha, e a
	// simetria e o que faz o olho ler "isso e som" em vez de "isso e um
	// grafico de barras".
	//
	// playhead01 < 0 esconde o cursor. Nao ha animacao interna: quem chama
	// passa a posicao a cada frame, porque so ele sabe se ha voice tocando.
	void Waveform(const char* id, const std::vector<float>& peaks,
		const ImVec2& size, const ImVec4& color, float playhead01 = -1.0f);

	// Separador vertical entre grupos da toolbar. Sem ele, dez botoes em fila
	// viram uma parede indistinguivel.
	void ToolbarSeparator();

} // namespace axe::ui