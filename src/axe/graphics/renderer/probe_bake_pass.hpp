#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/lighting/probe_volume.hpp"
#include "axe/lighting/reflection_probe.hpp"
#include "axe/renderer/render_queue.hpp"
#include "axe/scene/scene_environment.hpp"
#include <memory>

namespace axe
{
	// ── Probe Bake Pass ──────────────────────────────────────────────────────
	// Executa o bake de um Probe Volume: pra cada probe do grid, renderiza a
	// cena em 6 direções (mini cubemap 32x32 com um forward shader mínimo:
	// albedo * luz do sol com sombra + céu), lê de volta pra CPU e projeta em
	// Spherical Harmonics L1. O resultado vira 4 Texture3D (RGBA16F).
	//
	// Operação OFFLINE de editor — bloqueante, disparada pelo botão "Bake"
	// no Inspector via ProbeBakeRequest na RenderQueue. Segue o padrão
	// abstrato+OpenGL dos demais passes (nenhum GL nesta interface).
	class AXE_API ProbeBakePass
	{
	public:
		virtual ~ProbeBakePass() = default;

		virtual void Initialize() = 0;
		virtual bool IsInitialized() const = 0;

		// Renderiza e projeta todas as probes. Usa queue.Meshes como
		// geometria, queue.Light como sol (sombra própria, renderizada
		// internamente centrada no volume) e environment como céu.
		virtual std::shared_ptr<ProbeGrid> Bake(const RenderQueue& queue,
			const SceneEnvironment* environment,
			const ProbeBakeRequest& request) = 0;

		// Captura um Reflection Probe: renderiza a cena em 6 faces HDR na
		// posição pedida (reusando os shaders do bake de GI — sol, sombra
		// e, se houver, o grid de probes como ambiente) e pré-filtra o
		// resultado em mips GGX por roughness. Barato (~dezenas de ms).
		virtual std::shared_ptr<ReflectionCapture> CaptureReflection(
			const RenderQueue& queue,
			const SceneEnvironment* environment,
			const ReflectionBakeRequest& request,
			const ProbeVolumeData* giVolume = nullptr) = 0;

		static std::shared_ptr<ProbeBakePass> Create();
	};
}