#include "skeletal_mesh_loader.hpp"
#include "axe/log/log.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>

#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace axe
{
	namespace
	{
		// aiMatrix4x4 é ROW-major; glm::mat4 é COLUMN-major. Sem a transposta
		// a hierarquia inteira sai torcida — e o sintoma (personagem
		// espaguete) parece bug de peso, não de matriz. Custou horas a muita
		// gente; está isolado aqui de propósito.
		glm::mat4 ToGlm(const aiMatrix4x4& m)
		{
			glm::mat4 r;
			r[0][0] = m.a1; r[1][0] = m.a2; r[2][0] = m.a3; r[3][0] = m.a4;
			r[0][1] = m.b1; r[1][1] = m.b2; r[2][1] = m.b3; r[3][1] = m.b4;
			r[0][2] = m.c1; r[1][2] = m.c2; r[2][2] = m.c3; r[3][2] = m.c4;
			r[0][3] = m.d1; r[1][3] = m.d2; r[2][3] = m.d3; r[3][3] = m.d4;
			return r;
		}

		glm::vec3 ToGlm(const aiVector3D& v) { return { v.x, v.y, v.z }; }

		// Le o UnitScaleFactor dos metadados e devolve o fator que converte
		// as unidades do arquivo para METROS.
		//
		// No FBX, UnitScaleFactor = quantos CENTIMETROS vale uma unidade.
		// Mixamo grava 1.0 -> 1 unidade = 1 cm -> fator 0.01.
		// Um FBX exportado em metros gravaria 100.0 -> fator 1.0.
		//
		// glTF e .obj nao tem esse metadado: Get() falha e devolvemos 1.0
		// (eles ja estao em metros por especificacao).
		float ReadUnitScale(const aiScene* scene)
		{
			if (!scene || !scene->mMetaData)
				return 1.0f;

			double cmPerUnit = 0.0;

			if (!scene->mMetaData->Get("UnitScaleFactor", cmPerUnit) || cmPerUnit <= 0.0)
				return 1.0f;

			return static_cast<float>(cmPerUnit / 100.0);
		}

		// Assimp entrega (w,x,y,z); glm::quat também. Cuidado ao "otimizar".
		glm::quat ToGlm(const aiQuaternion& q) { return glm::quat(q.w, q.x, q.y, q.z); }

		// ─────────────────────────────────────────────────────────────────
		// PRESERVE_PIVOTS = false  — OBRIGATORIO, e nao e opcional.
		//
		// Por padrao o importer de FBX do Assimp QUEBRA o transform de cada
		// osso numa cadeia de nos auxiliares:
		//
		//     mixamorig:Hips_$AssimpFbx$_Translation
		//     mixamorig:Hips_$AssimpFbx$_Rotation
		//     mixamorig:Hips_$AssimpFbx$_Scaling
		//     mixamorig:Hips
		//
		// Cada um carrega SO UM PEDACO do transform. Um rig da Mixamo de ~65
		// ossos vira 147 "ossos" — numero que ja denuncia o problema.
		//
		// A bind pose sobrevive (a cadeia se compoe certo se ninguem mexer),
		// mas a ANIMACAO destroi o personagem: os canais miram esses nos
		// parciais, e qualquer amostragem que sobrescreva o transform inteiro
		// apaga o pedaco que aquele no guardava.
		//
		// Com false, o Assimp colapsa tudo num no por osso. E o que todo
		// engine faz. Tem que valer para Load() E LoadClips() — se um usar e
		// o outro nao, os NOMES dos nos nao batem e o religamento por nome
		// falha silenciosamente.
		// ─────────────────────────────────────────────────────────────────
		void ConfigureImporter(Assimp::Importer& importer)
		{
			importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
		}

		constexpr unsigned int kImportFlags =
			aiProcess_Triangulate |
			aiProcess_GenSmoothNormals |
			aiProcess_CalcTangentSpace |
			// Corta influências além de 4 e RENORMALIZA os pesos restantes.
			// Sem isto, uma malha do Mixamo com 6 influências por vértice
			// perde as 2 últimas silenciosamente e o vértice encolhe.
			aiProcess_LimitBoneWeights;

		// ---------------------------------------------------------------
		// Passo 1 — quais nomes de node são bones de verdade (têm vértices)
		// ---------------------------------------------------------------
		void CollectBoneOffsets(const aiScene* scene,
			std::unordered_map<std::string, glm::mat4>& outOffsets)
		{
			for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
			{
				const aiMesh* mesh = scene->mMeshes[m];
				for (unsigned int b = 0; b < mesh->mNumBones; ++b)
				{
					const aiBone* bone = mesh->mBones[b];
					outOffsets[bone->mName.C_Str()] = ToGlm(bone->mOffsetMatrix);
				}
			}
		}

		// ---------------------------------------------------------------
		// Passo 2 — marcar os nodes que precisam entrar no esqueleto
		//
		// Não basta pegar só os nodes que são bones: os nodes INTERMEDIÁRIOS
		// entre a raiz e um bone carregam transforms que fazem parte da
		// cadeia. Pular um "Armature" do Blender, por exemplo, perde a
		// rotação de 90° que ele aplica — e o personagem nasce deitado.
		//
		// Regra: um node entra se ELE é um bone OU se algum descendente é.
		// Retorna true se este node deve entrar.
		// ---------------------------------------------------------------
		bool MarkRequiredNodes(const aiNode* node,
			const std::unordered_map<std::string, glm::mat4>& boneOffsets,
			std::unordered_set<const aiNode*>& outRequired)
		{
			bool required = boneOffsets.find(node->mName.C_Str()) != boneOffsets.end();

			for (unsigned int i = 0; i < node->mNumChildren; ++i)
			{
				if (MarkRequiredNodes(node->mChildren[i], boneOffsets, outRequired))
					required = true;
			}

			if (required)
				outRequired.insert(node);

			return required;
		}

		// ---------------------------------------------------------------
		// Passo 3 — DFS construindo o Skeleton em ORDEM TOPOLÓGICA
		//
		// O pai é sempre adicionado antes de descer nos filhos. É essa
		// travessia que sustenta a invariante que o AnimationSampler usa pra
		// resolver a pose num único loop linear.
		// ---------------------------------------------------------------
		void BuildSkeletonRecursive(const aiNode* node,
			int parentIndex,
			const std::unordered_map<std::string, glm::mat4>& boneOffsets,
			const std::unordered_set<const aiNode*>& required,
			Skeleton& skeleton)
		{
			if (required.find(node) == required.end())
				return;

			const std::string name = node->mName.C_Str();

			// Node de hierarquia pura (não tem vértices atrelados): offset
			// identidade. Ele existe só pra propagar o transform pros filhos.
			glm::mat4 inverseBind(1.0f);
			auto it = boneOffsets.find(name);
			if (it != boneOffsets.end())
				inverseBind = it->second;

			const int myIndex = skeleton.AddBone(
				name,
				parentIndex,
				inverseBind,
				ToGlm(node->mTransformation)   // local bind pose
			);

			for (unsigned int i = 0; i < node->mNumChildren; ++i)
				BuildSkeletonRecursive(node->mChildren[i], myIndex, boneOffsets, required, skeleton);
		}

		// ---------------------------------------------------------------
		// Passo 5 — converter uma aiAnimation em AnimationClip
		//
		// Tempo: o Assimp trabalha em TICKS. Convertemos tudo pra SEGUNDOS
		// aqui, na borda do sistema, pra que nada além do loader precise
		// saber o que é um tick.
		// ---------------------------------------------------------------
		// positionScale: corrige translacoes quando o arquivo de ANIMACAO tem
		// unidade diferente da do PERSONAGEM.
		//
		// A escala do personagem ja entra na GlobalInverse, no fim da cadeia.
		// Mas as translacoes de um clipe vindo de outro arquivo estao nas
		// unidades DAQUELE arquivo. Se um veio em cm e o outro em m, os ossos
		// se deslocam 100x demais e o personagem estica — com a rotacao
		// perfeitamente correta, o que torna o bug bem dificil de ler.
		//
		// Rotacao e escala nao precisam de correcao: quaternion e adimensional,
		// e escala e razao.
		std::shared_ptr<AnimationClip> BuildClip(const aiAnimation* anim,
			const Skeleton& skeleton,
			float positionScale)
		{
			// mTicksPerSecond == 0 acontece em FBX mal exportado. 25 é o
			// fallback do próprio Assimp.
			double tps = anim->mTicksPerSecond;
			if (tps <= 0.0)
			{
				tps = 25.0;
				AXE_CORE_WARN("SkeletalMeshLoader: clipe '{}' sem TicksPerSecond; assumindo 25.",
					anim->mName.C_Str());
			}

			auto clip = std::make_shared<AnimationClip>();

			std::string name = anim->mName.C_Str();
			if (name.empty())
				name = "Unnamed";

			clip->SetName(name);
			clip->SetDuration(static_cast<float>(anim->mDuration / tps));
			clip->SetLooping(true);

			int discarded = 0;
			int recovered = 0;
			std::vector<std::string> discardedNames;

			// ── Fallback de casamento por SUFIXO ──────────────────────────
			//
			// FBX de fontes diferentes (mesmo do proprio Mixamo) troca o
			// prefixo de namespace dos ossos: "mixamorig:Hips" num arquivo,
			// "mixamorig1:Hips" ou "Armature|mixamorig:Hips" no outro. O
			// casamento exato descarta TODOS esses canais — e o resultado e
			// aquela animacao que "pega so em parte dos ossos".
			//
			// A saida e a mesma dos engines grandes: se o nome exato falhar,
			// casar pelo sufixo apos o ultimo ':' ou '|'. Sufixo ambiguo
			// (dois ossos terminando igual) fica de fora — ai e melhor
			// descartar do que animar o osso errado.
			auto tailOf = [](const std::string& s) -> std::string
				{
					const std::size_t p = s.find_last_of(":|");
					return (p == std::string::npos) ? s : s.substr(p + 1);
				};

			std::unordered_map<std::string, int> bySuffix;

			for (std::size_t b = 0; b < skeleton.GetBones().size(); ++b)
			{
				const std::string tail = tailOf(skeleton.GetBones()[b].Name);
				auto it = bySuffix.find(tail);

				if (it == bySuffix.end()) bySuffix[tail] = (int)b;
				else                      it->second = -2;   // ambiguo
			}

			for (unsigned int c = 0; c < anim->mNumChannels; ++c)
			{
				const aiNodeAnim* ch = anim->mChannels[c];

				// Casamento por NOME. É isto que permite um clipe vindo de
				// outro arquivo (idle.fbx) tocar neste esqueleto.
				int boneIndex = skeleton.FindBone(ch->mNodeName.C_Str());

				if (boneIndex < 0)
				{
					auto it = bySuffix.find(tailOf(ch->mNodeName.C_Str()));

					if (it != bySuffix.end() && it->second >= 0)
					{
						boneIndex = it->second;
						++recovered;
					}
				}

				if (boneIndex < 0)
				{
					++discarded;
					discardedNames.push_back(ch->mNodeName.C_Str());
					continue;
				}

				BoneChannel channel;
				channel.BoneIndex = boneIndex;

				channel.PositionKeys.reserve(ch->mNumPositionKeys);
				for (unsigned int k = 0; k < ch->mNumPositionKeys; ++k)
				{
					VectorKey key;
					key.Time = static_cast<float>(ch->mPositionKeys[k].mTime / tps);
					key.Value = ToGlm(ch->mPositionKeys[k].mValue) * positionScale;
					channel.PositionKeys.push_back(key);
				}

				channel.RotationKeys.reserve(ch->mNumRotationKeys);
				for (unsigned int k = 0; k < ch->mNumRotationKeys; ++k)
				{
					QuatKey key;
					key.Time = static_cast<float>(ch->mRotationKeys[k].mTime / tps);
					key.Value = ToGlm(ch->mRotationKeys[k].mValue);
					channel.RotationKeys.push_back(key);
				}

				channel.ScaleKeys.reserve(ch->mNumScalingKeys);
				for (unsigned int k = 0; k < ch->mNumScalingKeys; ++k)
				{
					VectorKey key;
					key.Time = static_cast<float>(ch->mScalingKeys[k].mTime / tps);
					key.Value = ToGlm(ch->mScalingKeys[k].mValue);
					channel.ScaleKeys.push_back(key);
				}

				clip->AddChannel(channel);
			}

			if (recovered > 0)
			{
				AXE_CORE_INFO("SkeletalMeshLoader: clipe '{}' — {} canal(is) casado(s) por sufixo (prefixos de namespace diferentes).",
					clip->GetName(), recovered);
			}

			if (discarded > 0)
			{
				// Separa o LIXO DE EXPORT (containers e pontas de cadeia, que
				// nao deformam vertice nenhum) dos nomes SUSPEITOS (osso de
				// verdade ausente do esqueleto — ai a animacao fica parcial e
				// merece um warning de verdade).
				//
				// Ponta de cadeia: termina em "_End", ou termina em numero N
				// e o esqueleto TEM o mesmo nome com N-1 (LeftHandThumb4 e
				// ponta porque LeftHandThumb3 existe).
				auto isHarmless = [&](const std::string& full) -> bool
					{
						const std::string t = tailOf(full);

						if (t == "Armature" || t == "RootNode" || t == "Scene")
							return true;

						if (t.size() > 4 && t.compare(t.size() - 4, 4, "_End") == 0)
							return true;

						if (!t.empty() && t.back() > '1' && t.back() <= '9')
						{
							std::string prev = t;
							prev.back() = char(prev.back() - 1);

							if (bySuffix.count(prev))
								return true;
						}

						return false;
					};

				std::string suspicious;
				int suspiciousCount = 0;

				for (const auto& dn : discardedNames)
				{
					if (isHarmless(dn))
						continue;

					if (suspiciousCount < 20)
					{
						if (suspiciousCount) suspicious += ", ";
						suspicious += dn;
					}

					++suspiciousCount;
				}

				if (suspiciousCount == 0)
				{
					AXE_CORE_INFO("SkeletalMeshLoader: clipe '{}' — {} canal(is) de nos terminais/containers ignorado(s) (nao deformam a malha).",
						clip->GetName(), discarded);
				}
				else
				{
					AXE_CORE_WARN("SkeletalMeshLoader: clipe '{}' — {} de {} canal(is) descartado(s) (bone ausente no esqueleto): {}",
						clip->GetName(), suspiciousCount, (int)anim->mNumChannels, suspicious);
				}
			}

			return clip;
		}

		// Cache, mesma lógica do MeshLoader: sem ele, cada Play/Stop
		// reimporta o FBX inteiro (parse + skinning + clipes) do zero.
		std::unordered_map<std::string, SkeletalAsset> s_Cache;
	}

	void SkeletalMeshLoader::ClearCache()
	{
		s_Cache.clear();
	}

	void SkeletalMeshLoader::InvalidateCache(const std::string& filepath)
	{
		s_Cache.erase(filepath);
	}

	SkeletalAsset SkeletalMeshLoader::Load(const std::string& filepath)
	{
		auto cached = s_Cache.find(filepath);
		if (cached != s_Cache.end())
			return cached->second;

		Assimp::Importer importer;
		ConfigureImporter(importer);

		const aiScene* scene = importer.ReadFile(filepath, kImportFlags);

		// Falha REAL: o Assimp nem conseguiu abrir/parsear o arquivo.
		if (!scene || !scene->mRootNode)
		{
			const char* err = importer.GetErrorString();

			AXE_CORE_ERROR("SkeletalMeshLoader: falha ao carregar '{}': {}",
				filepath, (err && *err) ? err : "(o Assimp nao devolveu mensagem de erro)");

			const std::string ext = std::filesystem::path(filepath).extension().string();

			if (!ext.empty() && !importer.IsExtensionSupported(ext))
			{
				aiString list;
				importer.GetExtensionList(list);

				AXE_CORE_ERROR("  -> O Assimp desta build NAO suporta '{}'.", ext);
				AXE_CORE_ERROR("  -> Formatos suportados: {}", list.C_Str());
			}

			return {};
		}

		// ─────────────────────────────────────────────────────────────────
		// CENA SEM MALHA = ARQUIVO SO DE ANIMACAO
		//
		// Este e o caso que confunde todo mundo, inclusive eu:
		//
		// O Assimp marca a cena com AI_SCENE_FLAGS_INCOMPLETE quando ela nao
		// tem malha. Mas isso NAO e um erro — entao GetErrorString() volta
		// VAZIO. O sintoma vira "falha ao carregar, motivo: (nada)", e voce
		// vai procurar arquivo corrompido, importer faltando, DLL errada...
		// quando o arquivo esta perfeito e so nao e o que voce pensa que e.
		//
		// E exatamente o que acontece com os FBX "Without Skin" da Mixamo
		// (idle.fbx, run.fbx): eles tem SO as curvas de animacao.
		//
		// Por isso o check de INCOMPLETE saiu do if la de cima: uma cena
		// pode vir marcada como incompleta e ainda assim ter a malha que
		// precisamos.
		// ─────────────────────────────────────────────────────────────────
		if (scene->mNumMeshes == 0)
		{
			if (scene->mNumAnimations > 0)
			{
				AXE_CORE_ERROR("SkeletalMeshLoader: '{}' nao contem malha — e um arquivo SO DE ANIMACAO "
					"({} clipe(s)).", filepath, scene->mNumAnimations);
				AXE_CORE_ERROR("  -> Importe primeiro o PERSONAGEM (na Mixamo: download 'With Skin'), "
					"e depois use 'Importar animacao...' no Inspector para adicionar este arquivo.");
			}
			else
			{
				AXE_CORE_ERROR("SkeletalMeshLoader: '{}' nao contem malha nem animacao — "
					"arquivo vazio ou nao suportado.", filepath);
			}

			return {};
		}

		// --- Passo 1: coletar offsets de todos os bones de todas as meshes
		std::unordered_map<std::string, glm::mat4> boneOffsets;
		CollectBoneOffsets(scene, boneOffsets);

		if (boneOffsets.empty())
		{
			AXE_CORE_ERROR("SkeletalMeshLoader: '{}' nao tem bones — use MeshLoader para malhas estaticas.",
				filepath);
			return {};
		}

		// --- Passo 2 + 3: hierarquia -> Skeleton em ordem topológica
		std::unordered_set<const aiNode*> required;
		MarkRequiredNodes(scene->mRootNode, boneOffsets, required);

		auto skeleton = std::make_shared<Skeleton>();
		skeleton->SetName(filepath);

		// ── CONVERSAO DE UNIDADE ─────────────────────────────────────────
		//
		// Aplicada na GlobalInverse, e nao nos vertices. Motivo:
		//
		//     Skin[i] = GlobalInverse * Global[i] * InverseBindPose[i]
		//
		// A GlobalInverse e o ULTIMO fator da cadeia. Multiplicar ela pela
		// escala escala TUDO de uma vez: malha, ossos, translacoes dos
		// clipes e ate as linhas do debug de esqueleto.
		//
		// A alternativa — escalar os vertices na CPU — obrigaria a escalar
		// tambem as InverseBindPose E as translacoes de todos os clipes, em
		// tres lugares diferentes. Esquecer um deles produz um personagem
		// esticado, e o bug e horrivel de achar.
		const float unitScale = ReadUnitScale(scene);
		skeleton->SetUnitScale(unitScale);

		const glm::mat4 unitFix = glm::scale(glm::mat4(1.0f), glm::vec3(unitScale));

		// Cancela o transform global do node raiz (rotação de eixos do
		// Blender etc) e, na sequencia, converte a unidade.
		skeleton->SetGlobalInverseTransform(
			unitFix * glm::inverse(ToGlm(scene->mRootNode->mTransformation)));

		if (unitScale != 1.0f)
		{
			AXE_CORE_INFO("SkeletalMeshLoader: '{}' em unidade nao-metrica "
				"(UnitScaleFactor); aplicando escala {:.4f} -> metros. "
				"A entidade pode ficar com Scale 1.0.",
				filepath, unitScale);
		}

		BuildSkeletonRecursive(scene->mRootNode, -1, boneOffsets, required, *skeleton);

		if (skeleton->GetBoneCount() > static_cast<std::size_t>(AXE_MAX_BONES))
		{
			AXE_CORE_ERROR("SkeletalMeshLoader: '{}' tem {} bones, acima do limite de {} do shader. "
				"A malha vai renderizar errada — reduza o rig ou aumente AXE_MAX_BONES.",
				filepath, skeleton->GetBoneCount(), AXE_MAX_BONES);
		}

		// --- Passo 4: vértices + pesos (todas as meshes combinadas)
		std::vector<SkinnedVertex>  vertices;
		std::vector<std::uint32_t>  indices;

		int overflowCount = 0;

		for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
		{
			const aiMesh* mesh = scene->mMeshes[m];
			const std::uint32_t vertexOffset = static_cast<std::uint32_t>(vertices.size());

			for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
			{
				SkinnedVertex v;
				v.Position = ToGlm(mesh->mVertices[i]);

				if (mesh->HasNormals())
					v.Normal = ToGlm(mesh->mNormals[i]);

				if (mesh->mTextureCoords[0])
					v.TexCoord = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };

				if (mesh->HasTangentsAndBitangents())
				{
					v.Tangent = ToGlm(mesh->mTangents[i]);
					v.Bitangent = ToGlm(mesh->mBitangents[i]);
				}

				vertices.push_back(v);
			}

			for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
			{
				const aiFace& face = mesh->mFaces[i];
				for (unsigned int j = 0; j < face.mNumIndices; ++j)
					indices.push_back(face.mIndices[j] + vertexOffset);
			}

			// Pesos. Note que o Assimp indexa por bone -> lista de vértices,
			// o inverso do que a GPU quer (vértice -> lista de bones).
			for (unsigned int b = 0; b < mesh->mNumBones; ++b)
			{
				const aiBone* bone = mesh->mBones[b];

				const int boneIndex = skeleton->FindBone(bone->mName.C_Str());
				if (boneIndex < 0)
				{
					// Não deveria acontecer: o bone veio de CollectBoneOffsets,
					// logo MarkRequiredNodes o marcou. Se cair aqui, o arquivo
					// referencia um bone que não existe na hierarquia de nodes.
					AXE_CORE_WARN("SkeletalMeshLoader: bone '{}' sem node correspondente — ignorado.",
						bone->mName.C_Str());
					continue;
				}

				for (unsigned int w = 0; w < bone->mNumWeights; ++w)
				{
					const aiVertexWeight& vw = bone->mWeights[w];
					const std::size_t vi = vertexOffset + vw.mVertexId;

					if (vi >= vertices.size())
						continue;

					if (!vertices[vi].AddBoneInfluence(boneIndex, vw.mWeight))
						++overflowCount;
				}
			}
		}

		if (overflowCount > 0)
		{
			// Não deveria ocorrer com aiProcess_LimitBoneWeights ativo. Se
			// ocorrer, as influências extras foram DESCARTADAS (não é fatal —
			// NormalizeWeights conserta a soma — mas a deformação perde
			// fidelidade nas juntas).
			AXE_CORE_WARN("SkeletalMeshLoader: {} influencia(s) alem de {} por vertice descartada(s) em '{}'.",
				overflowCount, AXE_MAX_BONE_INFLUENCE, filepath);
		}

		for (auto& v : vertices)
			v.NormalizeWeights();

		// --- Passo 5: clipes embutidos no arquivo
		SkeletalAsset asset;
		asset.SkeletonData = skeleton;
		asset.MeshData = std::make_shared<SkinnedMesh>(vertices, indices, skeleton);

		// Clipes embutidos vieram do MESMO arquivo -> mesma unidade -> 1.0.
		for (unsigned int a = 0; a < scene->mNumAnimations; ++a)
			asset.Clips.push_back(BuildClip(scene->mAnimations[a], *skeleton, 1.0f));

		AXE_CORE_INFO("SkeletalMeshLoader: '{}' — {} vertices, {} indices, {} bones, {} clipe(s).",
			filepath, vertices.size(), indices.size(),
			skeleton->GetBoneCount(), asset.Clips.size());

		s_Cache[filepath] = asset;
		return asset;
	}

	std::vector<std::shared_ptr<AnimationClip>> SkeletalMeshLoader::LoadClips(
		const std::string& filepath,
		const Skeleton& targetSkeleton)
	{
		std::vector<std::shared_ptr<AnimationClip>> clips;

		if (targetSkeleton.IsEmpty())
		{
			AXE_CORE_ERROR("SkeletalMeshLoader::LoadClips: esqueleto alvo vazio ('{}').", filepath);
			return clips;
		}

		Assimp::Importer importer;
		ConfigureImporter(importer);   // TEM que casar com o Load()

		// Sem flags de geometria: aqui só interessam as curvas. Processar
		// tangentes/normais de uma mesh que vamos jogar fora é desperdício.
		const aiScene* scene = importer.ReadFile(filepath, 0);

		if (!scene || !scene->mRootNode)
		{
			AXE_CORE_ERROR("SkeletalMeshLoader::LoadClips: falha ao carregar '{}': {}",
				filepath, importer.GetErrorString());
			return clips;
		}

		if (scene->mNumAnimations == 0)
		{
			AXE_CORE_WARN("SkeletalMeshLoader::LoadClips: '{}' nao contem animacoes.", filepath);
			return clips;
		}

		// Diferenca de unidade entre o arquivo de animacao e o personagem.
		// Ex: personagem em cm (0.01) e animacao em m (1.0) -> as translacoes
		// do clipe precisam ser multiplicadas por 100 pra falar a mesma
		// lingua que o esqueleto.
		const float animUnit = ReadUnitScale(scene);
		const float skelUnit = targetSkeleton.GetUnitScale();

		const float positionScale = (skelUnit > 1e-9f) ? (animUnit / skelUnit) : 1.0f;

		if (std::abs(positionScale - 1.0f) > 1e-4f)
		{
			AXE_CORE_WARN("SkeletalMeshLoader::LoadClips: '{}' tem unidade diferente do personagem "
				"(animacao {:.4f} vs esqueleto {:.4f}); corrigindo as translacoes por {:.2f}x.",
				filepath, animUnit, skelUnit, positionScale);
		}

		for (unsigned int a = 0; a < scene->mNumAnimations; ++a)
			clips.push_back(BuildClip(scene->mAnimations[a], targetSkeleton, positionScale));

		AXE_CORE_INFO("SkeletalMeshLoader::LoadClips: '{}' — {} clipe(s) religado(s) ao esqueleto '{}'.",
			filepath, clips.size(), targetSkeleton.GetName());

		return clips;
	}

} // namespace axe