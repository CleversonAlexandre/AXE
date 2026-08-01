#pragma once
#include "axe/core/types.hpp"
#include "axe/animation/rig/rig_node_base.hpp"

#include <memory>
#include <string>
#include <vector>

namespace axe
{
	// ═════════════════════════════════════════════════════════════════════════
	//  GRAFO DE RIG — CONTROLRIG_V1
	//
	//  Guarda os nos e os DOIS tipos de ligacao (execucao e dados), e sabe
	//  rodar um evento do começo ao fim.
	//
	//  Executar e simples de descrever: acha o no do evento, e vai seguindo o
	//  fio branco. O que exige cuidado e o PULL de dados no meio do caminho —
	//  ver os comentarios de EvalPin no .cpp, onde ficam as duas protecoes que
	//  importam (cache e ciclo).
	// ═════════════════════════════════════════════════════════════════════════

	// Fio de execucao: sai do pino `FromExec` do no `FromNode` e entra no no
	// `ToNode`. Nao tem indice de pino no destino porque um no so tem UM pino
	// de execucao de entrada.
	struct AXE_API RigExecLink
	{
		int FromNode = -1;
		int FromExec = 0;
		int ToNode = -1;
	};

	// Fio de dados: da saida `FromPin` do no `FromNode` para a entrada `ToPin`
	// do no `ToNode`.
	struct AXE_API RigDataLink
	{
		int FromNode = -1;
		int FromPin = 0;
		int ToNode = -1;
		int ToPin = 0;
	};

	class AXE_API RigGraph
	{
	public:
		RigGraph() = default;

		// Copia profunda — o asset guarda o molde e cada instancia roda a sua
		// propria copia, como o AnimGraph ja faz.
		RigGraph(const RigGraph& other);
		RigGraph& operator=(const RigGraph& other);

		RigGraph(RigGraph&&) = default;
		RigGraph& operator=(RigGraph&&) = default;

		// ── Estrutura ────────────────────────────────────────────────────────

		int  AddNode(std::unique_ptr<RigNode> node);

		// Insere com um Id ESCOLHIDO — usado pelo carregador do .axerig, que
		// precisa preservar os Ids gravados porque os fios referenciam por
		// eles. Empurra o contador interno pra frente: sem isso o proximo no
		// criado poderia repetir um Id restaurado, e dai um fio passaria a
		// apontar pra DOIS nos.
		int  AddNodeWithId(std::unique_ptr<RigNode> node, int id);
		void RemoveNode(int id);

		RigNode* FindNode(int id);
		const RigNode* FindNode(int id) const;

		// Primeiro no cujo TypeName bate. Serve pra perguntar "este grafo tem
		// evento Backward Solve?" sem tentar roda-lo e nao saber se o silencio
		// foi por ausencia do evento ou por a corrente nao fazer nada.
		const RigNode* FindNodeByType(const char* typeName) const;

		const std::vector<std::unique_ptr<RigNode>>& GetNodes() const { return m_Nodes; }
		std::vector<std::unique_ptr<RigNode>>& GetNodes() { return m_Nodes; }

		// Um pino de execucao de saida so pode ir para UM lugar (senao a ordem
		// deixaria de ser definida); ligar de novo substitui o anterior. Ja um
		// pino de ENTRADA de dados tambem aceita so um fio, pela mesma razao —
		// dois valores no mesmo pino nao tem desempate.
		void LinkExec(int fromNode, int fromExec, int toNode);
		void LinkData(int fromNode, int fromPin, int toNode, int toPin);

		void UnlinkExec(int fromNode, int fromExec);
		void UnlinkDataInput(int toNode, int toPin);

		const std::vector<RigExecLink>& GetExecLinks() const { return m_ExecLinks; }
		const std::vector<RigDataLink>& GetDataLinks() const { return m_DataLinks; }

		void Clear();

		// ── Serializacao ─────────────────────────────────────────────────────
		//
		// Grava e le APENAS o grafo: as chaves "nodes", "exec_links" e
		// "data_links". Nao sabe de hierarquia, nome do asset nem versao —
		// isso e assunto do ControlRigAsset, que envolve este resultado.
		//
		// Existe separado porque um grafo pode aparecer em mais de um lugar: no
		// asset, e dentro de um no que contem um subgrafo. Enquanto isto vivia
		// dentro do carregador do asset, so o asset conseguia gravar grafo.
		//
		// O formato e EXATAMENTE o de antes desta extracao — um .axerig gravado
		// pela versao anterior sai byte a byte igual.
		nlohmann::json ToJson() const;

		// LIMPA antes de carregar. Os Ids gravados sao preservados porque os
		// fios referenciam por eles.
		//
		// Fio cujas duas pontas nao existem e descartado em silencio: e o que
		// acontece quando um tipo de no sumiu da engine, e o alternativo seria
		// um grafo com fio pendurado no nada.
		void FromJson(const nlohmann::json& j);

		// ── Execucao ─────────────────────────────────────────────────────────

		// Roda a corrente de execucao a partir do no de evento cujo TypeName
		// bate com `eventType` (ex: "ForwardsSolve"). Sem esse no, nao faz
		// nada — um rig ainda vazio nao deve quebrar o personagem.
		void Execute(RigExecContext& ctx, const char* eventType);

		// Roda a corrente ligada a UM pino de execucao especifico.
		//
		// Existe pro Sequence: ele precisa rodar TODAS as suas saidas, em
		// ordem, e cada uma e uma corrente inteira. O percurso normal so segue
		// a saida 0, entao sem isto as saidas B, C... do Sequence nunca
		// rodariam.
		void RunExecPin(RigExecContext& ctx, int nodeId, int execPin);

		// Esquece tudo que ja foi calculado neste solve.
		//
		// Existe pro For Each: o cache guarda a saida de cada no puro por
		// execucao, o que e certo num grafo linear — mas dentro de um LACO o
		// elemento muda a cada volta, e sem limpar, todas as iteracoes
		// receberiam o valor da primeira.
		void InvalidateDataCache();

		// Le o valor de uma ENTRADA de dados de um no: segue o fio se houver,
		// senao devolve o Default do pino. E o pull descrito no rig_node.hpp.
		RigPinValue EvalPin(RigExecContext& ctx, int nodeId, int pin) const;

	private:
		int  FindExecTarget(int fromNode, int fromExec) const;
		void EvalOutputCached(RigExecContext& ctx, int nodeId, int pin, RigPinValue& out) const;

		std::vector<std::unique_ptr<RigNode>> m_Nodes;
		std::vector<RigExecLink>              m_ExecLinks;
		std::vector<RigDataLink>              m_DataLinks;

		int m_NextId = 1;

		// Incrementado a cada Execute do nivel raiz. Ver RigExecContext::SolveId.
		std::uint64_t m_SolveCounter = 0;

		// Orcamento de passos COMPARTILHADO por toda a execucao, inclusive as
		// correntes que o Sequence dispara. Fosse por corrente, um laco
		// escondido dentro de um Sequence escaparia do teto.
		mutable int m_Steps = 0;

		// ── Estado de UMA execucao ───────────────────────────────────────────
		//
		// mutable porque EvalPin e logicamente const: ler um pino nao muda o
		// grafo, so preenche cache.

		// Cache de saidas ja avaliadas neste solve, por (no, pino). Sem ele, um
		// no puro ligado a cinco entradas seria recalculado cinco vezes — e um
		// trace de chao recalculado cinco vezes custa cinco raycasts.
		mutable std::vector<RigPinValue> m_Cache;
		mutable std::vector<bool>        m_Cached;

		// Nos sendo avaliados AGORA na cadeia de pull. E como se detecta ciclo:
		// se um no aparece duas vezes na propria cadeia, o grafo se referencia
		// e a recursao nunca terminaria.
		mutable std::vector<bool> m_Evaluating;

		mutable bool m_CycleReported = false;
	};

} // namespace axe