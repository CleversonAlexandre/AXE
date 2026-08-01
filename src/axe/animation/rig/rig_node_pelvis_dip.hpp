#pragma once
#include "axe/core/types.hpp"
#include "rig_node_base.hpp"

#include <memory>

namespace axe
{
	// ── Pelvis Dip ───────────────────────────────────────────────────────────
	//
	// ABAIXA a raiz do corpo pra que o pe MAIS BAIXO consiga alcancar o chao.
	//
	// ── O PROBLEMA QUE ELE RESOLVE ───────────────────────────────────────
	//
	// Numa rampa, os dois pes pedem coisas OPOSTAS: o de cima precisa que a
	// perna ENCURTE (o joelho dobra, e o Two Bone IK faz isso bem) e o de baixo
	// precisa que a perna ESTIQUE. Uma perna de Mixamo em idle ja esta a ~98%
	// da extensao, entao nao ha o que esticar: o clamp de alcance do Two Bone
	// IK satura, o membro fica RETO e o pe NAO chega no chao — em silencio,
	// porque o no rodou e resolveu, so nao chegou onde foi mandado.
	//
	// O sintoma classico e "uma perna faz IK e a outra nao", com os DOIS grafos
	// identicos. Nao e espelhamento nem erro de fio: e a perna de baixo.
	//
	// ── A SOLUCAO ────────────────────────────────────────────────────────
	//
	// A mesma do AnimNode_FootIK: desce o quadril pelo deficit do pe mais
	// baixo. O de baixo passa a alcancar, o de cima dobra mais — que e
	// exatamente a postura de quem esta de pe numa rampa.
	//
	// SO DESCE, nunca sobe. Levantar o quadril pra "alcancar" um pe alto faz o
	// personagem flutuar, e o pe alto ja e resolvido dobrando o joelho.
	//
	// Rode-o ANTES dos Two Bone IK das duas pernas.
	//
	// ── SOBRE ESTE ARQUIVO ───────────────────────────────────────────────
	//
	// A convencao do projeto e que todo no mora em rig_nodes.{hpp,cpp}. Este
	// esta separado por um motivo pratico: quem escreveu o patch nao tinha os
	// arquivos originais inteiros, e um arquivo novo nao corre risco de apagar
	// codigo existente. Quando quiser, mova a classe pra rig_nodes.* e apague
	// este par — nada mais no sistema referencia estes arquivos por nome.
	class AXE_API RigNode_PelvisDip : public RigNode
	{
	public:
		RigNode_PelvisDip();

		const char* TypeName() const override { return "PelvisDip"; }

		std::unique_ptr<RigNode> Clone() const override
		{
			return std::make_unique<RigNode_PelvisDip>(*this);
		}

		void Execute(RigExecContext& ctx) override;

		// Serialize / Deserialize NAO sao sobrescritos de proposito.
		//
		// Todo o estado configuravel do no vive em PINOS, e a classe base ja
		// cuida deles. m_Dip e m_Init sao estado de runtime — gravar a
		// suavizacao de um frame no .axerig seria errado, e um override vazio
		// so seria ruido.

	private:
		// Estado de suavizacao, mesmo padrao do RigNode_DampFloat: o primeiro
		// solve entra DIRETO no valor em vez de subir de zero, senao o
		// personagem afunda visivelmente no primeiro frame do Play.
		float m_Dip = 0.0f;
		bool  m_Init = false;

		// Aviso one-shot de item nao resolvido. Sem ele, um nome de osso
		// errado deixa o no MUDO — a falha que este proprio no existe pra
		// tornar visivel.
		bool  m_WarnedNoPelvis = false;
	};

} // namespace axe