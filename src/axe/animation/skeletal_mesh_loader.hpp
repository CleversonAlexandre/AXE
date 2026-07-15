#pragma once
#include "axe/core/types.hpp"
#include "axe/animation/skinned_mesh.hpp"
#include "axe/animation/skeleton.hpp"
#include "axe/animation/animation_clip.hpp"

#include <memory>
#include <string>
#include <vector>

namespace axe
{
	struct AXE_API SkeletalAsset
	{
		std::shared_ptr<SkinnedMesh> MeshData;
		std::shared_ptr<Skeleton>    SkeletonData;

		// Clipes embutidos no MESMO arquivo (comum em .gltf, e no fluxo
		// Mixamo de "personagem + 1 animação por fbx").
		std::vector<std::shared_ptr<AnimationClip>> Clips;

		bool IsValid() const
		{
			return MeshData && SkeletonData && !SkeletonData->IsEmpty();
		}
	};

	class AXE_API SkeletalMeshLoader
	{
	public:
		// Importa mesh + esqueleto + clipes embutidos.
		// Cacheado por filepath, na mesma lógica do MeshLoader — sem isso,
		// cada ciclo de Play/Stop reimportaria o FBX inteiro via Assimp.
		// Retorna asset inválido (IsValid() == false) em caso de falha.
		static SkeletalAsset Load(const std::string& filepath);

		// Carrega SÓ as animações de um arquivo, religando-as a um esqueleto
		// já existente por NOME de bone.
		//
		// Este é o fluxo real de produção: o personagem vem de character.fbx,
		// e idle.fbx / run.fbx / attack.fbx trazem só as curvas. Os índices de
		// bone de cada arquivo NÃO batem entre si — o casamento é sempre por
		// nome, via Skeleton::FindBone. Canais cujo bone não existe no
		// esqueleto alvo são descartados.
		static std::vector<std::shared_ptr<AnimationClip>> LoadClips(
			const std::string& filepath,
			const Skeleton& targetSkeleton);

		static void ClearCache();
		static void InvalidateCache(const std::string& filepath);
	};

} // namespace axe