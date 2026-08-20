#pragma once
#include "axe/core/types.hpp"
#include "axe/animation/pose.hpp"
#include "axe/animation/skeleton.hpp"
#include "axe/utils/glm_config.hpp"

#include <string>
#include <vector>

namespace axe
{
	// ═════════════════════════════════════════════════════════════════════════
	//  HIERARQUIA DE RIG — CONTROLRIG_V1
	//
	//  O Control Rig nao mexe direto na Pose. Ele opera nesta hierarquia, que
	//  contem TRES especies de elemento:
	//
	//    Bone    — espelha um osso do esqueleto. E o unico que sai daqui de
	//              volta pra Pose no fim do solve.
	//    Control — o que voce agarra no viewport. Nao existe no esqueleto: e
	//              um elemento do rig que o grafo LE pra mover ossos.
	//    Null    — um agrupador sem forma. Serve pra juntar elementos e
	//              transformar todos de uma vez (o "container" da UE).
	//
	//  Cada elemento carrega DOIS transforms, e a distincao entre eles e o
	//  coracao do sistema:
	//
	//    Initial — o valor de partida, antes de qualquer logica do grafo. E a
	//              pose de referencia: um controle sem animacao nenhuma volta
	//              pra ca, e o Setup/Construction edita ISTO.
	//    Current — o valor de agora, resultado do Forwards Solve. E o que o
	//              grafo escreve e o que vai virar pose no fim.
	//
	//  Sem essa separacao, "resetar o rig" e "saber de quanto o controle se
	//  afastou do repouso" ficam impossiveis — e as duas coisas sao usadas o
	//  tempo todo por quem monta rig.
	//
	//  ORDEM TOPOLOGICA: como no Skeleton, o pai SEMPRE tem indice menor que o
	//  filho. E o que permite recomputar todos os globais num unico loop pra
	//  frente, sem recursao e sem visitados.
	// ═════════════════════════════════════════════════════════════════════════

	enum class RigElementType
	{
		Bone,
		Control,
		Null
	};

	// Forma desenhada no viewport para um Control. Puramente visual — nao
	// muda o solve, muda o que voce consegue AGARRAR.
	enum class RigControlShape
	{
		Circle,     // anel — o classico de rotacao
		Box,
		Sphere,
		Diamond,
		Arrow
	};

	// O que um Control REPRESENTA.
	//
	// Nem todo controle move alguma coisa no espaco. Um "canal" e um controle
	// que carrega so um VALOR — o interruptor de IK/FK, a abertura da mao, a
	// intensidade de um efeito. Ele aparece na hierarquia (pra ser animado
	// depois pelo sequencer) mas nao tem forma no viewport, porque nao ha o que
	// agarrar.
	enum class RigControlValue
	{
		Transform,   // o normal: posicao/rotacao/escala
		Bool,
		Float
	};

	struct AXE_API RigElement
	{
		std::string     Name;
		RigElementType  Type = RigElementType::Bone;

		// Indice do pai neste mesmo array; -1 = raiz. Sempre MENOR que o
		// proprio indice (ver ordem topologica acima).
		int             Parent = -1;

		// Ambos LOCAIS ao pai. Ver o comentario do cabecalho.
		BoneTransform   Initial;
		BoneTransform   Current;

		// ── A POSE QUE O ANIMADOR DEU ────────────────────────────────────────
		//
		// Transform LOCAL AO PROPRIO REPOUSO deste elemento — nao ao pai.
		// Identidade = neutro, e o solve resolve Current = Initial * Value.
		//
		// Existe porque Initial acumulava DOIS papeis que nao cabem no mesmo
		// campo: "onde este controle repousa" (autorado ao montar o rig, vai
		// pro asset, vale pra todas as instancias) e "onde o animador pos ele
		// agora" (a pose, keyavel). Enquanto eram um so:
		//
		//   - alinhar o gizmo no pe mudava o repouso, e o delta que o Control
		//     Follow Bone calcula saia errado — o osso deformava;
		//   - nao havia como dizer "este pe esta PLANTADO, ignore o osso",
		//     porque plantar e justamente uma pose, nao um repouso;
		//   - e um futuro Sequencer gravaria no ASSET, aparecendo no jogo ao
		//     vivo pra todos os personagens.
		//
		// Com Value em identidade o comportamento e IDENTICO ao anterior, e
		// nenhum .axerig precisa migrar.
		//
		// ── AINDA NAO E POR INSTANCIA ────────────────────────────────────────
		//
		// Value vai pro .axerig junto com Initial, entao uma pose SALVA e a pose
		// padrao do ASSET e aparece no jogo — exatamente como o repouso. O que
		// ja esta resolvido e a separacao dos SIGNIFICADOS; o isolamento por
		// personagem chega quando o Sequencer escrever no clone de runtime (o
		// AnimNode_ControlRig ja clona hierarquia, grafo e funcoes por
		// instancia — falta so alguem escrever la).
		//
		// Nao confundir com BoolValue/FloatValue: aqueles sao o valor de um
		// controle de CANAL (um interruptor, um slider). Este e o transform.
		BoneTransform   Value;

		// ── So para Control ──────────────────────────────────────────────────
		RigControlValue ValueType = RigControlValue::Transform;

		// Valor do canal. So conta quando ValueType nao e Transform.
		//
		// Fica FORA de Initial/Current de proposito: o solve reseta transforms
		// todo frame, e um interruptor que voltasse ao padrao a cada quadro
		// seria inutil.
		bool  BoolValue = false;
		float FloatValue = 0.0f;

		// De qual osso este controle nasceu.
		//
		// Guardado pra o "Reset to bind pose" saber pra ONDE voltar. Sem isso a
		// unica opcao seria zerar pra identidade — que joga o controle pra
		// origem do rig, nao pra cima do osso dele.
		std::string SourceBone;

		// Visivel no viewport?
		//
		// E estado de RUNTIME, decidido pelo grafo a cada solve — nao vai pro
		// disco. Um controle que ficasse escondido "de fábrica" seria muito
		// dificil de descobrir depois.
		bool Visible = true;

		RigControlShape Shape = RigControlShape::Circle;
		glm::vec3       ShapeColor{ 1.0f, 0.85f, 0.15f };
		float           ShapeSize = 1.0f;

		// Deslocamento APENAS do desenho do gizmo, em espaco do proprio
		// elemento. Existe porque o ponto util pra agarrar quase nunca e o
		// pivo: o controle do pe pivota no tornozelo, mas voce quer clicar na
		// sola.
		BoneTransform   ShapeOffset;
	};

	// ── Espelhamento ─────────────────────────────────────────────────────────
	//
	// Montar metade do rig e refletir a outra e o fluxo padrao em Blender e
	// Unreal, e por um motivo pratico: o lado direito de um personagem nao e
	// trabalho novo, e trabalho REPETIDO — e trabalho repetido feito a mao
	// diverge. Um controle com raio 0.14 de um lado e 0.13 do outro nao e um
	// bug que alguem encontra; e um rig que parece torto sem explicacao.
	struct AXE_API RigMirrorSettings
	{
		// Eixo NORMAL ao plano de simetria: 0=X, 1=Y, 2=Z.
		//
		// X e o certo pra personagem em pe com Y pra cima — o plano YZ corta o
		// corpo ao meio. Os outros existem pra rig de objeto (uma porta dupla
		// espelha em Z).
		int Axis = 0;

		// Par de tokens do nome. Vazio = detecta pela tabela de convencoes
		// conhecidas (Left/Right, _L/_R, .l/.r, ...).
		std::string Search;
		std::string Replace;

		// Leva os descendentes junto. Ligado porque um controle raramente
		// anda sozinho: espelhar so o pai deixaria os filhos do outro lado.
		bool IncludeChildren = true;
	};

	class AXE_API RigHierarchy
	{
	public:
		// ── Construcao ───────────────────────────────────────────────────────

		// Devolve o indice do novo elemento, ou -1 se o nome ja existe ou o pai
		// e invalido. Nome duplicado e recusado de proposito: todo no do grafo
		// referencia elemento POR NOME, entao um nome ambiguo viraria um bug
		// silencioso de "moveu o osso errado".
		int  Add(const std::string& name, RigElementType type, int parent);

		void Clear();

		// Remove o elemento E TODOS OS DESCENDENTES, reindexando os pais dos
		// sobreviventes.
		//
		// O reindex nao e opcional: Parent e um INDICE, entao apagar alguem no
		// meio faz todo mundo abaixo apontar pro elemento errado. Sem erro,
		// sem crash — o rig so passa a deformar o osso errado. Devolve quantos
		// elementos sairam.
		//
		// Os descendentes vao junto de proposito: um Control orfao ficaria
		// pendurado na raiz, longe do corpo, sem nenhuma indicacao do porque.
		int Remove(int index);

		// Troca o PAI de um elemento, PRESERVANDO a pose — ele continua
		// exatamente onde esta na tela.
		//
		// Devolve o NOVO indice (a lista e reordenada pra manter pai antes de
		// filho) ou -1 se recusado. Recusa criar CICLO: mover um elemento pra
		// dentro do proprio galho deixaria a hierarquia sem raiz e travaria
		// todo loop que a percorre.
		int Reparent(int index, int newParent);

		// Renomeia recusando duplicata do mesmo tipo (os nos do grafo
		// referenciam por NOME).
		bool Rename(int index, const std::string& newName);

		// Todos os descendentes de `index`, em ordem crescente.
		std::vector<int> CollectDescendants(int index) const;

		// ── Espelhamento ─────────────────────────────────────────────────────

		// Troca o lado num nome: "PV_LeftLeg" -> "PV_RightLeg", "ctrl_L" ->
		// "ctrl_R". Funciona nos DOIS sentidos — quem monta pela direita nao
		// deveria precisar saber que a ferramenta tem um lado preferido.
		//
		// Devolve VAZIO quando o nome nao tem lado nenhum. E informacao util,
		// nao falha: e o que deixa a interface avisar "este elemento nao tem
		// contraparte" antes de voce clicar, em vez de criar um duplicado
		// silencioso chamado "Pelvis" numa hierarquia que ja tem "Pelvis".
		//
		// Com `search`/`replace` preenchidos, o par explicito manda.
		static std::string MirrorName(const std::string& name,
			const std::string& search = std::string(),
			const std::string& replace = std::string());

		// Cria — ou ATUALIZA, se ja existir — a contraparte espelhada.
		//
		// Atualizar em vez de recusar e o que torna a operacao REPETIVEL:
		// voce ajusta o lado esquerdo, espelha de novo, e o direito acompanha.
		// Fosse so criar, o segundo espelhamento falharia com "nome ja existe"
		// e voce teria que apagar o lado inteiro pra refazer.
		//
		// Reflete o transform INICIAL em espaco GLOBAL, nao o local. Refletir
		// o local so daria certo se toda a cadeia de pais ja estivesse
		// espelhada — e num controle pendurado na raiz, que e o caso do pole
		// vector, daria simplesmente errado.
		//
		// Bone nao se espelha: ele reflete o esqueleto, e o lado direito ja
		// existe la. Devolve o indice do espelho do PROPRIO `index`, ou -1.
		int Mirror(int index, const RigMirrorSettings& settings);

		// Copia os ossos do esqueleto para ca, preservando a hierarquia. E o
		// primeiro passo de "criar Control Rig a partir do esqueleto".
		void ImportFromSkeleton(const Skeleton& skeleton);

		// ── Voltar ao repouso conhecido ──────────────────────────────────────

		// Devolve os BONES ao repouso do .axeskel. Controls e Nulls NAO sao
		// tocados — o alinhamento deles e trabalho autoral, e apagar isso junto
		// transformaria um botao de seguranca na pior perda possivel.
		//
		// Existe porque a hierarquia do rig e uma COPIA do esqueleto: um Set
		// Transform mal ligado, ou um gizmo arrastado sem querer, faz ela
		// divergir do .axeskel — e como o asset e compartilhado com o jogo (o
		// mesmo arquivo devolve o mesmo objeto), essa divergencia aparece no
		// viewport na hora. Devolve quantos ossos foram repostos.
		int ResetBonesToBindPose(const Skeleton& skeleton);

		// ── Pose do animador (Value) ─────────────────────────────────────────
		//
		// A API e em mat4 de proposito: BoneTransform nao e exportado, entao o
		// editor nao consegue compor nem decompor por conta propria. Toda a
		// matematica acontece aqui dentro da dll.

		// Grava a pose a partir de ONDE O ELEMENTO DEVE FICAR, em espaco global
		// do rig. Resolve o Value necessario pra chegar la a partir de onde ele
		// esta agora:
		//
		//     Value' = Value * inverse(GetGlobal(index)) * wanted
		//
		// Passar pelo global atual — e nao pelo Initial — e o que faz isto
		// funcionar mesmo quando o elemento ja foi movido por outro no no mesmo
		// solve: a pose se SOMA ao que o grafo mandou, em vez de brigar com
		// ele. E o que o Backward Solve precisa pra encostar um controle num
		// osso animado sem perder o que ja estava aplicado.
		void SetValueFromGlobal(int index, const glm::mat4& wanted);

		// Volta ao neutro. E o "tirar a mao" do animador.
		void ClearValue(int index);

		// Ha pose gravada? Usado pela interface pra mostrar que o elemento saiu
		// do neutro — sem isso, um controle posado e um em repouso sao
		// indistinguiveis na tela.
		bool HasValue(int index) const;

		// Devolve UM Control/Null pra cima do osso de onde ele nasceu.
		//
		// Alinha em GLOBAL de proposito: o controle e pendurado no PAI do osso,
		// nao no osso, entao copiar o local poria ele um elo acima do lugar.
		bool ResetToSourceBone(int index);

		// ── CONTROLES SEGUEM A POSE ATUAL DOS OSSOS ──────────────────────────
		//
		//  Move todo Control/Null que nasceu de um osso para onde esse osso
		//  esta AGORA — depois do ApplyPose, ou seja, na pose da animacao — e
		//  reaplica o Value do animador por cima.
		//
		//  ── POR QUE ISTO PRECISA EXISTIR ──────────────────────────────────
		//
		//  O ResetToInitial poe todo controle no repouso do rig, que e a bind
		//  pose. Enquanto o rig roda sozinho (o preview do editor) isso esta
		//  certo: nao ha animacao, e o repouso E a pose.
		//
		//  Com animacao debaixo — que e o caso do Sequencer e o do runtime —
		//  fica errado, e de um jeito que so aparece no resultado final:
		//
		//    - um FK Chain com peso 1 copia o controle pro osso, e o membro
		//      SALTA para a T-pose no instante em que o peso sobe;
		//    - um Two Bone IK mira no controle de pe, que esta na posicao de
		//      bind, e as pernas abrem — a animacao de tiro vira um espacate.
		//
		//  Em ambos os casos o grafo esta correto e a animacao esta correta; o
		//  que esta fora do lugar sao os controles. Depois deste snap, peso 1
		//  reproduz a animacao (o controle JA esta no osso animado) e o Value
		//  do animador vira offset por cima — que e o que "ajustar a pose"
		//  quer dizer.
		//
		//  E o mesmo passo que o Sequencer da Unreal executa ao ligar um
		//  Control Rig numa sequence com animacao.
		//
		//  ── O OFFSET AUTORADO E PRESERVADO ────────────────────────────────
		//
		//  O controle nao e colado EM CIMA do osso: ele vai para a mesma
		//  posicao relativa que o autor lhe deu, agora medida a partir do osso
		//  animado. Para um controle de FK (montado sobre o osso) o offset e a
		//  identidade e da no mesmo; para um POLE VECTOR, que vive deslocado da
		//  junta, e a diferenca entre funcionar e nao funcionar — colado na
		//  junta, a direcao do polo fica degenerada e o membro torce.
		//
		//  ── A HIERARQUIA DOS CONTROLES CONTINUA VALENDO ───────────────────
		//
		//  Cada elemento recebe DUAS matrizes: o repouso (onde ele fica com
		//  Value neutro, agora acompanhando a animacao) e o atual (com o Value
		//  dele E o dos ancestrais). O local sai de
		//
		//      cur[i] = cur[pai] * (inverse(rest[pai]) * rest[i]) * Value[i]
		//
		//  Fixar cada controle no proprio osso, um por um, apagaria o movimento
		//  que o pai acabou de propagar — girar ctrl_Spine nao mexeria em
		//  ctrl_Spine1.
		//
		//  Chamar DEPOIS do ApplyPose e ANTES do solve. Elemento sem SourceBone
		//  (ou cujo osso sumiu) mantem o repouso autorado relativo ao pai.
		//
		//  Devolve quantos elementos foram ancorados num osso.
		int SnapControlsToCurrentBones();

		// ── Consulta ─────────────────────────────────────────────────────────

		std::size_t Size() const { return m_Elements.size(); }

		const std::vector<RigElement>& GetElements() const { return m_Elements; }
		std::vector<RigElement>& GetElements() { return m_Elements; }

		// Repoe a lista inteira (usado pelo undo/redo do editor).
		//
		// Existe porque escrever direto pelo GetElements() nao invalida o
		// cache de globais — a hierarquia voltaria ao estado antigo mas as
		// matrizes continuariam as de antes, e o personagem ficaria deformado
		// ate alguem mexer em outra coisa.
		void SetElements(std::vector<RigElement> elements)
		{
			m_Elements = std::move(elements);
			m_GlobalsDirty = true;
			m_MapSkeleton = nullptr;
		}

		const RigElement& operator[](int i) const { return m_Elements[i]; }
		RigElement& operator[](int i) { return m_Elements[i]; }

		// Busca exata pelo nome.
		int Find(const std::string& name) const;

		// Busca tolerante a prefixo de namespace: "LeftFoot" acha
		// "mixamorig:LeftFoot". Recusa se o sufixo for ambiguo — casar errado
		// em silencio e pior que nao casar. (Mesma politica do loader e do
		// Foot IK; ver a licao do FOOTIK_V2.)
		int FindFlexible(const std::string& name) const;

		// Busca restrita a um tipo — "o Control chamado X", nao "o osso X".
		// Bone e Control podem legitimamente ter o mesmo nome.
		int Find(const std::string& name, RigElementType type) const;

		// ── Transforms ───────────────────────────────────────────────────────
		//
		// LOCAL e a fonte da verdade; GLOBAL e derivado e fica em cache, porque
		// Get/Set em Global Space e a operacao mais comum de um rig graph.

		const BoneTransform& GetLocal(int i) const { return m_Elements[i].Current; }

		void SetLocal(int i, const BoneTransform& t);

		glm::mat4 GetGlobal(int i) const;

		// propagateToChildren = true (o normal): mexer no pai leva os filhos
		// junto, porque os locais deles nao mudam.
		//
		// false: os filhos ficam ONDE ESTAO. Custa mais — e preciso reescrever
		// o local de cada filho direto pra cancelar o movimento do pai — mas e
		// o que permite reposicionar um pivo sem arrastar o resto do corpo.
		void SetGlobal(int i, const glm::mat4& m, bool propagateToChildren = true);

		// Os mesmos, sobre o transform INICIAL.
		// Matriz COMPLETA do desenho de um Control: global do elemento ja
		// combinado com o ShapeOffset (translacao, rotacao E escala).
		//
		// Existe porque o editor NAO pode montar essa matriz: BoneTransform
		// nao e exportado da dll, entao ToMatrix() nao esta disponivel la. A
		// composicao acontece aqui dentro.
		glm::mat4 GetControlShapeMatrix(int i) const;

		glm::mat4 GetInitialGlobal(int i) const;
		void      SetInitialGlobal(int i, const glm::mat4& m);

		// ── DOIS ELEMENTOS SAO O MESMO REFERENCIAL? ──────────────────────────
		//
		// Transform LOCAL so quer dizer a mesma coisa dos dois lados de uma
		// copia se os PAIS estiverem no mesmo lugar. A pergunta aparece toda
		// vez que alguem copia controle -> osso: FK Chain, Set Transform lendo
		// um ctrl, e a auditoria do Sequencer.
		//
		// Responder por NOME (o pai do controle representa o pai do osso?) e
		// barato e erra no caso mais comum que existe: um `ctrl_RootNode`
		// criado na origem e o `RootNode` do FBX, os dois identidade, sao o
		// mesmo lugar com nomes diferentes. Comparar os globais INICIAIS
		// responde a pergunta de verdade.
		//
		// Indice negativo = espaco do mundo, que e a identidade — e por isso
		// -1 e um elemento identidade SAO o mesmo referencial.
		bool SameInitialFrame(int a, int b, float eps = 1e-3f) const;

		// ── Ciclo do solve ───────────────────────────────────────────────────

		// Current <- Initial em todos os elementos. Chamado no comeco de cada
		// Forwards Solve: o grafo sempre parte do repouso, nunca do resultado
		// do frame anterior (senao erro de arredondamento se acumula e o rig
		// "escorre" com o tempo).
		void ResetToInitial();

		// Current <- a pose de animacao, para os Bones que existirem nela. E a
		// entrada do rig quando ele roda dentro do AnimGraph: o Control Rig
		// MODIFICA a animacao, nao substitui.
		void ApplyPose(const Skeleton& skeleton, const Pose& pose);

		// Bones -> Pose. A saida do solve.
		void WritePose(const Skeleton& skeleton, Pose& pose) const;

	private:
		void EnsureGlobals() const;
		void MarkDirty() { m_GlobalsDirty = true; m_MapSkeleton = nullptr; }

		// Mapa osso-do-esqueleto -> elemento-do-rig, reconstruido so quando o
		// esqueleto ou a hierarquia mudam.
		//
		// ApplyPose e WritePose rodam TODO FRAME, um por osso. Sem este cache
		// cada um deles faria uma busca linear por nome — O(n^2) por frame,
		// com comparacao de string no meio. Num rig de 70 ossos isso ja
		// aparece no profiler.
		void EnsureBoneMap(const Skeleton& skeleton) const;

		std::vector<RigElement> m_Elements;

		// Cache de globais. mutable porque GetGlobal() e logicamente const —
		// quem chama nao deveria precisar saber que existe cache.
		mutable std::vector<glm::mat4> m_Globals;
		mutable bool m_GlobalsDirty = true;

		mutable const Skeleton* m_MapSkeleton = nullptr;
		mutable std::vector<int> m_BoneToElement;
	};

} // namespace axe