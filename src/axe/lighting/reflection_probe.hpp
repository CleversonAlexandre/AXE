#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include <memory>
#include <cstdint>

namespace axe
{
	// ── Reflection Probe ─────────────────────────────────────────────────
	// Cubemap HDR local capturado na POSIÇÃO da entity e pré-filtrado
	// (mips GGX por roughness) — substitui o reflexo do céu no especular
	// dentro da caixa de influência (Transform: posição = ponto de
	// captura, ESCALA = tamanho da caixa em metros). É o irmão especular
	// das Light Probes: as SH L1 resolvem o difuso, mas o reflexo dentro
	// de uma sala vinha do céu atenuado — um cubemap capturado no centro
	// da sala mostra as PAREDES da sala no reflexo, como deve ser.
	//
	// Limite: 4 probes simultâneas na cena (texture units 24-27 do
	// lighting pass). Sobrepostas, combinam por peso com feather.
	struct AXE_API ReflectionProbeSettings
	{
		bool Enabled = true;

		// Resolução da face do cubemap de captura (o prefilter gera mips
		// a partir dela). 128 é o padrão da indústria pra probes locais.
		int Resolution = 128;

		// Multiplicador do reflexo local
		float Intensity = 1.0f;

		// Transição na borda da caixa de volta pro reflexo do céu (m)
		float Feather = 0.5f;

		// Box projection (parallax correction): trata o cubemap como se
		// estivesse "colado" nas paredes da caixa — o reflexo fica
		// ancorado no lugar certo em vez de "infinitamente longe". É o
		// que faz reflection probe local de interior parecer correta.
		bool BoxProjection = true;

		// Far clip da captura
		float BakeFarClip = 150.0f;
	};

	// Resultado da captura — interface abstrata: o dono real do cubemap
	// pré-filtrado é a implementação da API gráfica (OpenGL etc.), e o
	// resto do engine só transporta o shared_ptr. GetPrefilteredID()
	// devolve um id OPACO que só a camada da mesma API consome (mesmo
	// contrato dos ssaoTextureID/shadowMapID que já cruzam o Execute).
	class AXE_API ReflectionCapture
	{
	public:
		virtual ~ReflectionCapture() = default;
		virtual uint32_t GetPrefilteredID() const = 0;
		virtual bool IsValid() const = 0;
	};

	// Dados por frame consumidos pelo Lighting Pass — shared_ptr pelo
	// mesmo motivo do ProbeGrid: um rebake no meio do frame reatribui o
	// Capture do componente, e a cópia na RenderQueue mantém o antigo
	// vivo até o fim do frame (lição do crash do 2º bake do GI).
	struct ReflectionProbeData
	{
		glm::mat4 WorldToLocal{ 1.0f }; // sem escala (escala → HalfExtents)
		glm::vec3 HalfExtents{ 1.0f };
		glm::vec3 Position{ 0.0f };     // ponto de captura (mundo)
		float     Intensity = 1.0f;
		float     Feather = 0.5f;
		bool      BoxProjection = true;
		std::shared_ptr<const ReflectionCapture> Capture;
	};

	// Pedido de captura — enfileirado pelo SceneCollector (botão "Bake"
	// ou load de cena), executado pelo SceneRenderer. Barato: 6 renders +
	// prefilter (~dezenas de ms), por isso o load sempre rebakeia em vez
	// de serializar o cubemap em disco.
	struct ReflectionBakeRequest
	{
		glm::vec3 Position{ 0.0f };
		int       Resolution = 128;
		float     FarClip = 150.0f;
		std::shared_ptr<ReflectionCapture>* Target = nullptr;
	};

} // namespace axe