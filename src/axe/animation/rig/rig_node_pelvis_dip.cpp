#include "rig_node_pelvis_dip.hpp"
#include "rig_hierarchy.hpp"
#include "axe/log/log.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace axe
{
	RigNode_PelvisDip::RigNode_PelvisDip()
	{
		Title = "Pelvis Dip";
		HasExecIn = true;
		ExecOut.push_back("");

		AddInItem("Pelvis", RigElementType::Bone);

		// As duas alturas vem dos Ground Trace dos pes, em ESPACO DE
		// COMPONENTE — e o mesmo pino Height que alimenta o alvo do IK, entao
		// nao ha conversao pra fazer aqui.
		AddInFloat("Height A", 0.0f);
		AddInFloat("Height B", 0.0f);

		// 12: mesmo default do Damp Float, pra o rig inteiro responder no mesmo
		// ritmo. Mais alto = quadril mais duro, e a rampa "bate" no personagem.
		AddInFloat("Speed", 12.0f);

		// ── EM METROS ────────────────────────────────────────────────────────
		//
		// Teto de seguranca: uma normal de quina de colisor ou um trace que
		// pegou o objeto errado nao derruba o personagem no chao. Mesmo papel
		// do Max Angle do Align To Vector.
		//
		// Convertido pra espaco de componente no Execute. A licao do FOOTIK_V5:
		// parametro em metros nunca encosta em distancia de espaco de
		// componente sem conversao explicita.
		AddInFloat("Max Dip", 0.25f);

		AddInFloat("Weight", 1.0f);
	}

	void RigNode_PelvisDip::Execute(RigExecContext& ctx)
	{
		if (!ctx.Hierarchy)
			return;

		const int pelvis = ReadItem(ctx, 0);

		if (pelvis < 0)
		{
			// Politica da casa: item nao resolvido e no-op, nunca crash. Mas
			// MUDO nao — foi assim que a perna de baixo passou horas sem pista.
			if (!m_WarnedNoPelvis)
			{
				m_WarnedNoPelvis = true;

				AXE_CORE_WARN("Pelvis Dip '{}': o pino Pelvis nao casa com nenhum "
					"osso da hierarquia — o no nao fez nada.", Title);
			}

			return;
		}

		const float weight = glm::clamp(ReadFloat(ctx, 5), 0.0f, 1.0f);

		if (weight <= 0.0001f)
			return;

		// ── QUANTO DESCER ────────────────────────────────────────────────────
		//
		// O pe que MANDA e o mais baixo: se ele alcanca, o outro alcanca por
		// construcao (o de cima resolve dobrando o joelho, que o Two Bone IK
		// faz sem esforco).
		//
		// min com 0 porque o no SO DESCE. Com os dois pes acima do plano de
		// apoio o alvo e zero e o quadril volta pro lugar sozinho, suavizado.
		const float hA = ReadFloat(ctx, 1);
		const float hB = ReadFloat(ctx, 2);

		float target = std::min(0.0f, std::min(hA, hB));

		// Metros -> espaco de componente. A escala e o comprimento da coluna Y
		// da world transform; guarda contra escala zero, que estouraria a
		// divisao.
		const float scale = std::max(1e-6f,
			glm::length(glm::vec3(ctx.WorldTransform[1])));

		const float maxDipComp = std::max(0.0f, ReadFloat(ctx, 4)) / scale;

		// Clampa o ALVO, nao o valor suavizado: assim a perseguicao nunca mira
		// fora do teto e o quadril nao "puxa" contra o clamp.
		target = glm::clamp(target, -maxDipComp, 0.0f);

		const float speed = ReadFloat(ctx, 3);

		if (!m_Init)
		{
			// PRIMEIRO solve entra direto no valor. Subindo de zero, o
			// personagem afunda visivelmente no frame em que o Play comeca.
			m_Dip = target;
			m_Init = true;
		}
		else if (speed <= 0.0f)
		{
			// Suavizacao desligada = passa direto. Assim da pra comparar com e
			// sem suavizacao sem desmontar o fio.
			m_Dip = target;
		}
		else if (ctx.DeltaTime > 0.0f)
		{
			// k = 1 - exp(-speed*dt): perseguicao exponencial INDEPENDENTE do
			// frame rate, identica a do Damp Float. Um lerp de fator fixo
			// mudaria de velocidade conforme o FPS.
			const float k = 1.0f - std::exp(-speed * ctx.DeltaTime);
			m_Dip += (target - m_Dip) * k;
		}
		// dt == 0 (preview pausado): congela onde esta, nao salta.

		const float applied = m_Dip * weight;

		if (std::abs(applied) < 1e-6f)
			return;

		// O "pra cima" do MUNDO expresso em espaco de componente — e onde a
		// hierarquia de ossos vive. Mesmo calculo do Ground Trace e do
		// AnimNode_FootIK.
		const glm::mat4 toLocal = glm::inverse(ctx.WorldTransform);
		const glm::vec3 compUp = glm::normalize(
			glm::vec3(toLocal * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));

		RigHierarchy& h = *ctx.Hierarchy;

		// TRANSLADA a global, sem tocar em rotacao nem escala: o quadril desce
		// de pe. Compor uma matriz nova aqui reintroduziria erro de
		// decomposicao a cada frame.
		glm::mat4 g = h.GetGlobal(pelvis);
		g[3] += glm::vec4(compUp * applied, 0.0f);

		// propagate = true: as duas pernas descem JUNTO. E o ponto inteiro do
		// no — descer so o quadril deslocaria o corpo dos membros.
		h.SetGlobal(pelvis, g, true);
	}

} // namespace axe