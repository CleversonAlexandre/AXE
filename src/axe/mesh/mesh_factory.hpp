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
		static std::shared_ptr<Mesh> CreateCamera();

		//Cria primitiva pelo UUID fixo
		static std::shared_ptr<Mesh> CreateByUUID(const std::string& uuid);

		//Verifica se um UUID é de primitiva
		static bool IsPrimitive(const std::string& uuid);

		// ── SC25: UUID -> malha, seja ela primitiva ou asset ──────────────────
		//
		// A regra "primitiva vem da fabrica, asset vem do MeshLoader" estava
		// escrita a mao em QUATRO lugares — SceneSerializer, preview do Script
		// Editor (dois pontos) e a instanciacao de um script na cena. Tres
		// deles chamavam CreateByUUID direto, que devolve nada para um UUID de
		// asset: era por isso que uma malha importada sumia ao ser posta na
		// cena e ao ser trocada no painel, enquanto funcionava ao abrir a cena.
		//
		// Nao e coincidencia terem divergido: sao quatro copias de uma decisao
		// que nunca teve dono. Agora tem. Devolve nullptr quando o UUID nao
		// resolve — quem chama decide o fallback.
		static std::shared_ptr<Mesh> ResolveByUUID(const std::string& uuid);
	};
}//namespace axe