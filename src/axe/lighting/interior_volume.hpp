#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"

namespace axe
{
	// ── Interior Volume ──────────────────────────────────────────────────────
	// Caixa (OBB, definida pelo Transform da entity — a ESCALA é o tamanho
	// total da caixa em metros) que bloqueia a luz "de fora" (sol direto +
	// ambient + IBL) para dentro de ambientes fechados. Resolve o clássico
	// light leaking de deferred: o shadow map bloqueia a luz direta, mas o
	// termo ambient/IBL é aplicado em TODOS os pixels da cena — então uma
	// sala fechada fica iluminada "do nada", em qualquer hora do dia.
	//
	// Point Lights, partículas e emissive NÃO são afetados — a iluminação
	// interna fica a cargo delas, como em um interior real.
	//
	// BlendDistance: a transição acontece PARA DENTRO da caixa, a partir da
	// superfície. Ou seja: posicione a face da caixa no vão da porta/janela
	// e a luz externa "vaza" suavemente por BlendDistance metros para dentro.
	// NOTA: sem AXE_API de propósito — struct de dados pura, header-only,
	// sem nenhum código fora de linha. Exportá-la faria o MSVC exigir o
	// construtor implícito importado da DLL (__imp_??0InteriorVolume) em
	// vez de gerá-lo inline no editor.exe — a mesma armadilha de dllimport
	// das janelas do editor. PODs de header não atravessam a fronteira da
	// DLL como símbolo, só como layout.
	struct InteriorVolume
	{
		bool  Enabled = true;

		// 0 = sem efeito, 1 = bloqueio total da luz externa
		float Intensity = 1.0f;

		// Distância (m) da transição suave na borda da caixa
		float BlendDistance = 0.5f;

		// O que o volume bloqueia — separado pra permitir, por exemplo,
		// manter o sol direto entrando pela janela (via shadow map) mas
		// matar só o ambient/IBL que vaza pelas paredes.
		bool AffectDirect = true;
		bool AffectAmbient = true;
	};

	// Dados por frame consumidos pelo Lighting Pass — montados pelo
	// SceneCollector a partir do Transform MUNDIAL da entity (funciona
	// com hierarquia pai→filho). Sem nenhuma dependência de ECS, seguindo
	// o mesmo padrão da RenderQueue.
	struct InteriorVolumeData
	{
		// world → espaço local da caixa, SEM escala (a escala vira
		// HalfExtents). Assim BlendDistance fica em metros de mundo,
		// uniforme em todos os eixos, mesmo com caixas não-cúbicas.
		glm::mat4 WorldToLocal{ 1.0f };

		// Metade do tamanho mundial da caixa (Scale * 0.5)
		glm::vec3 HalfExtents{ 0.5f };

		float Intensity = 1.0f;
		float BlendDistance = 0.5f;
		bool  AffectDirect = true;
		bool  AffectAmbient = true;
	};

} // namespace axe