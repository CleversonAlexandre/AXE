#pragma once
#include "axe/core/types.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/material/material.hpp"
#include <memory>
#include <string>

namespace axe
{

	struct AXE_API LoadedAsset
	{
		std::shared_ptr<Mesh>     MeshData;
		std::shared_ptr<Material> MaterialData;
	};

	class AXE_API MeshLoader
	{
	public:
		// Retorna asset vazio (MeshData == nullptr) se falhar.
		// Resultado é cacheado por filepath — chamadas repetidas para o
		// mesmo arquivo (mesma malha usada por várias entidades, ou a
		// cena sendo serializada/restaurada no Play/Stop) não reimportam
		// via Assimp, só retornam o LoadedAsset já carregado.
		static LoadedAsset Load(const std::string& filepath);

		// Limpa todo o cache de malhas (ex: ao trocar de projeto).
		static void ClearCache();

		// Remove uma entrada específica do cache — usar ao reimportar um
		// asset (ex: o artista substituiu o .fbx por uma versão nova) pra
		// forçar reler do disco na próxima chamada a Load().
		static void InvalidateCache(const std::string& filepath);

	private:
		static LoadedAsset ProcessMesh(void* aiMeshPtr, const void* aiScenePtr);
	};

} // namespace axe