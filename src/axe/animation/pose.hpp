#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/animation/skeleton.hpp"

#include <vector>

namespace axe
{
	// Forward-declare COM o AXE_API. Sem ele, o MSVC vê uma declaração sem
	// dllexport seguida de uma definição com dllexport (bone_mask.hpp) e
	// reclama de linkage divergente — um erro que o g++/clang ignoram, e que
	// por isso só aparece no build de verdade.
	class AXE_API BoneMask;

	// Transform LOCAL de um osso (relativo ao pai), decomposto.
	//
	// Guardar T/R/S separados — e não uma mat4 — é o que torna o blending
	// possível. Uma mat4 já misturou rotação e escala; interpolar duas delas
	// linearmente produz shear e ENCOLHIMENTO (o osso afunda no meio da
	// transição). Com quaternion separado, dá pra fazer slerp de verdade.
	struct BoneTransform
	{
		glm::vec3 Translation{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale{ 1.0f };

		glm::mat4 ToMatrix() const;
		static BoneTransform FromMatrix(const glm::mat4& m);
	};

	// Uma pose completa: um BoneTransform por osso, em espaço LOCAL.
	//
	// É a moeda de troca de todo o sistema de animação a partir do Milestone
	// 3. O fluxo passa a ser:
	//
	//     clipes → Pose → (blend/mask/additive) → Pose final → matrizes
	//
	// Tudo que mistura animação opera em Pose. Só no ÚLTIMO passo a pose vira
	// matrizes de skinning (AnimationSampler::BuildSkinningMatrices), e aí já
	// não há mais nada pra blendar.
	class AXE_API Pose
	{
	public:
		void Resize(std::size_t boneCount) { m_Bones.resize(boneCount); }
		std::size_t Size() const { return m_Bones.size(); }
		bool IsEmpty() const { return m_Bones.empty(); }

		BoneTransform& operator[](std::size_t i) { return m_Bones[i]; }
		const BoneTransform& operator[](std::size_t i) const { return m_Bones[i]; }

		const std::vector<BoneTransform>& GetBones() const { return m_Bones; }

		// Pose de bind (T-pose) do esqueleto. Base de tudo: qualquer osso
		// que um clipe não anima cai aqui.
		static void FromBindPose(const Skeleton& skeleton, Pose& out);

		// ── Operações de blend ───────────────────────────────────────────
		// Todas escrevem em `out`, que pode ser o mesmo objeto que `a` ou
		// `b` (fazem aliasing com segurança).

		// Blend linear uniforme: out = a*(1-alpha) + b*alpha.
		// alpha=0 → a; alpha=1 → b. É o crossfade entre dois clipes.
		static void Blend(const Pose& a, const Pose& b, float alpha, Pose& out);

		// Blend por osso, com peso vindo de uma máscara.
		//
		// É isso que faz "andar com as pernas E atirar com os braços": a
		// pose de baixo é a locomoção, a de cima é o tiro, e a máscara
		// decide onde uma vira a outra. `alpha` escala a máscara inteira
		// (permite fazer fade in/out da camada).
		static void BlendMasked(const Pose& base, const Pose& layer,
			const BoneMask& mask, float alpha, Pose& out);

		// ── BLEND MASCARADO EM ESPACO DE MALHA (so a ROTACAO) ────────────
		//
		// ── O PROBLEMA QUE ISTO RESOLVE ──────────────────────────────────
		//
		// O BlendMasked acima mistura rotacao LOCAL — local ao pai. Isso e
		// certo quando as duas poses partem da mesma base, e errado quando a
		// mascara CORTA a hierarquia no meio.
		//
		// O caso concreto: locomocao na base, mira na camada, mascara a
		// partir do Spine. O Hips fica de fora da mascara, entao vem da
		// locomocao. Mas as rotacoes locais do Spine da camada foram
		// autoradas em cima do Hips DELA — que numa pose de mira costuma
		// estar girado. Enxertadas num Hips diferente, elas acumulam a
		// diferenca: o tronco inteiro sai torto, e a arma aponta para o lado.
		//
		// Trocar a raiz da mascara para Hips "conserta" porque ai a camada
		// substitui o corpo todo — e ai as pernas param de andar. Os dois
		// sintomas sao o mesmo problema visto de dois lados.
		//
		// ── A CORRECAO ───────────────────────────────────────────────────
		//
		// Compor a rotacao ate o espaco da MALHA, interpolar LA, e so entao
		// voltar para local contra o pai ja resolvido. A camada passa a
		// carregar "para onde este osso APONTA", e nao "quanto ele gira em
		// relacao ao pai" — entao a direcao da mira sobrevive a qualquer
		// coisa que a base faca com o quadril.
		//
		// SO a rotacao. Translacao em espaco de malha brigaria com a posicao
		// do quadril que a base acabou de definir, e o personagem se
		// desmontaria. E a mesma divisao que o "Mesh Space Rotation Blend"
		// da Unreal faz, e pelo mesmo motivo.
		//
		// O esqueleto e necessario para os indices de pai; a ordem topologica
		// (ParentIndex < i, garantida pelo Skeleton) e o que permite resolver
		// tudo numa passada so.
		//
		// `scratch` existe para nao alocar tres vetores por frame por
		// personagem — quem chama e um no de AnimGraph, que ja guarda estado
		// por instancia.
		struct MeshSpaceScratch
		{
			std::vector<glm::quat> Base;
			std::vector<glm::quat> Layer;
			std::vector<glm::quat> Out;
		};

		static void BlendMaskedMeshSpace(const Pose& base, const Pose& layer,
			const BoneMask& mask, float alpha, const Skeleton& skeleton,
			MeshSpaceScratch& scratch, Pose& out);

		// ── Additive ─────────────────────────────────────────────────────
		// Aditivo é a DIFERENÇA entre uma pose e uma referência. É como
		// funciona um "hit reaction" ou uma respiração: você não substitui
		// a animação de correr, você SOMA um solavanco em cima dela.

		// additive = source - reference (por osso).
		//   T: subtração   R: ref⁻¹ * src   S: divisão
		static void MakeAdditive(const Pose& source, const Pose& reference, Pose& outAdditive);

		// out = base + additive*alpha.
		static void ApplyAdditive(const Pose& base, const Pose& additive,
			float alpha, Pose& out);

		// ── Blend N-ário incremental ─────────────────────────────────────
		// Usado pelo BlendSpace, que mistura vários clipes com pesos que
		// somam 1. Chame Reset() e depois Accumulate() por clipe.
		//
		// Não é `sum(pose_i * w_i)`: quaternion não soma. O truque é fazer
		// slerp incremental, reponderando pelo peso acumulado até aqui.
		void ResetAccumulation();
		void Accumulate(const Pose& pose, float weight);

	private:
		std::vector<BoneTransform> m_Bones;

		// Soma dos pesos já acumulados — o denominador do slerp incremental.
		float m_AccumulatedWeight = 0.0f;
	};

} // namespace axe