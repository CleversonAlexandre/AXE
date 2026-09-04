#pragma once
// B2.4 — este arquivo mudou de src/axe para src/editor.
//
// O importador de FBX e ferramenta de AUTORIA: ele le formato de
// intercambio e produz os arquivos cozidos que o runtime consome. O
// jogo nunca importa nada, entao o assimp nao precisa estar no axe.dll.
//
// O AXE_API saiu junto: os tipos nao sao mais exportados pela DLL, e
// mante-lo os marcaria como dllimport de simbolos que nao existem la.
//
// Quem liga isto ao runtime e o AssetImportHooks, registrado no boot
// do editor.
#include "axe/core/types.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/material/material.hpp"
#include "axe/asset/asset.hpp"        // ASSET_VIEWER_V2 — AssetImportSettings
#include <memory>
#include <string>

namespace axe
{

	struct LoadedAsset
	{
		std::shared_ptr<Mesh>     MeshData;
		std::shared_ptr<Material> MaterialData;
	};

	class MeshLoader
	{
	public:
		// Retorna asset vazio (MeshData == nullptr) se falhar.
		// Resultado é cacheado por filepath — chamadas repetidas para o
		// mesmo arquivo (mesma malha usada por várias entidades, ou a
		// cena sendo serializada/restaurada no Play/Stop) não reimportam
		// via Assimp, só retornam o LoadedAsset já carregado.
		// quiet — silencia os erros de "arquivo sem malha".
		//
		// SC40: existe para SONDAGEM. O erro serve quando o USUARIO manda
		// importar um arquivo; quando o editor apenas testa "isto por acaso tem
		// malha?" — como o gerador de thumbnails faz em cada FBX da pasta — ele
		// vira um par erro+sucesso por arquivo no log, e um log cheio de erros
		// que nao sao erros treina qualquer um a ignorar o log.
		static LoadedAsset Load(const std::string& filepath, bool quiet = false);

		// Limpa todo o cache de malhas (ex: ao trocar de projeto).
		static void ClearCache();

		// Remove uma entrada específica do cache — usar ao reimportar um
		// asset (ex: o artista substituiu o .fbx por uma versão nova) pra
		// forçar reler do disco na próxima chamada a Load().
		static void InvalidateCache(const std::string& filepath);

	private:
		// ASSET_VIEWER_V2 — recebe as settings do asset para aplicar escala e
		// pivo NOS VERTICES, antes de o Mesh ir para a GPU. Depois disso os
		// dados de CPU nao sao mais transformaveis sem reimportar.
		static LoadedAsset ProcessMesh(void* aiMeshPtr, const void* aiScenePtr,
			const AssetImportSettings& importSettings = {});
	};

} // namespace axe