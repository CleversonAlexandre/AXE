#pragma once
#include "axe/core/types.hpp"
#include "axe/animation/pose.hpp"
#include "axe/animation/skeleton.hpp"
#include "axe/animation/anim_parameters.hpp"


#include <memory>
#include <string>
#include <vector>

// O header completo, e não o json_fwd.hpp: o forward seria mais leve, mas eu
// não tenho como confirmar que ele existe no vendor deste projeto — e um
// include que não resolve custa um ciclo de compilação inteiro pra descobrir.
#include <nlohmann/json.hpp>

namespace axe
{
	struct AnimNotify;   // animation_clip.hpp

	// ═════════════════════════════════════════════════════════════════════════
	//  GRAFO DE POSES
	//
	//  O AnimGraph deixa de SER uma máquina de estados e passa a ser um grafo
	//  onde os links carregam uma POSE.
	//
	//      Clip Player ──┐
	//                    ├──► Blend by Float ──► Layered Blend ──► Output Pose
	//      Blend Space ──┘                            ▲
	//                                                 │
	//                            Attack (aditivo) ────┘
	//
	//  A máquina de estados não some — ela vira UM NÓ (AnimNode_StateMachine).
	//  É a promoção que a Unreal fez, e é o que abre espaço pra IK, Control Rig
	//  e Montage/Slot: todos são nós que recebem uma pose e devolvem outra.
	//
	//  Num grafo de estados puro (o que tínhamos), simplesmente NÃO EXISTE onde
	//  plugar um Two Bone IK. É essa a diferença.
	// ═════════════════════════════════════════════════════════════════════════

	// Buffers de Pose reciclados.
	//
	// Um blend de quatro níveis precisa de poses temporárias em cada junção.
	// Alocar um vector<BoneTransform> por nó, por personagem, por frame é morte
	// por mil cortes — e o profiler vai apontar pro malloc, não pra animação.
	class AXE_API PosePool
	{
	public:
		PosePool() = default;

		// ── COPIÁVEL, E A CÓPIA NASCE VAZIA ──────────────────────────────────
		//
		// O pool é RASCUNHO: buffers de pose emprestados durante a avaliação de
		// um frame. Não há nada nele que valha a pena copiar.
		//
		// Mas ele PRECISA ser copiável, porque vive dentro do
		// SkeletalMeshComponent — e o SceneSnapshot (o Play/Stop) clona o
		// registry inteiro. Um pool move-only quebrava a compilação do axe.dll.
		//
		// Copiar os buffers seria pior que inútil: seriam sobrescritos no
		// primeiro Evaluate.
		PosePool(const PosePool&) {}
		PosePool& operator=(const PosePool&) { m_Buffers.clear(); m_Used = 0; return *this; }

		PosePool(PosePool&&) = default;
		PosePool& operator=(PosePool&&) = default;

		// Devolve um buffer livre, já dimensionado pro esqueleto.
		Pose& Acquire(const Skeleton& skeleton);

		// Libera TUDO. Chamado uma vez por frame, no fim da avaliação.
		//
		// Não há release individual de propósito: os nós emprestam poses em
		// pilha (a avaliação é uma árvore), e devolver tudo de uma vez é mais
		// simples e mais rápido do que rastrear cada empréstimo.
		void ReleaseAll() { m_Used = 0; }

		std::size_t Capacity() const { return m_Buffers.size(); }

	private:
		std::vector<std::unique_ptr<Pose>> m_Buffers;
		std::size_t m_Used = 0;
	};

	struct AXE_API AnimEvalContext
	{
		const Skeleton* Skel = nullptr;

		// O blackboard. Nós leem daqui; o gameplay escreve.
		AnimParameters* Params = nullptr;

		float DeltaTime = 0.0f;

		// false = preview do editor com o tempo congelado. Os nós ainda
		// avaliam (a pose sai certa), mas nada avança.
		bool AdvanceTime = true;

		PosePool* Pool = nullptr;

		// Coletor de notifies cruzados neste Update. Quem quiser saber (o
		// AnimationWorld, pra spawnar particula/avisar script) aponta um
		// vector aqui; nullptr = ninguem ouvindo, custo zero. Os NOS so
		// COLETAM — despachar e decisao de quem esta fora do grafo.
		// Ponteiro para vector de tipo INCOMPLETO: legal em C++17 e evita
		// puxar animation_clip.hpp (e a cadeia dele) pra todo TU que so
		// precisa do contexto — inclusive os scripts compilados em runtime.
		std::vector<AnimNotify>* NotifySink = nullptr;

		float GetFloat(const std::string& name) const
		{
			return Params ? Params->GetFloat(name) : 0.0f;
		}

		bool GetBool(const std::string& name) const
		{
			return Params ? Params->GetBool(name) : false;
		}
	};

	// ═════════════════════════════════════════════════════════════════════════
	//  PINOS TIPADOS
	//
	//  Os links do grafo NÃO carregam só poses.
	//
	//      [Speed] ──(float)──► Blend Space 2D ──(pose)──► Output Pose
	//
	//  Sem pinos de dado, o parâmetro que dirige um blend space teria que ser
	//  um nome digitado dentro do nó — e aí não há como pôr um Clamp, um Lerp
	//  ou uma curva entre a variável e o consumidor. O grafo vira um formulário
	//  com fios, não um grafo.
	// ═════════════════════════════════════════════════════════════════════════
	enum class AnimPinType
	{
		Pose,
		Float,
		Bool
	};

	class AnimNode;

	// Entrada de DADO de um nó.
	//
	// Tem três origens possíveis, nesta ordem de precedência:
	//
	//   1. Link  — ligado a um nó de valor (o nó verde "Speed")
	//   2. Inline — o valor digitado no próprio pino
	//
	// É o comportamento da Unreal: o pino mostra um campo editável enquanto
	// nada estiver ligado nele, e o campo some quando você liga um fio.
	struct AXE_API AnimDataPin
	{
		std::string Name;
		AnimPinType Type = AnimPinType::Float;

		float InlineFloat = 0.0f;
		bool  InlineBool = false;

		// Resolvido pelo AnimPoseGraph a partir de m_Links. nullptr = usa inline.
		//
		// Cache, não fonte de verdade: a verdade é a lista de links do grafo.
		// Guardar aqui um "LinkedNodeId" serializado criaria um segundo lugar
		// onde a topologia mora — e dois lugares sempre divergem.
		AnimNode* Link = nullptr;
	};

	class AXE_API AnimNode
	{
	public:
		virtual ~AnimNode() = default;

		// Nome do TIPO (serializado). "ClipPlayer", "StateMachine"...
		virtual const char* TypeName() const = 0;

		// ── CÓPIA PROFUNDA ───────────────────────────────────────────────────
		//
		// O grafo do ASSET é um MOLDE. Cada personagem roda a SUA cópia.
		//
		// Não é purismo: os nós guardam estado de runtime (o tempo do clipe, o
		// estado atual da máquina, o progresso do crossfade). Se dois
		// personagens compartilhassem os nós do asset, andariam em sincronia
		// perfeita como marionetes — e um deles entrando em "Attack" mudaria o
		// estado do outro.
		//
		// É virtual (e não um copy-ctor) porque clonamos por AnimNode*, sem
		// saber o tipo concreto. `= 0` de propósito: um nó novo que esqueça de
		// implementar Clone NÃO COMPILA. Se tivesse default, ele compilaria e
		// perderia os campos em silêncio.
		virtual std::unique_ptr<AnimNode> Clone() const = 0;

		// O que este nó PRODUZ.
		//
		// Pose para os nós de animação; Float/Bool para os nós de variável.
		// O editor usa isto pra colorir o pino de saída e pra REJEITAR um link
		// de float num pino de pose — validação na hora de ligar, não um crash
		// três frames depois.
		virtual AnimPinType OutputType() const { return AnimPinType::Pose; }

		// ── Nós de VALOR ─────────────────────────────────────────────────────
		//
		// Só os nós cujo OutputType() não é Pose implementam estes. Os nós de
		// pose herdam o default e nunca são chamados assim.
		virtual float EvaluateFloat(AnimEvalContext& ctx) { (void)ctx; return 0.0f; }
		virtual bool  EvaluateBool(AnimEvalContext& ctx) { (void)ctx; return false; }

		// ── FASE 1 ───────────────────────────────────────────────────────────
		//
		// Avança o tempo, decide transições, calcula pesos.
		//
		// Separada do Evaluate DE PROPÓSITO. Se fossem uma fase só:
		//
		//   - um estado saindo de cena congelaria no meio do crossfade (o
		//     personagem para de andar durante a transição e o pé escorrega
		//     no chão — bem visível);
		//   - não haveria como pular ramos de peso zero antes de gastar CPU
		//     amostrando poses que ninguém vai ver.
		//
		// A regra: quem faz blend TEM que dar Update nos DOIS lados, mesmo que
		// só um apareça na tela.
		virtual void Update(AnimEvalContext& ctx) = 0;

		// ── FASE 2 ───────────────────────────────────────────────────────────
		//
		// Produz a pose. Puxa as entradas conforme precisa.
		virtual void Evaluate(AnimEvalContext& ctx, Pose& out) = 0;

		// Duração do ciclo em segundos. A máquina de estados usa isto pro
		// ExitTime — e num blend space a duração VARIA com o parâmetro, o que
		// faz o exit time acompanhar a velocidade sozinho.
		virtual float GetDuration(AnimEvalContext& ctx) const { (void)ctx; return 0.0f; }

		// 0..1 dentro do ciclo. É o que o ExitTime compara.
		virtual float GetNormalizedTime() const { return 0.0f; }

		// Volta ao estado inicial (entrar num estado, dar Stop, etc).
		virtual void Reset() {}

		// ── Serialização ─────────────────────────────────────────────────────
		//
		// Cada nó salva a SI MESMO.
		//
		// A alternativa seria um switch gigante no AnimGraphAsset ("se for
		// ClipPlayer, grave ClipName; se for BlendSpace, grave as amostras...").
		// Isso significa que adicionar um tipo de nó exige editar TRÊS lugares:
		// a classe, o save e o load. Um dia alguém edita dois — e o sintoma é
		// um nó que perde metade dos campos ao reabrir o projeto, em silêncio.
		//
		// Aqui, adicionar um nó é adicionar uma classe. Só.
		//
		// Os campos COMUNS (Id, tipo, título, posição, valores inline dos pinos
		// de dado) são gravados pelo grafo, não por cada nó — senão todo nó
		// repetiria o mesmo bloco e um deles esqueceria.
		virtual void Serialize(nlohmann::json& j) const { (void)j; }
		virtual void Deserialize(const nlohmann::json& j) { (void)j; }

		// ── Ligação ──────────────────────────────────────────────────────────
		//
		// Entradas de POSE, em ordem de pino. Preenchidas pelo AnimPoseGraph
		// ao resolver os links. Um pino sem link fica nullptr — e todo nó TEM
		// que tolerar isso (cai em bind pose), senão um grafo em construção
		// crasha o editor.
		std::vector<AnimNode*> Inputs;

		// Quantos pinos de pose este nó tem. O editor desenha a partir daqui.
		virtual int InputCount() const { return 0; }
		virtual const char* InputName(int i) const { (void)i; return "Pose"; }

		// Entradas de DADO. Cada nó declara as suas no construtor.
		//
		// Vetor concreto (e não virtual como os pinos de pose) porque o editor
		// precisa EDITAR o valor inline — e um getter virtual const não deixa.
		std::vector<AnimDataPin> DataInputs;

		// ── Autoria ──────────────────────────────────────────────────────────
		int         Id = -1;          // único dentro do grafo
		std::string Title;            // rótulo no editor
		float       EditorX = 0.0f;
		float       EditorY = 0.0f;

		// Copia os campos comuns (título, posição, valores inline dos pinos).
		// Chamado pelo Clone() de cada nó — centralizado pra que um nó novo não
		// esqueça de copiar a posição no editor, por exemplo.
		void CopyCommonTo(AnimNode& dst) const
		{
			dst.Id = Id;
			dst.Title = Title;
			dst.EditorX = EditorX;
			dst.EditorY = EditorY;
			dst.DataInputs = DataInputs;

			// Os ponteiros resolvidos NÃO são copiados: eles apontam pros nós
			// do grafo ANTIGO. O Resolve() do grafo novo os reconstrói.
			for (auto& d : dst.DataInputs)
				d.Link = nullptr;

			dst.Inputs.assign(dst.InputCount(), nullptr);
		}

	protected:
		// Avalia uma entrada; se não houver nada ligada, devolve bind pose.
		//
		// Centralizado aqui porque TODO nó precisa disso, e esquecer o check
		// num único lugar significa ler um vector vazio — crash silencioso e
		// difícil de rastrear.
		void EvalInput(AnimEvalContext& ctx, int pin, Pose& out) const;
		void UpdateInput(AnimEvalContext& ctx, int pin) const;

		// Lê um pino de dado: link se houver, senão o valor inline.
		//
		// Centralizado aqui pelo mesmo motivo do EvalInput: se cada nó
		// reimplementasse o "tem link? senão inline", um deles esqueceria — e o
		// sintoma seria um blend space travado num valor, sem erro nenhum.
		float ReadFloat(AnimEvalContext& ctx, int pin) const;
		bool  ReadBool(AnimEvalContext& ctx, int pin) const;

		// Declara um pino de dado. Chamado no construtor de cada nó.
		void AddFloatPin(const char* name, float inlineValue = 0.0f);
		void AddBoolPin(const char* name, bool inlineValue = false);
	};

} // namespace axe