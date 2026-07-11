#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include <memory>
#include <vector>
#include <string>

namespace axe
{
	class Texture3D;

	// Máximo de Probe Volumes simultâneos por cena — 2 grids x 4 samplers
	// ocupam os texture units 16-23 do lighting pass (24-27 são das
	// Reflection Probes). Volumes além do limite continuam bakeando, mas
	// não são enviados ao shader.
	inline constexpr int kMaxProbeVolumes = 2;

	// ── Probe Volume (Irradiance Volume / Light Probes) ─────────────────────
	//
	// Grid 3D de "light probes" dentro de uma caixa (definida pelo Transform
	// da entity — a ESCALA é o tamanho total, mesma convenção do Interior
	// Volume). No bake, cada probe renderiza a cena em 6 direções (mini
	// cubemap) e o resultado é projetado em Spherical Harmonics de ordem 1
	// (4 coeficientes RGB). Em runtime, o Lighting Pass interpola as probes
	// vizinhas (trilinear, de graça via sampler3D) e usa essa irradiância no
	// lugar do IBL global do céu.
	//
	// É isso que faz o "natural" da Unreal: probes dentro de uma sala fechada
	// só enxergam paredes escuras → o ambient delas é escuro → o interior
	// escurece SOZINHO, sem volume manual. Probes ao ar livre enxergam céu +
	// chão iluminado → capturam um bounce do sol. (Equivalente ao Volumetric
	// Lightmap da UE4 / Light Probe Group da Unity, versão MVP.)
	//
	// O canal alfa do coeficiente SH0 guarda a "visibilidade do céu" da
	// probe (fração do ângulo sólido que enxerga céu) — usada pra atenuar o
	// IBL especular e o ambient flat em interiores.
	//
	// NOTA: sem AXE_API de propósito — structs de dados header-only não
	// atravessam a fronteira da DLL como símbolo, só como layout (mesma
	// lição do InteriorVolume: evita __imp_??0 no editor.exe).

	// Configurações editáveis no Inspector (serializadas).
	struct ProbeVolumeSettings
	{
		bool Enabled = true;

		// Probes por eixo (2..16 por eixo). Total = X*Y*Z — cada probe
		// custa 6 renders da cena no bake, então cuidado com grids gigantes.
		glm::ivec3 Resolution{ 6, 3, 6 };

		// Multiplicador da irradiância das probes
		float Intensity = 1.0f;

		// Transição suave (m) na borda da caixa — fora dela, o lighting
		// pass volta pro IBL global do céu.
		float Feather = 1.0f;

		// Far clip dos mini renders do bake — o suficiente pra enxergar a
		// geometria relevante ao redor do volume.
		float BakeFarClip = 150.0f;

		// Número de bounces de luz do bake. 1 = sol → parede → probe;
		// 2 = sol → parede → chão → probe (luz que "dobra a esquina",
		// a assinatura visual do GI). Cada bounce roda o bake inteiro
		// de novo usando o grid anterior como ambiente — tempo de bake
		// escala linear (2 bounces = 2x). Acima de 3 o ganho visual é
		// quase imperceptível.
		int Bounces = 2;

		// Desenha os pontos das probes no viewport (gizmo)
		bool ShowProbes = false;

		// Rebake automático ao ABRIR a cena do disco QUANDO não há um
		// .axeprobes válido ao lado do .axescene (o grid é salvo lá a cada
		// save da cena e recarregado no load — cena abre com GI pronto).
		// Esta flag só age no fallback: arquivo ausente, corrompido ou de
		// resolução diferente. O Play/Stop NÃO rebakeia em nenhum caso:
		// o grid sobrevive ao snapshot.
		bool AutoBakeOnLoad = true;

		// Identidade estável do arquivo .axeprobes deste volume — gerado
		// aleatoriamente no primeiro save da cena. Com multi-volume, cada
		// volume salva o próprio grid em "Cena.<id>.axeprobes"; um ID
		// persistente sobrevive a renomes de entity e reordenação, coisa
		// que "índice na cena" ou "nome da entity" não sobrevivem.
		uint32_t FileID = 0;

		// Occlusion Probes (estilo Unity): usa a visibilidade do céu da
		// probe (SH0.a — puramente GEOMÉTRICA, independente da hora) pra
		// ocluir a luz DIRETA do sol dentro do volume, sem depender de
		// shadow map. Probe em sala fechada enxerga 0% de céu → sol morto
		// lá dentro em qualquer horário; ao ar livre enxerga ~50% (o chão
		// bloqueia o hemisfério de baixo) → sol pleno. Janelas ficam numa
		// aproximação borrada (a probe é esparsa) — pra sol entrando com
		// recorte preciso pela janela, é o shadow map que resolve.
		bool OccludeSunlight = true;
	};

	// Resultado do bake — CPU-free depois de criado: só os 4 sampler3D
	// RGBA16F (via abstração Texture3D, sem GL aqui). Vive no componente
	// como shared_ptr; NÃO é serializado (rebakear ao abrir a cena).
	struct ProbeGrid
	{
		glm::ivec3 Resolution{ 0 };

		// Coeficientes SH L1 (RGB) por probe. SH0.a = visibilidade do céu.
		std::shared_ptr<Texture3D> SH0;
		std::shared_ptr<Texture3D> SH1X;
		std::shared_ptr<Texture3D> SH1Y;
		std::shared_ptr<Texture3D> SH1Z;

		// Cópia CPU dos mesmos coeficientes (RGBA float, total*4 cada) —
		// custa ~centenas de KB e permite salvar o grid em .axeprobes sem
		// readback da GPU (Texture3D dispensa um GetData).
		std::vector<float> DataSH0;
		std::vector<float> DataSH1X;
		std::vector<float> DataSH1Y;
		std::vector<float> DataSH1Z;

		bool IsValid() const { return SH0 && SH1X && SH1Y && SH1Z; }

		bool HasCPUData() const
		{
			size_t expected = (size_t)Resolution.x * Resolution.y * Resolution.z * 4;
			return expected > 0 && DataSH0.size() == expected
				&& DataSH1X.size() == expected
				&& DataSH1Y.size() == expected
				&& DataSH1Z.size() == expected;
		}
	};

	// ── IO do .axeprobes ─────────────────────────────────────────────────
	// Formato binário simples e estável:
	//   char[8]  magic   = "AXEPROBE"
	//   uint32   version = 1
	//   int32[3] resolution (x, y, z)
	//   float[total*4] x 4 blocos: SH0, SH1X, SH1Y, SH1Z (crus)
	// Salvo pelo SceneSerializer::Serialize ao lado do .axescene; lido pelo
	// Deserialize — se existir e a resolução bater com as Settings, a cena
	// abre com o GI pronto, sem rebake. Implementação em probe_grid_io.cpp.
	AXE_API bool SaveProbeGridToFile(const std::string& path, const ProbeGrid& grid);
	AXE_API std::shared_ptr<ProbeGrid> LoadProbeGridFromFile(const std::string& path);

	// Dados por frame consumidos pelo Lighting Pass — montados pelo
	// SceneCollector. MVP: UM volume ativo por cena (o primeiro encontrado).
	// Dados por frame consumidos pelo Lighting Pass — montados pelo
	// SceneCollector. MVP: UM volume ativo por cena.
	//
	// Grid é shared_ptr (não ponteiro cru!) de propósito: o bake pode
	// acontecer NO MESMO FRAME em que este Collect rodou (pedido pelo
	// Inspector), e quando isso acontece o SceneRenderer REATRIBUI o
	// shared_ptr do componente pro grid novo — destruindo o antigo. Um
	// ponteiro cru pra dentro do componente ficaria pendurado (dangling)
	// nesse exato frame, e o Lighting Pass, rodando depois, crasharia lendo
	// memória já liberada (foi exatamente o bug do 2º bake: ele sempre
	// derrubava o resultado do 1º e o Execute() acessava memória já
	// destruída). Guardando um shared_ptr aqui, a RenderQueue mantém o
	// grid ANTIGO vivo até o fim deste frame — renderiza com o grid de
	// antes do bake (correto: o novo ainda nem terminou), e o próximo
	// frame já pega o resultado atualizado.
	struct ProbeVolumeData
	{
		glm::mat4 WorldToLocal{ 1.0f }; // sem escala (escala → HalfExtents)
		glm::vec3 HalfExtents{ 1.0f };
		float     Intensity = 1.0f;
		float     Feather = 1.0f;
		bool      OccludeSunlight = true;
		std::shared_ptr<const ProbeGrid> Grid;
	};

	// Pedido de bake — enfileirado pelo SceneCollector quando o usuário
	// clica em "Bake" no Inspector; executado pelo SceneRenderer (que tem o
	// contexto gráfico) via ProbeBakePass. Target aponta pro shared_ptr do
	// componente onde o resultado deve ser escrito — é seguro porque não há
	// mudança estrutural no registry entre o Collect e o Render do mesmo
	// frame (mesma garantia que o RenderQueue já assume pros ponteiros de
	// Mesh/Material).
	struct ProbeBakeRequest
	{
		bool       Requested = false;
		glm::mat4  LocalToWorld{ 1.0f }; // rotação+translação, sem escala
		glm::vec3  HalfExtents{ 1.0f };
		glm::ivec3 Resolution{ 4 };
		float      FarClip = 150.0f;
		int        Bounces = 2;
		std::shared_ptr<ProbeGrid>* Target = nullptr;
	};

} // namespace axe