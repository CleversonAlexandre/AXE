#include "mesh_loader.hpp"
#include "axe/log/log.hpp"
#include "axe/graphics/shader.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <filesystem>
#include <unordered_map>

namespace axe
{

	static std::shared_ptr<Shader> s_DefaultShader;

	static std::shared_ptr<Shader> GetOrCreateShader()
	{
		if (s_DefaultShader) return s_DefaultShader;

		const std::string vertexSrc = R"(
		#version 460 core
		layout(location = 0) in vec3 a_Position;
		layout(location = 1) in vec3 a_Normal;
		layout(location = 2) in vec2 a_TexCoord;
		uniform mat4 u_Model;
		uniform mat4 u_ViewProjection;
		uniform mat3 u_NormalMatrix;
		out vec3 v_Normal;
		out vec3 v_FragPos;
		void main()
		{
			vec4 worldPos = u_Model * vec4(a_Position, 1.0);
			v_FragPos  = worldPos.xyz;
			v_Normal   = normalize(u_NormalMatrix * a_Normal);
			gl_Position = u_ViewProjection * worldPos;
		}
	)";

		const std::string fragmentSrc = R"(
		#version 460 core
		layout(location = 0) out vec4 FragColor;
		in vec3 v_Normal;
		in vec3 v_FragPos;
		uniform vec4  u_Color;
		uniform float u_SpecularStrength;
		uniform float u_Shininess;
		uniform vec3  u_LightDirection;
		uniform vec3  u_LightColor;
		uniform float u_LightIntensity;
		uniform float u_AmbientStrength;
		uniform vec3  u_CameraPosition;
		uniform bool  u_HasLight;
		void main()
		{
			vec3 objectColor = u_Color.rgb;
			if (!u_HasLight) { FragColor = vec4(objectColor, u_Color.a); return; }
			vec3 normal   = normalize(v_Normal);
			vec3 lightDir = normalize(-u_LightDirection);
			vec3 ambient  = u_AmbientStrength * u_LightColor;
			float diff    = max(dot(normal, lightDir), 0.0);
			vec3 diffuse  = diff * u_LightColor * u_LightIntensity;
			vec3 viewDir  = normalize(u_CameraPosition - v_FragPos);
			vec3 halfDir  = normalize(lightDir + viewDir);
			float spec    = pow(max(dot(normal, halfDir), 0.0), u_Shininess);
			vec3 specular = u_SpecularStrength * spec * u_LightColor;
			FragColor = vec4((ambient + diffuse + specular) * objectColor, u_Color.a);
		}
	)";

		s_DefaultShader = Shader::Create(vertexSrc, fragmentSrc);
		return s_DefaultShader;
	}

	// Cache por filepath — evita reimportar via Assimp toda vez que a mesma
	// malha é referenciada por outra entidade, ou que a cena é
	// (re)serializada (abrir cena, entrar no Play, sair do Play/Stop).
	// LoadedAsset guarda só shared_ptr<Mesh>/shared_ptr<Material> — Mesh é
	// somente-leitura após construído (VAO/VBO/IBO + vértices pra física),
	// então é seguro compartilhar a mesma instância entre N entidades.
	static std::unordered_map<std::string, LoadedAsset> s_MeshCache;

	void MeshLoader::ClearCache()
	{
		s_MeshCache.clear();
	}

	void MeshLoader::InvalidateCache(const std::string& filepath)
	{
		s_MeshCache.erase(filepath);
	}

	LoadedAsset MeshLoader::Load(const std::string& filepath)
	{
		auto cached = s_MeshCache.find(filepath);
		if (cached != s_MeshCache.end())
			return cached->second;

		Assimp::Importer importer;

		const aiScene* scene = importer.ReadFile(filepath,
			aiProcess_Triangulate |
			aiProcess_GenSmoothNormals | // só gera se o mesh não tiver normais
			aiProcess_CalcTangentSpace
		);

		// Mesmo bug que o SkeletalMeshLoader tinha: o Assimp marca
		// AI_SCENE_FLAGS_INCOMPLETE em QUALQUER cena sem malha — e isso nao e
		// um erro, entao GetErrorString() volta VAZIO. Tratar isso como falha
		// produz "falhou, motivo: (nada)", e o usuario vai caçar arquivo
		// corrompido, importer faltando, DLL errada... com o arquivo perfeito.
		if (!scene || !scene->mRootNode)
		{
			const char* err = importer.GetErrorString();

			AXE_CORE_ERROR("MeshLoader: falha ao carregar '{}': {}", filepath,
				(err && *err) ? err : "(o Assimp nao devolveu mensagem de erro)");

			// JSON mandado pro Assimp: um asset do engine (.axeskel, .axeanim)
			// chegou aqui por engano. O "unexpected colon" e o dois-pontos do
			// proprio JSON.
			const std::string ext = std::filesystem::path(filepath).extension().string();

			if (ext == ".axeskel" || ext == ".axeanim" || ext == ".axemat")
			{
				AXE_CORE_ERROR("  -> '{}' e um ASSET DO ENGINE (JSON), nao um modelo. "
					"Alguma entidade da cena aponta pra ele como se fosse mesh — "
					"apague essa entidade e recrie arrastando o asset certo.", ext);
			}

			return {};
		}

		if (scene->mNumMeshes == 0 && scene->mNumAnimations > 0)
		{
			AXE_CORE_ERROR("MeshLoader: '{}' nao contem malha — e um arquivo SO DE ANIMACAO "
				"({} clipe(s)). Use 'Importar animacao...' no Inspector do personagem, "
				"nao o Asset Browser.", filepath, scene->mNumAnimations);
			return {};
		}

		if (scene->mNumMeshes == 0)
		{
			//AXE_CORE_ERROR("MeshLoader: nenhuma mesh encontrada em '{}'", filepath);
			return {};
		}

		//AXE_CORE_INFO("MeshLoader: carregando '{}' ({} mesh(es) encontrada(s))", filepath, scene->mNumMeshes);

		if (scene->mNumMeshes == 1)
		{
			LoadedAsset single = ProcessMesh(scene->mMeshes[0], scene);
			s_MeshCache[filepath] = single;
			return single;
		}

		// Múltiplos meshes — combina vértices e índices direto do Assimp
		std::vector<Vertex>   allVertices;
		std::vector<uint32_t> allIndices;

		for (uint32_t m = 0; m < scene->mNumMeshes; m++)
		{
			aiMesh* aiMesh = scene->mMeshes[m];
			uint32_t vertexOffset = (uint32_t)allVertices.size();

			for (uint32_t i = 0; i < aiMesh->mNumVertices; i++)
			{
				Vertex v;
				v.Position = { aiMesh->mVertices[i].x, aiMesh->mVertices[i].y, aiMesh->mVertices[i].z };
				if (aiMesh->HasNormals())
					v.Normal = { aiMesh->mNormals[i].x, aiMesh->mNormals[i].y, aiMesh->mNormals[i].z };
				if (aiMesh->mTextureCoords[0])
					v.TexCoord = { aiMesh->mTextureCoords[0][i].x, aiMesh->mTextureCoords[0][i].y };
				if (aiMesh->HasTangentsAndBitangents())
				{
					v.Tangent = { aiMesh->mTangents[i].x,   aiMesh->mTangents[i].y,   aiMesh->mTangents[i].z };
					v.Bitangent = { aiMesh->mBitangents[i].x, aiMesh->mBitangents[i].y, aiMesh->mBitangents[i].z };
				}
				allVertices.push_back(v);
			}

			for (uint32_t i = 0; i < aiMesh->mNumFaces; i++)
			{
				const aiFace& face = aiMesh->mFaces[i];
				for (uint32_t j = 0; j < face.mNumIndices; j++)
					allIndices.push_back(face.mIndices[j] + vertexOffset);
			}
		}

		AXE_CORE_INFO("MeshLoader: combinado {} vértices, {} índices", allVertices.size(), allIndices.size());

		LoadedAsset combined;
		combined.MeshData = std::make_shared<Mesh>(allVertices, allIndices);
		s_MeshCache[filepath] = combined;
		return combined;
	}

	LoadedAsset MeshLoader::ProcessMesh(void* aiMeshPtr, const void* aiScenePtr)
	{
		aiMesh* mesh = static_cast<aiMesh*>(aiMeshPtr);
		const aiScene* scene = static_cast<const aiScene*>(aiScenePtr);

		std::vector<Vertex>   vertices;
		std::vector<uint32_t> indices;

		vertices.reserve(mesh->mNumVertices);
		for (uint32_t i = 0; i < mesh->mNumVertices; i++)
		{
			Vertex v;
			v.Position = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };

			if (mesh->HasNormals())
				v.Normal = { mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z };

			if (mesh->mTextureCoords[0])
				v.TexCoord = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
			else
				v.TexCoord = { 0.0f, 0.0f };

			if (mesh->HasTangentsAndBitangents())
			{
				v.Tangent = { mesh->mTangents[i].x,   mesh->mTangents[i].y,   mesh->mTangents[i].z };
				v.Bitangent = { mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z };
			}
			else
			{
				v.Tangent = { 1.0f, 0.0f, 0.0f };
				v.Bitangent = { 0.0f, 1.0f, 0.0f };
			}

			vertices.push_back(v);
		}

		indices.reserve(mesh->mNumFaces * 3);
		for (uint32_t i = 0; i < mesh->mNumFaces; i++)
		{
			const aiFace& face = mesh->mFaces[i];
			for (uint32_t j = 0; j < face.mNumIndices; j++)
				indices.push_back(face.mIndices[j]);
		}

		//AXE_CORE_INFO("MeshLoader: {} vértices, {} índices", vertices.size(), indices.size());

		LoadedAsset asset;
		asset.MeshData = std::make_shared<Mesh>(vertices, indices);

		// Material
		if (mesh->mMaterialIndex < scene->mNumMaterials)
		{
			aiMaterial* aiMat = scene->mMaterials[mesh->mMaterialIndex];

			aiColor4D diffuse(0.7f, 0.7f, 0.7f, 1.0f);
			aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);

			aiString matName;
			aiMat->Get(AI_MATKEY_NAME, matName);
			std::string name = matName.C_Str();
			if (name.empty()) name = "Material";

			auto material = std::make_shared<Material>(GetOrCreateShader(), name);
			material->Color = { diffuse.r, diffuse.g, diffuse.b, diffuse.a };
			asset.MaterialData = material;

			//AXE_CORE_INFO("MeshLoader: cor do material ({}, {}, {}, {})",
			//	diffuse.r, diffuse.g, diffuse.b, diffuse.a);
		}

		return asset;
	}

} // namespace axe