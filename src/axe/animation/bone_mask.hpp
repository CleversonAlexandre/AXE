#pragma once
#include "axe/core/types.hpp"
#include "axe/animation/skeleton.hpp"

#include <string>
#include <vector>

namespace axe
{
	// Peso por osso, em [0,1]. Decide QUANTO de cada osso vem da camada de
	// cima num BlendMasked.
	//
	// Caso de uso canônico: o personagem corre (pose base) e atira (camada).
	// A máscara marca a coluna pra cima com peso 1 — os braços seguem o tiro,
	// as pernas seguem a corrida.
	class AXE_API BoneMask
	{
	public:
		// Todos os ossos com o mesmo peso (0 por padrão = camada não afeta
		// nada).
		void Reset(const Skeleton& skeleton, float weight = 0.0f);

		// Marca `rootBoneName` e TODOS os seus descendentes com `weight`.
		//
		// Descer a hierarquia é o que torna a máscara utilizável: você diz
		// "spine_02 pra cima" e não precisa listar os 30 ossos de braço, mão
		// e dedo um por um.
		//
		// Retorna false se o osso não existir (nome errado no asset).
		bool SetBranch(const Skeleton& skeleton,
			const std::string& rootBoneName,
			float weight = 1.0f);

		void SetBone(int boneIndex, float weight);

		// Suaviza o peso ao longo dos N ossos abaixo da raiz do ramo, em vez
		// de cortar de 0 pra 1 de um osso pro outro.
		//
		// Sem isto, a transição entre as duas camadas acontece numa junta
		// só — e o tronco "quebra" visivelmente ali. É o defeito nº1 de
		// máscara feita à mão.
		void Feather(const Skeleton& skeleton,
			const std::string& rootBoneName,
			int depth);

		// Osso fora do alcance devolve 0 (a camada não o afeta) — nunca
		// estoura o vetor.
		float GetWeight(std::size_t boneIndex) const
		{
			return boneIndex < m_Weights.size() ? m_Weights[boneIndex] : 0.0f;
		}

		std::size_t Size() const { return m_Weights.size(); }
		bool IsEmpty() const { return m_Weights.empty(); }

	private:
		std::vector<float> m_Weights;
	};

} // namespace axe