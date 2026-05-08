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
		// Retorna asset vazio (MeshData == nullptr) se falhar
		static LoadedAsset Load(const std::string& filepath);

	private:
		static LoadedAsset ProcessMesh(void* aiMeshPtr, const void* aiScenePtr);
	};

} // namespace axe