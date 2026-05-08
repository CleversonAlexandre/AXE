#pragma once

#include "axe/core/types.hpp"
#include "mesh.hpp"
#include <memory>
#include <string>


namespace axe
{
	class Mesh;

	class AXE_API MeshFactory
	{
	public:
		static std::shared_ptr<Mesh> CreateCube();
		static std::shared_ptr<Mesh> CreateSphere(int segments = 16);
		static std::shared_ptr<Mesh> CreatePlane();
		static std::shared_ptr<Mesh> CreateCylinder(int segments = 16);

		//Cria primitiva pelo UUID fixo
		static std::shared_ptr<Mesh> CreateByUUID(const std::string& uuid);

		//Verifica se um UUID é de primitiva
		static bool IsPrimitive(const std::string& uuid);		
	};
}//namespace axe