#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/animation/skeleton.hpp"
#include "axe/animation/animation_clip.hpp"
#include "axe/animation/pose.hpp"

#include <vector>

namespace axe
{
	// Transforma (Skeleton + AnimationClip + tempo) na palette de matrizes
	// que o vertex shader consome como u_Bones[].
	//
	// Fica em axe.dll e NÃO conhece OpenGL: só produz dados. Quem sobe pra
	// GPU é o renderer, via RenderQueue — mantendo o desacoplamento do resto
	// do engine.
	//
	// A matriz final de cada bone é:
	//
	//     Skin[i] = GlobalInverse * Global[i] * InverseBindPose[i]
	//
	//   InverseBindPose  leva o vértice do espaço do modelo pro espaço do bone
	//                    (desfaz a T-pose)
	//   Global[i]        recoloca o vértice no mundo do modelo, já com a pose
	//                    animada acumulada do bone raiz até aqui
	//   GlobalInverse    cancela o transform que o exportador pendurou no node
	//                    raiz (rotação de eixos do Blender, escala cm/m, etc)
	class AXE_API AnimationSampler
	{
	public:
		// ── Milestone 3: o caminho de duas etapas ────────────────────────
		//
		// O fluxo antigo (clipe → matrizes, direto) NÃO permite blending:
		// interpolar duas mat4 linearmente produz shear e encolhe o osso.
		// Blend só é correto em espaço LOCAL TRS. Por isso o pipeline agora
		// é sempre:
		//
		//     SamplePose(clip)  →  Pose  →  [blend/mask/additive]  →  Pose
		//                                                              ↓
		//                                        BuildSkinningMatrices()
		//
		// Sample() abaixo continua existindo, mas é só um atalho pro caso
		// de um clipe só, sem blend nenhum.

		// Amostra um clipe e devolve a pose LOCAL. Ossos que o clipe não
		// anima caem na bind pose (não na identidade — senão o corpo
		// colapsa na origem).
		static void SamplePose(const Skeleton& skeleton,
			const AnimationClip& clip,
			float timeSeconds,
			Pose& outPose);

		// Último passo do pipeline: acumula a hierarquia e produz as
		// matrizes que o Skin Cache consome. Depois daqui não há mais nada
		// pra blendar.
		static void BuildSkinningMatrices(const Skeleton& skeleton,
			const Pose& pose,
			std::vector<glm::mat4>& outSkinning,
			std::vector<glm::mat4>* outGlobals = nullptr);

		// ── API original (Milestone 1) ───────────────────────────────────
		// Amostra `clip` no instante `timeSeconds` e preenche `outSkinning`
		// com uma matriz por bone (redimensionado automaticamente).
		//
		// O tempo é envelopado pelo próprio clip (loop ou clamp, conforme
		// AnimationClip::IsLooping) — pode passar o tempo acumulado cru.
		//
		// Bones que o clipe não anima caem na LocalBindPose: um clipe que só
		// mexe o braço não colapsa o resto do corpo na origem.
		//
		// `outGlobals`, se != nullptr, recebe as matrizes globais (model-space)
		// de cada bone — é o que sockets/attachments (arma na mão, efeito no
		// pé) vão usar mais pra frente. Passe nullptr se não precisar.
		static void Sample(const Skeleton& skeleton,
			const AnimationClip& clip,
			float timeSeconds,
			std::vector<glm::mat4>& outSkinning,
			std::vector<glm::mat4>* outGlobals = nullptr);

		// Pose de bind pura (T-pose). Útil pro preview no editor, pra um
		// SkeletalMesh sem clipe atribuído, e como fallback quando um clipe
		// falha ao carregar — sem isso a mesh colapsa num ponto.
		static void BindPose(const Skeleton& skeleton,
			std::vector<glm::mat4>& outSkinning,
			std::vector<glm::mat4>* outGlobals = nullptr);

		// Palette neutra (identidades). É o que um shader skinned recebe
		// quando não há esqueleto — renderiza a mesh como se fosse estática.
		static void Identity(std::vector<glm::mat4>& outSkinning, std::size_t count);
	};

} // namespace axe