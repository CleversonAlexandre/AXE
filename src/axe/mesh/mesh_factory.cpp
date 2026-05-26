#include "mesh_factory.hpp"
#include "primitive_uuid.hpp"
#include "axe/log/log.hpp"

namespace axe
{

	static void CalculateTangents(std::vector<Vertex>& vertices,
		const std::vector<uint32_t>& indices)
	{
		// Inicializa tangentes e bitangentes com zero
		for (auto& v : vertices)
		{
			v.Tangent = glm::vec3(0.0f);
			v.Bitangent = glm::vec3(0.0f);
		}

		// Calcula por triângulo
		for (size_t i = 0; i < indices.size(); i += 3)
		{
			Vertex& v0 = vertices[indices[i]];
			Vertex& v1 = vertices[indices[i + 1]];
			Vertex& v2 = vertices[indices[i + 2]];

			glm::vec3 edge1 = v1.Position - v0.Position;
			glm::vec3 edge2 = v2.Position - v0.Position;

			glm::vec2 deltaUV1 = v1.TexCoord - v0.TexCoord;
			glm::vec2 deltaUV2 = v2.TexCoord - v0.TexCoord;

			float det = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
			if (std::abs(det) < 1e-6f) continue;

			float f = 1.0f / det;

			glm::vec3 tangent = f * (deltaUV2.y * edge1 - deltaUV1.y * edge2);
			glm::vec3 bitangent = f * (-deltaUV2.x * edge1 + deltaUV1.x * edge2);

			v0.Tangent += tangent; v1.Tangent += tangent; v2.Tangent += tangent;
			v0.Bitangent += bitangent; v1.Bitangent += bitangent; v2.Bitangent += bitangent;
		}

		// Normaliza
		for (auto& v : vertices)
		{
			if (glm::length(v.Tangent) > 0.0001f)
				v.Tangent = glm::normalize(v.Tangent);
			else
				v.Tangent = glm::vec3(1.0f, 0.0f, 0.0f);

			if (glm::length(v.Bitangent) > 0.0001f)
				v.Bitangent = glm::normalize(v.Bitangent);
			else
				v.Bitangent = glm::vec3(0.0f, 1.0f, 0.0f);
		}
	}


	bool MeshFactory::IsPrimitive(const std::string& uuid)
	{
		return uuid.rfind("primitive-", 0) == 0;
	}

	std::shared_ptr<Mesh> MeshFactory::CreateByUUID(const std::string& uuid)
	{
		if (uuid == PrimitiveUUID::Cube)     return CreateCube();
		if (uuid == PrimitiveUUID::Sphere)   return CreateSphere();
		if (uuid == PrimitiveUUID::Plane)    return CreatePlane();
		if (uuid == PrimitiveUUID::Cylinder) return CreateCylinder();

		AXE_CORE_WARN("MeshFactory: UUID primitivo desconhecido '{}'", uuid);
		return nullptr;
	}

	std::shared_ptr<Mesh> MeshFactory::CreatePlane()
	{
		std::vector<Vertex> vertices = {
			{ { -0.5f, 0.0f, -0.5f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f } },
			{ {  0.5f, 0.0f, -0.5f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } },
			{ {  0.5f, 0.0f,  0.5f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
			{ { -0.5f, 0.0f,  0.5f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
		};

		std::vector<uint32_t> indices = { 0, 1, 2, 2, 3, 0 };
		CalculateTangents(vertices, indices);
		return std::make_shared<Mesh>(vertices, indices);
	}

	std::shared_ptr<Mesh> MeshFactory::CreateCube()
	{
		std::vector<Vertex> vertices = {
			// Frente
			{ { -0.5f, -0.5f,  0.5f }, {  0.0f,  0.0f,  1.0f }, { 0.0f, 0.0f } },
			{ {  0.5f, -0.5f,  0.5f }, {  0.0f,  0.0f,  1.0f }, { 1.0f, 0.0f } },
			{ {  0.5f,  0.5f,  0.5f }, {  0.0f,  0.0f,  1.0f }, { 1.0f, 1.0f } },
			{ { -0.5f,  0.5f,  0.5f }, {  0.0f,  0.0f,  1.0f }, { 0.0f, 1.0f } },
			// Trás
			{ {  0.5f, -0.5f, -0.5f }, {  0.0f,  0.0f, -1.0f }, { 0.0f, 0.0f } },
			{ { -0.5f, -0.5f, -0.5f }, {  0.0f,  0.0f, -1.0f }, { 1.0f, 0.0f } },
			{ { -0.5f,  0.5f, -0.5f }, {  0.0f,  0.0f, -1.0f }, { 1.0f, 1.0f } },
			{ {  0.5f,  0.5f, -0.5f }, {  0.0f,  0.0f, -1.0f }, { 0.0f, 1.0f } },
			// Esquerda
			{ { -0.5f, -0.5f, -0.5f }, { -1.0f,  0.0f,  0.0f }, { 0.0f, 0.0f } },
			{ { -0.5f, -0.5f,  0.5f }, { -1.0f,  0.0f,  0.0f }, { 1.0f, 0.0f } },
			{ { -0.5f,  0.5f,  0.5f }, { -1.0f,  0.0f,  0.0f }, { 1.0f, 1.0f } },
			{ { -0.5f,  0.5f, -0.5f }, { -1.0f,  0.0f,  0.0f }, { 0.0f, 1.0f } },
			// Direita
			{ {  0.5f, -0.5f,  0.5f }, {  1.0f,  0.0f,  0.0f }, { 0.0f, 0.0f } },
			{ {  0.5f, -0.5f, -0.5f }, {  1.0f,  0.0f,  0.0f }, { 1.0f, 0.0f } },
			{ {  0.5f,  0.5f, -0.5f }, {  1.0f,  0.0f,  0.0f }, { 1.0f, 1.0f } },
			{ {  0.5f,  0.5f,  0.5f }, {  1.0f,  0.0f,  0.0f }, { 0.0f, 1.0f } },
			// Topo
			{ { -0.5f,  0.5f,  0.5f }, {  0.0f,  1.0f,  0.0f }, { 0.0f, 0.0f } },
			{ {  0.5f,  0.5f,  0.5f }, {  0.0f,  1.0f,  0.0f }, { 1.0f, 0.0f } },
			{ {  0.5f,  0.5f, -0.5f }, {  0.0f,  1.0f,  0.0f }, { 1.0f, 1.0f } },
			{ { -0.5f,  0.5f, -0.5f }, {  0.0f,  1.0f,  0.0f }, { 0.0f, 1.0f } },
			// Base
			{ { -0.5f, -0.5f, -0.5f }, {  0.0f, -1.0f,  0.0f }, { 0.0f, 0.0f } },
			{ {  0.5f, -0.5f, -0.5f }, {  0.0f, -1.0f,  0.0f }, { 1.0f, 0.0f } },
			{ {  0.5f, -0.5f,  0.5f }, {  0.0f, -1.0f,  0.0f }, { 1.0f, 1.0f } },
			{ { -0.5f, -0.5f,  0.5f }, {  0.0f, -1.0f,  0.0f }, { 0.0f, 1.0f } },
		};

		std::vector<uint32_t> indices = {
			 0,  1,  2,  2,  3,  0,  // Frente
			 4,  5,  6,  6,  7,  4,  // Trás
			 8,  9, 10, 10, 11,  8,  // Esquerda
			12, 13, 14, 14, 15, 12,  // Direita
			16, 17, 18, 18, 19, 16,  // Topo
			20, 21, 22, 22, 23, 20,  // Base
		};

		CalculateTangents(vertices, indices);
		return std::make_shared<Mesh>(vertices, indices);
	}


	std::shared_ptr<Mesh> MeshFactory::CreateCylinder(int segments)
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		float height = 1.0f;
		float radius = 0.5f;

		// Tampa superior e inferior + lateral
		for (int i = 0; i <= segments; i++)
		{
			float angle = glm::two_pi<float>() * i / segments;
			float x = radius * std::cos(angle);
			float z = radius * std::sin(angle);

			// Lateral top
			vertices.push_back({ {x, height * 0.5f, z}, glm::normalize(glm::vec3(x, 0, z)), {(float)i / segments, 1.0f} });
			// Lateral bottom
			vertices.push_back({ {x, -height * 0.5f, z}, glm::normalize(glm::vec3(x, 0, z)), {(float)i / segments, 0.0f} });
		}

		// Lateral faces
		for (int i = 0; i < segments; i++)
		{
			uint32_t top1 = i * 2;
			uint32_t bottom1 = i * 2 + 1;
			uint32_t top2 = (i + 1) * 2;
			uint32_t bottom2 = (i + 1) * 2 + 1;

			indices.push_back(top1);
			indices.push_back(bottom1);
			indices.push_back(top2);

			indices.push_back(bottom1);
			indices.push_back(bottom2);
			indices.push_back(top2);
		}

		uint32_t baseIndex = (uint32_t)vertices.size();

		// Centro superior
		vertices.push_back({ {0.0f, height * 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f} });
		uint32_t topCenter = baseIndex++;

		// Centro inferior
		vertices.push_back({ {0.0f, -height * 0.5f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.5f, 0.5f} });
		uint32_t bottomCenter = baseIndex++;

		// Tampas
		for (int i = 0; i < segments; i++)
		{
			float a1 = glm::two_pi<float>() * i / segments;
			float a2 = glm::two_pi<float>() * (i + 1) / segments;

			// Top
			vertices.push_back({ {radius * std::cos(a1), height * 0.5f, radius * std::sin(a1)}, {0,1,0}, {0.5f + 0.5f * std::cos(a1), 0.5f + 0.5f * std::sin(a1)} });
			vertices.push_back({ {radius * std::cos(a2), height * 0.5f, radius * std::sin(a2)}, {0,1,0}, {0.5f + 0.5f * std::cos(a2), 0.5f + 0.5f * std::sin(a2)} });

			indices.push_back(topCenter);
			indices.push_back(baseIndex);
			indices.push_back(baseIndex + 1);
			baseIndex += 2;

			// Bottom
			vertices.push_back({ {radius * std::cos(a1), -height * 0.5f, radius * std::sin(a1)}, {0,-1,0}, {0.5f + 0.5f * std::cos(a1), 0.5f + 0.5f * std::sin(a1)} });
			vertices.push_back({ {radius * std::cos(a2), -height * 0.5f, radius * std::sin(a2)}, {0,-1,0}, {0.5f + 0.5f * std::cos(a2), 0.5f + 0.5f * std::sin(a2)} });

			indices.push_back(bottomCenter);
			indices.push_back(baseIndex + 1);
			indices.push_back(baseIndex);
			baseIndex += 2;
		}

		CalculateTangents(vertices, indices);
		return std::make_shared<Mesh>(vertices, indices);
	}

	std::shared_ptr<Mesh> MeshFactory::CreateSphere(int segments)
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		float radius = 0.5f;
		int rings = segments;
		int slices = segments;

		for (int r = 0; r <= rings; r++)
		{
			float phi = glm::pi<float>() * r / rings;
			for (int s = 0; s <= slices; s++)
			{
				float theta = glm::two_pi<float>() * s / slices;

				float x = radius * std::sin(phi) * std::cos(theta);
				float y = radius * std::cos(phi);
				float z = radius * std::sin(phi) * std::sin(theta);

				glm::vec3 normal = glm::normalize(glm::vec3(x, y, z));
				glm::vec2 uv = { (float)s / slices, (float)r / rings };

				vertices.push_back({ {x, y, z}, normal, uv });
			}
		}

		for (int r = 0; r < rings; r++)
		{
			for (int s = 0; s < slices; s++)
			{
				uint32_t a = r * (slices + 1) + s;
				uint32_t b = a + slices + 1;

				indices.push_back(a);
				indices.push_back(b);
				indices.push_back(a + 1);

				indices.push_back(b);
				indices.push_back(b + 1);
				indices.push_back(a + 1);
			}
		}

		CalculateTangents(vertices, indices);
		return std::make_shared<Mesh>(vertices, indices);
	}

} // namespace axe