#pragma once
#include "axe/core/types.hpp"
#include "axe/animation/rig/rig_hierarchy.hpp"
#include "axe/utils/glm_config.hpp"

// O header completo: o vendor traz so o json.hpp single-header, sem json_fwd.
#include <nlohmann/json.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace axe
{
	class RigGraph;

	// O RigExecContext, declarado antes da classe, carrega um callback que
	// recebe um RigNode (ver OnNodeExecuted). Por referencia, entao a declaracao
	// adiantada basta.
	class RigNode;

	// ═════════════════════════════════════════════════════════════════════════
	//  NO DE RIG — CONTROLRIG_V1
	//
	//  Aqui o grafo e IMPERATIVO, nao fluxo de dados como o AnimGraph.
	//
	//  No AnimGraph a pose e PUXADA: o Output pede a pose ao no anterior, que
	//  pede ao anterior, e assim por diante. Faz sentido la, porque so existe
	//  uma coisa fluindo e ela tem um destino unico.
	//
	//  Num rig isso nao serve. "Mova o pe, DEPOIS mova o joelho, DEPOIS
	//  alinhe o tornozelo" e uma ORDEM — e ordem nao se expressa puxando
	//  dados. Entao existem dois tipos de fio, e a diferenca entre eles e o
	//  ponto que mais confunde quem monta rig:
	//
	//    EXECUCAO (branco) — diz QUANDO cada no roda. Segue-se pra frente,
	//                        a partir do evento Forwards Solve.
	//    DADOS             — dizem DE ONDE vem cada valor. Sao PUXADOS sob
	//                        demanda, no instante em que o no que executa
	//                        precisa ler a entrada.
	//
	//  Um no sem pinos de execucao e PURO: nao tem lugar na ordem, so
	//  responde quando alguem le a saida dele. "Get Transform" e puro; "Set
	//  Transform" nao pode ser, porque a hora em que ele escreve importa.
	// ═════════════════════════════════════════════════════════════════════════

	enum class RigPinType
	{
		Exec,
		Bool,
		Float,
		Vector,
		Transform,
		Item,       // referencia a um elemento da hierarquia (nome + tipo)

		// Aceita qualquer coisa e ADOTA o tipo do primeiro fio ligado.
		//
		// Existe pro Reroute: um no de desvio que so repassa o valor nao pode
		// ter tipo fixo, senao voce precisaria de um Reroute por tipo.
		Wildcard,

		// LISTA de elementos. Existe pra um no so poder operar sobre uma cadeia
		// inteira: sem ela, rigar um braco custa tres Get + tres Set + os fios,
		// e o grafo vira um emaranhado que esconde a intencao.
		ItemArray
	};

	// Uma referencia a elemento da hierarquia. NOME + TIPO, porque Bone e
	// Control podem ter o mesmo nome e pegar o errado deforma em silencio.
	struct AXE_API RigItemRef
	{
		std::string    Name;
		RigElementType Type = RigElementType::Bone;
	};

	struct AXE_API RigPinValue
	{
		bool           Bool = false;
		float          Float = 0.0f;
		glm::vec3      Vector{ 0.0f };
		BoneTransform  Transform;

		std::string    ItemName;
		RigElementType ItemType = RigElementType::Bone;

		std::vector<RigItemRef> Items;
	};

	struct AXE_API RigPin
	{
		std::string Name;
		RigPinType  Type = RigPinType::Float;

		// Valor usado quando NADA esta ligado neste pino. E o que permite
		// digitar 0.35 direto no no em vez de arrastar um fio de uma constante
		// pra cada numerozinho.
		RigPinValue Default;
	};

	// Tudo que um no precisa saber do mundo durante o solve.
	// ── BLACKBOARD DO GAMEPLAY ───────────────────────────────────────────────
	//
	// O que o jogo sabe e o rig nao tem como descobrir sozinho: velocidade, se
	// esta no chao, o quanto o personagem esta agachado, o peso de uma mira. O
	// AnimGraph ja tem esse quadro (AnimParameters) e a State Machine ja o le
	// pra decidir transicoes — o rig so nao alcancava.
	//
	// ── SO LEITURA, DE PROPOSITO ─────────────────────────────────────────
	//
	// Nao ha Set aqui, e nao e esquecimento. O rig CONSULTA a engine e nunca a
	// comanda: a unica coisa que ele escreve e a pose. Um rig que escrevesse no
	// blackboard viraria logica de jogo no lugar errado, e o AnimGraph ja tem
	// maquina de estados pra isso.
	//
	// Trigger tambem fica de fora. Consumir um trigger e efeito colateral, e o
	// rig pode ser avaliado DUAS VEZES no mesmo frame (a corrente de execucao e
	// um pull de saida). O segundo solve encontraria o pulso ja gasto.
	//
	// ── POR QUE UMA INTERFACE, E NAO AnimParameters* ──────────────────────
	//
	// rig_node_base nao pode depender de anim_parameters: o rig e um subsistema
	// proprio, e o AnimGraph e so UM dos seus consumidores (o outro e o editor).
	// Com a interface, quem monta o contexto decide de onde os valores vem — do
	// blackboard em jogo, de valores de teste no preview, e amanha do Sequencer.
	struct AXE_API RigBlackboard
	{
		virtual ~RigBlackboard() = default;

		// Ausente devolve 0/false, nunca erro: um grafo que le uma variavel que o
		// gameplay ainda nao escreveu tem que continuar rodando.
		virtual float GetFloat(const std::string& name) const = 0;
		virtual bool  GetBool(const std::string& name) const = 0;

		// Pra interface poder avisar "esse nome nao existe" antes de o usuario
		// cacar um zero que nao devia ser zero.
		virtual bool  Has(const std::string& name) const = 0;
	};

	// ── VISAO ────────────────────────────────────────────────────────────────
	//
	// De onde a cena esta sendo observada. Nulo = ninguem informou (preview do
	// editor, ou avaliacao fora de um contexto com camera).
	//
	// ── POR QUE O RIG PRECISA DISSO ──────────────────────────────────────
	//
	// Ha coisas que so fazem sentido em relacao a QUEM OLHA: a cabeca que
	// acompanha a camera, o torso que gira conforme a mira, o rig que se
	// desliga quando o personagem esta longe demais pra alguem notar.
	//
	// Nada disso e derivavel da pose — a pose nao sabe onde esta a camera.
	//
	// ── SO LEITURA, COMO O BLACKBOARD ────────────────────────────────────
	//
	// O rig LE a camera e nunca a move. Mover a camera e trabalho de gameplay
	// ou do Sequencer; um rig que empurrasse a camera seria efeito colateral
	// numa avaliacao que pode rodar duas vezes no mesmo frame.
	struct AXE_API RigView
	{
		virtual ~RigView() = default;

		// Transform da camera em espaco de MUNDO. Quem consome converte pro
		// espaco do rig com a WorldTransform do contexto — a mesma conversao
		// que o Ground Trace ja faz.
		virtual glm::mat4 GetCameraWorld() const = 0;

		// Campo de visao vertical, em graus. Serve pra decidir LOD com base no
		// tamanho aparente, e nao so na distancia: um personagem longe com FOV
		// fechado ocupa a tela inteira.
		virtual float GetFovDegrees() const = 0;
	};

	struct AXE_API RigExecContext
	{
		RigHierarchy* Hierarchy = nullptr;
		const Skeleton* Skel = nullptr;

		// Transform do personagem no mundo. So interessa a quem consulta o
		// mundo (o trace de chao) — mesma licao do Foot IK: parametro em
		// metros nunca encosta em distancia de espaco de componente sem
		// conversao explicita, e a escala sai daqui.
		glm::mat4 WorldTransform{ 1.0f };

		// Pode consultar a fisica? Falso no preview do editor, que roda numa
		// cena isolada sem mundo fisico proprio.
		bool  AllowWorldQueries = false;

		// CHAO VIRTUAL do preview, no plano Y = 0 (o mesmo grid que voce ve).
		//
		// Sem isto o Ground Trace devolvia ZERO no editor, e qualquer grafo
		// que passasse por ele ficava morto — o Two Bone IK recebia um alvo
		// constante e o personagem nao reagia a nada. Com o plano, da pra
		// montar e conferir o Foot IK no proprio preview; a fisica de verdade
		// entra em Play.
		bool  UseEditorGround = false;

		float DeltaTime = 0.0f;

		// Necessario pros nos lerem as proprias entradas (o pull de dados).
		RigGraph* Graph = nullptr;

		// ── SUBGRAFO ─────────────────────────────────────────────────────────
		//
		// Quando este solve roda DENTRO de um no que contem um grafo, estes
		// campos apontam pro nivel de fora. E como o no Entry le os valores que
		// entraram: ele nao tem dado proprio nenhum, so repassa o que esta
		// ligado no no que o contem — e pra isso precisa alcancar o grafo de
		// fora e perguntar por aquele pino.
		//
		// Nulos no nivel raiz.
		RigExecContext* Caller = nullptr;
		int CallerNodeId = -1;

		// O que o jogo sabe e o rig nao descobre sozinho. Nulo = sem blackboard
		// (preview do editor, ou qualquer avaliacao fora do AnimGraph).
		// Ver RigBlackboard, acima.
		const RigBlackboard* Blackboard = nullptr;

		// De onde a cena esta sendo vista. Nulo = sem camera informada.
		// Ver RigView, acima.
		const RigView* View = nullptr;

		// ── ESPIAO DE EXECUCAO ───────────────────────────────────────────────
		//
		// Chamado logo DEPOIS de cada no rodar. NULO no runtime — quem liga e o
		// editor, e so enquanto o diagnostico esta aberto.
		//
		// Existe porque um solve so mostra o RESULTADO. "O quadril girou 54
		// graus" e um sintoma que atravessou o grafo inteiro, e descobrir de
		// fora qual no fez aquilo e chute; a alternativa era desligar no por no
		// e olhar, que e exatamente o que se estava fazendo a mao.
		//
		// Com o espiao, o editor fotografa a hierarquia antes de cada no e
		// compara depois: "o Two Bone IK 'Perna E' girou o Hips em 54.3 graus".
		//
		// Vale dentro de funcao tambem: o subgrafo COPIA o contexto (ver o no
		// Call), entao o callback viaja junto.
		std::function<void(const RigNode&)> OnNodeExecuted = nullptr;

		// ── BIBLIOTECA DE FUNCOES ────────────────────────────────────────────
		//
		// Como um no Call acha a definicao da funcao que ele chama.
		//
		// Um std::function e nao um ponteiro pro asset DE PROPOSITO: rig_nodes
		// nao pode depender de control_rig_asset (e o asset que contem o grafo,
		// nao o contrario), e inverter isso criaria include circular. Assim
		// quem monta o contexto — o asset, ou o AnimNode, ou o editor — resolve
		// o nome do jeito que quiser, e o no so pergunta.
		//
		// Nulo = nao ha biblioteca; todo Call vira no-op e avisa uma vez.
		std::function<RigGraph* (const std::string&)> ResolveFunction;

		// Profundidade de aninhamento. Um subgrafo que contenha a si mesmo
		// recursaria pra sempre; a guarda corta antes da pilha estourar.
		int Depth = 0;

		// Identificador desta execucao, atribuido pelo Execute mais EXTERNO e
		// herdado por todos os niveis.
		//
		// Um no que contem grafo pode ser alcancado de dois jeitos no mesmo
		// solve: pela corrente de execucao, e por alguem PUXANDO uma saida
		// dele. Sem saber se o interior ja rodou neste frame, ele rodaria duas
		// vezes — e um Ground Trace la dentro custaria o dobro de raycasts.
		std::uint64_t SolveId = 0;
	};

	// ── Serializacao de valores ──────────────────────────────────────────────
	//
	// Moraram um tempo num namespace ANONIMO dentro do control_rig_asset.cpp, o
	// que os tornava inalcancaveis pra qualquer outro arquivo. Isso passou
	// despercebido enquanto o asset era o unico a gravar grafo — deixou de ser
	// quando um no passou a poder conter um subgrafo e precisar gravar o seu.
	//
	// Ficam aqui, junto das estruturas que eles serializam, pra existir UMA
	// forma de gravar um pino. Duas divergem: uma ganha um campo, a outra nao,
	// e o arquivo que uma grava a outra le pela metade.
	AXE_API nlohmann::json SaveRigTransform(const BoneTransform& t);
	AXE_API BoneTransform  LoadRigTransform(const nlohmann::json& j);

	AXE_API nlohmann::json SaveRigPinValue(const RigPinValue& v);
	AXE_API RigPinValue    LoadRigPinValue(const nlohmann::json& j);

	// Nome e tipo dos pinos. Quase todo no declara os seus no construtor e
	// pronto — mas os de subgrafo tem pinos que dependem do que foi colapsado,
	// entao o layout precisa sobreviver ao arquivo.
	AXE_API nlohmann::json SaveRigPinLayout(const std::vector<RigPin>& pins);
	AXE_API void           LoadRigPinLayout(const nlohmann::json& j, std::vector<RigPin>& pins);

	class AXE_API RigNode
	{
	public:
		virtual ~RigNode() = default;

		virtual const char* TypeName() const = 0;
		virtual std::unique_ptr<RigNode> Clone() const = 0;

		int         Id = -1;
		std::string Title;

		float EditorX = 0.0f;
		float EditorY = 0.0f;

		std::vector<RigPin> Inputs;
		std::vector<RigPin> Outputs;

		// Tem pino de execucao de ENTRADA? (o evento Forwards Solve nao tem:
		// ele e o comeco da corrente.)
		bool HasExecIn = false;

		// Nomes dos pinos de execucao de SAIDA. Um no comum tem um so, sem
		// nome; o Sequence tem varios (A, B, C...); um Branch tem dois.
		std::vector<std::string> ExecOut;

		// Sem execucao nenhuma = no PURO. Ver o cabecalho.
		bool IsPure() const { return !HasExecIn && ExecOut.empty(); }

		// Roda o no. So faz sentido pra nos nao-puros.
		virtual void Execute(RigExecContext& ctx) { (void)ctx; }

		// Produz o valor de uma saida de dados. Chamado sob demanda pelo pull.
		virtual void EvalOutput(RigExecContext& ctx, int pin, RigPinValue& out)
		{
			(void)ctx; (void)pin; (void)out;
		}

		// Este no dispara as PROPRIAS saidas de execucao dentro do Execute?
		//
		// Quem devolve true (so o Sequence) fica responsavel por rodar TODAS
		// as suas correntes, na ordem que quiser — e o percurso externo para
		// nele em vez de seguir a saida 0, senao aquela corrente rodaria duas
		// vezes.
		virtual bool HandlesOwnFlow() const { return false; }

		// Chamado quando um fio chega num pino Wildcard: o no assume o tipo
		// concreto. Ninguem alem do Reroute precisa disto.
		virtual void AdoptType(RigPinType t) { (void)t; }

		virtual void Serialize(nlohmann::json& j) const { (void)j; }
		virtual void Deserialize(const nlohmann::json& j) { (void)j; }

	protected:
		// Atalhos de declaracao de pino, pra os construtores dos nos ficarem
		// legiveis.
		void AddIn(const std::string& name, RigPinType type);
		void AddInFloat(const std::string& name, float def);
		void AddInBool(const std::string& name, bool def);
		void AddInItem(const std::string& name, RigElementType type);
		void AddOut(const std::string& name, RigPinType type);

		// Le uma entrada: segue o fio de dados se houver, senao devolve o
		// Default do pino.
		RigPinValue Read(RigExecContext& ctx, int pin) const;

		float     ReadFloat(RigExecContext& ctx, int pin) const;
		bool      ReadBool(RigExecContext& ctx, int pin) const;
		glm::vec3 ReadVector(RigExecContext& ctx, int pin) const;

		// Resolve um pino Item para indice na hierarquia. Devolve -1 se nao
		// casar — os nos tratam isso como "nao faca nada", nunca como crash.
		int ReadItem(RigExecContext& ctx, int pin) const;
	};

	// Fabrica por nome de tipo, usada pelo carregador do .axerig.
	AXE_API std::unique_ptr<RigNode> CreateRigNode(const std::string& typeName);

} // namespace axe