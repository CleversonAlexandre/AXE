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

		// ── So para Control ──────────────────────────────────────────────────
		RigControlShape Shape = RigControlShape::Circle;
		glm::vec3       ShapeColor{ 1.0f, 0.85f, 0.15f };
		float           ShapeSize = 1.0f;

		// Deslocamento APENAS do desenho do gizmo, em espaco do proprio
		// elemento. Existe porque o ponto util pra agarrar quase nunca e o
		// pivo: o controle do pe pivota no tornozelo, mas voce quer clicar na
		// sola.
		BoneTransform   ShapeOffset;
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

		// Copia os ossos do esqueleto para ca, preservando a hierarquia. E o
		// primeiro passo de "criar Control Rig a partir do esqueleto".
		void ImportFromSkeleton(const Skeleton& skeleton);

		// ── Consulta ─────────────────────────────────────────────────────────

		std::size_t Size() const { return m_Elements.size(); }

		const std::vector<RigElement>& GetElements() const { return m_Elements; }
		std::vector<RigElement>& GetElements() { return m_Elements; }

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
		glm::mat4 GetInitialGlobal(int i) const;
		void      SetInitialGlobal(int i, const glm::mat4& m);

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