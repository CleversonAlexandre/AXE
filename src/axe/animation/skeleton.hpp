#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace axe
{
	// Número máximo de bones que o vertex shader consegue receber por
	// draw call (uniform mat4 u_Bones[MAX_BONES]). 128 mat4 = 8 KB de
	// uniform storage — cabe folgado no limite de 64 KB da RX 580.
	// Se um esqueleto passar disso, o loader avisa e trunca.
	static constexpr int AXE_MAX_BONES = 128;

	// Quantos bones podem influenciar um único vértice. 4 é o padrão da
	// indústria e casa com aiProcess_LimitBoneWeights do Assimp.
	static constexpr int AXE_MAX_BONE_INFLUENCE = 4;

	struct Bone
	{
		std::string Name;

		// Índice do pai dentro de Skeleton::m_Bones. -1 = raiz.
		// INVARIANTE: ParentIndex < índice deste bone (ordem topológica),
		// o que permite calcular a pose global num único loop pra frente,
		// sem recursão — o pai SEMPRE já foi resolvido.
		int ParentIndex = -1;

		// Offset matrix do Assimp: leva um vértice do espaço do MODELO
		// para o espaço LOCAL deste bone, na pose de bind (T-pose).
		// É o que "desfaz" a pose de bind antes de aplicar a animação.
		glm::mat4 InverseBindPose{ 1.0f };

		// Transform local do node na pose de bind. Usado como fallback
		// quando um AnimationClip não tem canal para este bone (ex: um
		// clipe de "acenar" que só anima o braço — o resto do corpo usa
		// a bind pose em vez de colapsar na identidade).
		glm::mat4 LocalBindPose{ 1.0f };
	};

	class AXE_API Skeleton
	{
	public:
		// Adiciona um bone. O chamador DEVE inserir os pais antes dos
		// filhos (o SkeletalMeshLoader garante isso via DFS a partir da
		// raiz). Retorna o índice do bone criado.
		int AddBone(const std::string& name,
			int parentIndex,
			const glm::mat4& inverseBindPose,
			const glm::mat4& localBindPose);

		// Retorna o índice do bone, ou -1 se não existir.
		// É por aqui que um AnimationClip carregado de OUTRO arquivo
		// (ex: idle.fbx) se liga a este esqueleto: casamento por nome.
		int FindBone(const std::string& name) const;

		const std::vector<Bone>& GetBones() const { return m_Bones; }
		std::size_t GetBoneCount() const { return m_Bones.size(); }
		bool IsEmpty() const { return m_Bones.empty(); }

		// Inverso do transform do node raiz da cena. Cancela qualquer
		// transform que o exportador tenha colocado no topo da hierarquia
		// (rotação de eixos do Blender/Maya, escala em cm vs m, etc).
		const glm::mat4& GetGlobalInverseTransform() const { return m_GlobalInverseTransform; }
		void SetGlobalInverseTransform(const glm::mat4& m) { m_GlobalInverseTransform = m; }

		// Fator que converte as unidades do ARQUIVO para METROS.
		//
		// FBX guarda `UnitScaleFactor` = quantos centimetros vale 1 unidade.
		// A Mixamo grava 1.0 (1 unidade = 1 cm), entao o fator vira 0.01.
		// glTF e .obj ja vem em metros -> 1.0.
		//
		// Guardado aqui pra que LoadClips consiga AVISAR quando um arquivo de
		// animacao vier com unidade diferente da do personagem — caso em que
		// as translacoes dos ossos ficariam 100x erradas e o personagem
		// esticaria, um bug bem dificil de diagnosticar no olho.
		float GetUnitScale() const { return m_UnitScale; }
		void  SetUnitScale(float s) { m_UnitScale = s; }

		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& name) { m_Name = name; }

	private:
		std::string                            m_Name;
		std::vector<Bone>                      m_Bones;   // ordem topológica
		std::unordered_map<std::string, int>   m_NameToIndex;
		glm::mat4                              m_GlobalInverseTransform{ 1.0f };

		// Fator arquivo -> metros (ver GetUnitScale).
		float                                  m_UnitScale = 1.0f;
	};

} // namespace axe
