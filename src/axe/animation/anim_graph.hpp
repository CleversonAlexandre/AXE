#pragma once
#include "axe/core/types.hpp"
#include "axe/animation/anim_parameters.hpp"
#include "axe/animation/animation_clip.hpp"
#include "axe/animation/blend_space_1d.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace axe
{
	enum class AnimCompare
	{
		Greater,
		GreaterEqual,
		Less,
		LessEqual,
		Equal,
		NotEqual,

		IsTrue,
		IsFalse,

		// Só para Trigger. Verdadeiro se o pulso está armado.
		TriggerSet
	};

	struct AXE_API AnimCondition
	{
		std::string Parameter;
		AnimCompare Op = AnimCompare::Greater;
		float       Value = 0.0f;   // ignorado por IsTrue/IsFalse/TriggerSet

		// PURAMENTE LEITURA. Nunca consome trigger — ver o comentário em
		// AnimParameters::ConsumeTrigger.
		bool Evaluate(const AnimParameters& params) const;
	};

	// DECLARACAO de um parametro do grafo.
	//
	// Diferente do AnimParameters, que e o blackboard de RUNTIME (qualquer
	// nome vira chave na hora). Aqui e o CONTRATO: quais parametros este
	// grafo conhece, de que tipo, com que valor inicial.
	//
	// Sem isso o editor nao teria o que oferecer no dropdown de condicao — o
	// usuario teria que digitar o nome do parametro na mao e torcer pra nao
	// errar, e um typo viraria uma transicao que nunca dispara, sem erro
	// nenhum. E o contrato tambem e o que o Script Editor vai ler pra saber
	// o que pode escrever.
	struct AXE_API AnimParamDecl
	{
		std::string   Name;
		AnimParamType Type = AnimParamType::Float;

		float DefaultF = 0.0f;
		int   DefaultI = 0;
		bool  DefaultB = false;
	};

	// Um estado toca UMA coisa: um clipe, ou um blend space inteiro.
	//
	// Poder ter um blend space dentro de um estado é o que evita o erro
	// clássico de modelar locomoção como três estados (idle/walk/run) com
	// transições entre eles — o que sempre dá "pop" no meio de uma
	// aceleração. Locomoção inteira vira UM estado.
	struct AXE_API AnimState
	{
		std::string Name;

		std::shared_ptr<AnimationClip> Clip;

		// Se != nullptr, tem prioridade sobre Clip.
		std::shared_ptr<BlendSpace1D> BlendSpace;

		// Nome do parâmetro que dirige o blend space (ex: "Speed").
		std::string BlendParameter;

		float PlayRate = 1.0f;

		// ── Referencias por NOME (o que vai pro .axeanim) ────────────────
		//
		// O asset nao pode serializar um shared_ptr<AnimationClip>. Guarda o
		// NOME do clipe e religa ao abrir, contra os clipes do .axeskel.
		//
		// Nome e nao indice: o usuario pode reordenar/remover animacoes no
		// personagem, e um indice apontaria pro clipe errado em silencio.
		std::string ClipName;

		// Blend space serializado: pares (nome do clipe, valor no eixo).
		std::vector<std::pair<std::string, float>> BlendSamples;

		// Posicao do no na tela do editor. Dado de AUTORIA, nao de runtime —
		// mas vive aqui porque o alternativa (um mapa paralelo indexado por
		// estado) desincroniza na primeira vez que alguem remove um estado.
		float EditorX = 0.0f;
		float EditorY = 0.0f;

		// Duração efetiva do estado — usada pelo ExitTime das transições.
		// Num blend space, varia com o parâmetro.
		float GetDuration(const AnimParameters& params) const;
	};

	struct AXE_API AnimTransition
	{
		// -1 = ANY STATE: vale a partir de qualquer estado.
		//
		// É o que faz "morrer" e "levar dano" funcionarem sem você ter que
		// ligar 15 setas de todos os estados até Death. Sem Any-State, um
		// grafo real vira uma teia ilegível.
		int From = -1;
		int To = -1;

		float Duration = 0.2f;   // crossfade; 0 = corte seco

		// Se true, a transição só pode disparar depois que o estado de
		// origem completou `ExitTime` do seu ciclo (0..1).
		//
		// É o que impede um ataque de ser cortado no primeiro frame. Sem
		// isso, "attack -> idle quando parado" dispararia imediatamente e o
		// golpe nunca sairia.
		bool  HasExitTime = false;
		float ExitTime = 1.0f;   // 1.0 = só ao terminar o ciclo

		// Empate é resolvido por prioridade (maior vence). Em caso de empate
		// de prioridade, vence a que foi adicionada primeiro.
		int Priority = 0;

		// Transição Any-State para o estado em que já estamos: normalmente
		// NÃO deve redisparar (senão "levar dano" reinicia eternamente
		// enquanto o trigger estiver armado). Ligue só se quiser re-trigger.
		bool CanRetriggerSelf = false;

		// TODAS precisam passar (AND). Lista vazia = sempre verdadeira —
		// combinada com HasExitTime, é como se faz "quando o clipe acabar,
		// volte pro idle".
		std::vector<AnimCondition> Conditions;

		bool ConditionsMet(const AnimParameters& params) const;
	};

	// Definição do grafo. É DADO, não runtime: várias entidades podem
	// compartilhar o mesmo AnimGraph, cada uma com seu AnimGraphInstance
	// (que carrega o estado atual, o tempo, os parâmetros).
	//
	// Vira o asset `.axeanim` no Milestone 5.
	class AXE_API AnimGraph
	{
	public:
		int AddState(const AnimState& state);
		int FindState(const std::string& name) const;   // -1 se não achar

		// Remove o estado e TODAS as transicoes que o tocam, reindexando as
		// que sobraram. Sem a reindexacao, remover o estado 2 faria toda
		// transicao que aponta pro 3 passar a apontar pro 4 — um bug de
		// autoria silencioso e infernal de achar.
		void RemoveState(int index);

		void AddTransition(const AnimTransition& transition);
		void RemoveTransition(int index);

		const std::vector<AnimState>& GetStates() const { return m_States; }
		const std::vector<AnimTransition>& GetTransitions() const { return m_Transitions; }

		std::vector<AnimState>& GetStatesMutable() { return m_States; }
		std::vector<AnimTransition>& GetTransitionsMutable() { return m_Transitions; }

		const AnimState& GetState(int i) const { return m_States[i]; }
		AnimState& GetStateMutable(int i) { return m_States[i]; }

		// ── Parametros declarados ────────────────────────────────────────
		void AddParameter(const AnimParamDecl& decl);
		void RemoveParameter(int index);
		int  FindParameter(const std::string& name) const;

		const std::vector<AnimParamDecl>& GetParameters() const { return m_Parameters; }
		std::vector<AnimParamDecl>& GetParametersMutable() { return m_Parameters; }

		// Preenche um blackboard com os valores DEFAULT das declaracoes.
		// Chamado pelo AnimGraphInstance ao assumir o grafo — sem isto, uma
		// condicao "Speed > 100" leria 0 no primeiro frame mesmo que o
		// default fosse 300.
		void SeedParameters(AnimParameters& params) const;

		// Estado inicial. Se nunca setado, é o primeiro adicionado.
		int  GetEntryState() const { return m_EntryState; }
		void SetEntryState(int i) { m_EntryState = i; }
		bool SetEntryState(const std::string& name);

		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& n) { m_Name = n; }

		bool IsEmpty() const { return m_States.empty(); }

		// Confere que nenhuma transição aponta pra índice inválido e que o
		// estado de entrada existe. Chame ao carregar o asset — um índice
		// podre aqui vira crash no primeiro frame de Play.
		bool Validate() const;

	private:
		std::string                 m_Name;
		std::vector<AnimState>      m_States;
		std::vector<AnimTransition> m_Transitions;
		std::vector<AnimParamDecl>  m_Parameters;
		int                         m_EntryState = 0;
	};

} // namespace axe
