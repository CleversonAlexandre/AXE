#include "skeletal_mesh_asset.hpp"

#include <unordered_set>
#include <cctype>
#include <unordered_map>
#include "skeletal_mesh_loader.hpp"
#include "axe/log/log.hpp"

#include <nlohmann/json.hpp>
#include <fstream>

namespace axe
{
	using json = nlohmann::json;

	namespace fs = std::filesystem;

	std::shared_ptr<SkeletalMeshAsset> SkeletalMeshAsset::Create(const std::string& name,
		const fs::path& sourceFile)
	{
		auto asset = std::make_shared<SkeletalMeshAsset>();
		asset->m_Name = name;
		asset->m_SourceFile = sourceFile;
		return asset;
	}

	fs::path SkeletalMeshAsset::ResolvePath(const fs::path& p) const
	{
		if (p.empty() || p.is_absolute())
			return p;

		// Relativo -> a partir da pasta do .axeskel.
		if (!m_FilePath.empty())
			return fs::weakly_canonical(m_FilePath.parent_path() / p);

		return p;
	}

	fs::path SkeletalMeshAsset::RelativizePath(const fs::path& p) const
	{
		if (p.empty() || m_FilePath.empty())
			return p;

		std::error_code ec;
		fs::path rel = fs::relative(p, m_FilePath.parent_path(), ec);

		// Se relative() falhou (drives diferentes, por exemplo), guarda o
		// absoluto — feio, mas funcional, e melhor do que gravar um caminho
		// quebrado.
		if (ec || rel.empty())
			return p;

		return rel;
	}

	bool SkeletalMeshAsset::Resolve()
	{
		if (m_Resolved)
			return true;

		// Re-resolve limpo: um Resolve anterior (ou um reload-in-place do
		// cache) pode ter deixado clipes aqui — sem limpar, duplicariam.
		m_Clips.clear();

		const fs::path source = ResolvePath(m_SourceFile);

		if (source.empty() || !fs::exists(source))
		{
			AXE_CORE_ERROR("SkeletalMeshAsset '{}': arquivo de origem '{}' nao encontrado.",
				m_Name, source.string());
			return false;
		}

		SkeletalAsset imported = SkeletalMeshLoader::Load(source.string());

		if (!imported.IsValid())
		{
			AXE_CORE_ERROR("SkeletalMeshAsset '{}': falha ao importar '{}'.",
				m_Name, source.string());
			return false;
		}

		m_Mesh = imported.MeshData;
		m_Skeleton = imported.SkeletonData;

		// Clipes embutidos no próprio arquivo do personagem (comum em glTF).
		m_Clips = imported.Clips;

		// Nomes de clipe sao a CHAVE do religamento (o grafo referencia por
		// nome) — dois clipes com o mesmo nome e o segundo fica inalcancavel,
		// silenciosamente. Ex. real: a entrada "idle" com 3 takes gera
		// "idle_2", que colide com o take 1 da ENTRADA chamada "idle_2".
		// Aqui garantimos unicidade global, custe o sufixo que custar.
		std::unordered_set<std::string> usedNames;

		for (const auto& c : m_Clips)
			if (c) usedNames.insert(c->GetName());

		auto makeUnique = [&usedNames](std::string name) -> std::string
			{
				if (name.empty() || !usedNames.count(name))
				{
					usedNames.insert(name);
					return name;
				}

				for (int n = 2; ; ++n)
				{
					const std::string cand = name + "_" + std::to_string(n);

					if (!usedNames.count(cand))
					{
						usedNames.insert(cand);
						return cand;
					}
				}
			};

		// Clipes de arquivos separados (o fluxo Mixamo).
		for (const auto& entry : m_Animations)
		{
			const fs::path animPath = ResolvePath(entry.SourceFile);

			if (!fs::exists(animPath))
			{
				AXE_CORE_WARN("SkeletalMeshAsset '{}': animacao '{}' nao encontrada em '{}' — ignorada.",
					m_Name, entry.Name, animPath.string());
				continue;
			}

			// Religamento por NOME de osso. Os índices de bone de um arquivo
			// de animação NUNCA batem com os do personagem — é por isso que
			// isto funciona com os FBX separados da Mixamo.
			auto clips = SkeletalMeshLoader::LoadClips(animPath.string(), *m_Skeleton);

			int takeIdx = 0;

			for (auto& c : clips)
			{
				if (!c)
					continue;

				// O nome do JSON manda: o usuário pode ter renomeado o clipe,
				// e "mixamo.com" (nome que a Mixamo põe em TODO clipe) não
				// ajuda ninguém a distinguir idle de run.
				//
				// MAS: um FBX pode carregar VÁRIOS takes (ex.: exportado do
				// Blender com 'mixamo.com', '.001', '.002'). Renomear todos
				// para o MESMO nome fazia o religamento por nome resolver
				// sempre pro primeiro — os outros takes ficavam inalcançáveis,
				// e o usuário tocava o take errado sem nenhum aviso.
				if (!entry.Name.empty())
				{
					if (takeIdx == 0)
						c->SetName(makeUnique(entry.Name));
					else
						c->SetName(makeUnique(entry.Name + "_" + std::to_string(takeIdx + 1)));
				}
				else
				{
					c->SetName(makeUnique(c->GetName()));
				}

				++takeIdx;
				m_Clips.push_back(c);
			}

			if (takeIdx > 1)
			{
				AXE_CORE_INFO("SkeletalMeshAsset '{}': '{}' contem {} takes — nomeados '{}', '{}_2'...",
					m_Name, animPath.filename().string(), takeIdx, entry.Name, entry.Name);
			}
		}

		// Reaplica a autoria persistida (Animation Editor) nos clipes
		// recem-importados — o FBX nao sabe nada de loop/notifies.
		for (auto& c : m_Clips)
		{
			if (!c) continue;

			auto it = m_ClipMeta.find(c->GetName());
			if (it == m_ClipMeta.end()) continue;

			c->SetLooping(it->second.Loop);
			c->RateScale = it->second.RateScale;
			c->RootMotion = it->second.RootMotion;
			c->NotifyTrackCount = std::max(1, it->second.TrackCount);
			c->Notifies = it->second.Notifies;
		}

		m_Resolved = true;

		AXE_CORE_INFO("SkeletalMeshAsset '{}': {} bones, {} clipe(s).",
			m_Name, m_Skeleton->GetBoneCount(), m_Clips.size());

		return true;
	}

	int SkeletalMeshAsset::AddAnimation(const fs::path& file)
	{
		if (!Resolve())
			return 0;

		if (!fs::exists(file))
		{
			AXE_CORE_ERROR("SkeletalMeshAsset: animacao '{}' nao encontrada.", file.string());
			return 0;
		}

		// ── Dedupe por arquivo ───────────────────────────────────────────
		//
		// Importar o MESMO arquivo duas vezes nao adiciona informacao —
		// adiciona uma entrada duplicada, que multiplica pelos takes do FBX
		// e vira aquela cascata de "idle, idle_1, idle_2_2..." no combo.
		// Ja registrado? Avisa e nao duplica.
		const fs::path rel = RelativizePath(file);

		for (const auto& e : m_Animations)
		{
			if (e.SourceFile.generic_string() == rel.generic_string())
			{
				AXE_CORE_INFO("SkeletalMeshAsset '{}': '{}' ja esta importado (entrada '{}') — nada a fazer.",
					m_Name, file.filename().string(), e.Name);
				return 0;
			}
		}

		auto clips = SkeletalMeshLoader::LoadClips(file.string(), *m_Skeleton);

		if (clips.empty())
		{
			AXE_CORE_ERROR("SkeletalMeshAsset: '{}' nao trouxe nenhum clipe compativel com o esqueleto '{}'. "
				"Os nomes dos ossos batem? (Mixamo usa 'mixamorig:*' em todos os arquivos)",
				file.string(), m_Skeleton->GetName());
			return 0;
		}

		int added = 0;

		for (auto& c : clips)
		{
			if (!c)
				continue;

			AnimEntry entry;

			// Nome do ARQUIVO, não o nome interno do clipe. A Mixamo chama
			// todo clipe de "mixamo.com" — se usássemos isso, o dropdown
			// ficaria com cinco entradas idênticas.
			entry.Name = file.stem().string();
			entry.SourceFile = RelativizePath(file);

			// Colisão de nome (dois arquivos com o mesmo stem, ou um FBX com
			// vários clipes dentro): desambigua com sufixo.
			if (added > 0)
				entry.Name += "_" + std::to_string(added);

			c->SetName(entry.Name);

			m_Animations.push_back(entry);
			m_Clips.push_back(c);

			++added;
		}

		return added;
	}

	bool SkeletalMeshAsset::RemoveAnimation(std::size_t index)
	{
		if (index >= m_Animations.size())
			return false;

		AXE_CORE_INFO("SkeletalMeshAsset '{}': entrada '{}' removida.",
			m_Name, m_Animations[index].Name);

		m_Animations.erase(m_Animations.begin() + index);

		// Reconstroi os clipes do zero: o Resolve() limpa m_Clips e reimporta
		// so o que sobrou — os sufixos anti-colisao reassentam sozinhos.
		m_Resolved = false;
		return Resolve();
	}

	int SkeletalMeshAsset::FindAnimationEntryBySource(const fs::path& file) const
	{
		// Normalizacao completa antes de comparar: absoluto (records podem
		// vir relativos ao CWD), canonico (resolve ../ e symlinks do trecho
		// existente) e CAIXA BAIXA (Windows e case-insensitive, mas
		// weakly_canonical nao normaliza caixa de trecho inexistente — dois
		// caminhos iguais em disco podiam divergir aqui por um 'meshes' vs
		// 'Meshes').
		auto normalize = [](const fs::path& p) -> std::string
			{
				std::error_code ec;
				fs::path abs = fs::absolute(p, ec);
				if (ec) abs = p;

				fs::path canon = fs::weakly_canonical(abs, ec);
				if (ec) canon = abs;

				std::string s = canon.generic_string();
				std::transform(s.begin(), s.end(), s.begin(),
					[](unsigned char ch) { return (char)std::tolower(ch); });
				return s;
			};

		const std::string target = normalize(file);

		if (target.empty())
			return -1;

		for (std::size_t i = 0; i < m_Animations.size(); ++i)
		{
			if (normalize(ResolvePath(m_Animations[i].SourceFile)) == target)
				return (int)i;
		}

		// ── Fallback: mesmo NOME DE ARQUIVO ──────────────────────────────
		//
		// O .axeskel guarda o CAMINHO da animacao. Mover o FBX de pasta no
		// Asset Browser (Meshes/idle.fbx -> Meshes/Animation/idle.fbx) deixa
		// o caminho gravado obsoleto, e o duplo-clique parava de achar o dono
		// — o clipe seguia funcionando, so a navegacao quebrava. Casar pelo
		// nome do arquivo resolve o caso comum sem inventar heuristica: se
		// dois arquivos diferentes tem o mesmo nome, o primeiro ganha, e o
		// log avisa que o caminho esta desatualizado.
		auto fileNameLower = [](const fs::path& p)
			{
				std::string s = p.filename().string();
				std::transform(s.begin(), s.end(), s.begin(),
					[](unsigned char ch) { return (char)std::tolower(ch); });
				return s;
			};

		const std::string wantedName = fileNameLower(file);

		for (std::size_t i = 0; i < m_Animations.size(); ++i)
		{
			if (fileNameLower(m_Animations[i].SourceFile) == wantedName)
			{
				AXE_CORE_INFO("SkeletalMeshAsset '{}': '{}' casado por NOME — o caminho no .axeskel ('{}') esta desatualizado (arquivo movido?). Reimporte para atualizar.",
					m_Name, wantedName, m_Animations[i].SourceFile.generic_string());

				return (int)i;
			}
		}

		return -1;
	}

	bool SkeletalMeshAsset::UpdateAnimationSource(std::size_t index, const fs::path& file)
	{
		if (index >= m_Animations.size())
			return false;

		const fs::path rel = RelativizePath(file);

		if (m_Animations[index].SourceFile.generic_string() == rel.generic_string())
			return false;

		AXE_CORE_INFO("SkeletalMeshAsset '{}': caminho da animacao '{}' atualizado ({} -> {}).",
			m_Name, m_Animations[index].Name,
			m_Animations[index].SourceFile.generic_string(), rel.generic_string());

		m_Animations[index].SourceFile = rel;

		if (!m_FilePath.empty())
			Save(m_FilePath);

		return true;
	}

	void SkeletalMeshAsset::StoreClipMeta(const std::shared_ptr<AnimationClip>& clip)
	{
		if (!clip)
			return;

		ClipMeta m;
		m.Loop = clip->IsLooping();
		m.RateScale = clip->RateScale;
		m.RootMotion = clip->RootMotion;
		m.TrackCount = std::max(1, clip->NotifyTrackCount);
		m.Notifies = clip->Notifies;

		m_ClipMeta[clip->GetName()] = std::move(m);
	}

	void SkeletalMeshAsset::RemoveAnimation(int index)
	{
		if (index < 0 || index >= static_cast<int>(m_Animations.size()))
			return;

		m_Animations.erase(m_Animations.begin() + index);

		// Força reimportação: os índices de m_Clips não batem mais com os de
		// m_Animations (clipes embutidos no personagem vêm antes). Recalcular
		// do zero é barato — o SkeletalMeshLoader cacheia por caminho — e
		// evita um bug de índice deslocado bem difícil de rastrear.
		m_Resolved = false;
		m_Clips.clear();
		Resolve();
	}

	// ── Cache de identidade dos .axeskel ────────────────────────────────────
	//
	// Mesmo arquivo => MESMO objeto vivo — igual ao cache dos .axeanim. Sem
	// isto, o editor de AnimGraph importava um clipe na copia DELE do
	// esqueleto, e o componente da cena (outra copia) nunca ficava sabendo:
	// "o clipe 'idle_2' nao existe no personagem", bind pose.
	//
	// mtime: arquivo mudado por fora recarrega as ENTRADAS no mesmo objeto e
	// derruba m_Resolved — o proximo Resolve() reimporta; ate la, quem ja
	// renderizava segue com a malha antiga (nada fica nulo no meio do frame).
	namespace
	{
		struct CachedSkel
		{
			std::weak_ptr<SkeletalMeshAsset> Asset;
			fs::file_time_type               MTime{};
		};

		std::unordered_map<std::string, CachedSkel> s_LoadedSkels;

		std::string SkelCacheKey(const fs::path& p)
		{
			std::error_code ec;
			auto canon = fs::weakly_canonical(p, ec);
			return ec ? std::string{} : canon.string();
		}
	}

	bool SkeletalMeshAsset::Save(const fs::path& filepath)
	{
		// m_FilePath tem que estar setado ANTES de relativizar — os caminhos
		// são relativos à pasta do .axeskel.
		m_FilePath = filepath;

		json root;
		root["type"] = "SkeletalMesh";
		root["version"] = "1.0";
		root["name"] = m_Name;
		root["source"] = RelativizePath(ResolvePath(m_SourceFile)).generic_string();

		json anims = json::array();

		for (const auto& a : m_Animations)
		{
			json j;
			j["name"] = a.Name;
			j["source"] = RelativizePath(ResolvePath(a.SourceFile)).generic_string();
			anims.push_back(j);
		}

		root["animations"] = anims;

		// ── Autoria por clipe (Animation Editor) ─────────────────────────
		if (!m_ClipMeta.empty())
		{
			json meta = json::object();

			for (const auto& [name, m] : m_ClipMeta)
			{
				json jm;
				jm["loop"] = m.Loop;
				jm["rate"] = m.RateScale;
				jm["root_motion"] = m.RootMotion;
				jm["tracks"] = m.TrackCount;

				json jn = json::array();

				for (const auto& n : m.Notifies)
				{
					json e;
					e["t"] = n.Time;
					e["name"] = n.Name;
					e["type"] = (int)n.Type;
					e["payload"] = n.Payload;
					e["track"] = n.Track;
					e["socket"] = n.Socket;
					e["loc"] = { n.LocationOffset.x, n.LocationOffset.y, n.LocationOffset.z };
					e["rot"] = { n.RotationOffset.x, n.RotationOffset.y, n.RotationOffset.z };
					e["scale"] = { n.Scale.x, n.Scale.y, n.Scale.z };
					e["attached"] = n.Attached;
					e["color"] = { n.Color.x, n.Color.y, n.Color.z };
					jn.push_back(e);
				}

				jm["notifies"] = jn;
				meta[name] = jm;
			}

			root["clip_meta"] = meta;
		}

		fs::create_directories(filepath.parent_path());

		std::ofstream file(filepath);
		if (!file.is_open())
		{
			AXE_CORE_ERROR("SkeletalMeshAsset: falha ao salvar '{}'", filepath.string());
			return false;
		}

		file << root.dump(4);
		file.close();

		// O proprio save nao e "mudanca externa" — atualiza o mtime do cache
		// pra nao disparar um reload a toa no proximo LoadFromFile.
		{
			const std::string key = SkelCacheKey(filepath);

			std::error_code mec;
			const auto mtime = fs::last_write_time(filepath, mec);

			if (!key.empty() && !mec)
			{
				auto it = s_LoadedSkels.find(key);

				if (it != s_LoadedSkels.end())
					it->second.MTime = mtime;
			}
		}

		AXE_CORE_INFO("SkeletalMeshAsset: salvo em '{}'", filepath.string());
		return true;
	}

	bool SkeletalMeshAsset::Save()
	{
		if (m_FilePath.empty())
		{
			AXE_CORE_ERROR("SkeletalMeshAsset '{}': sem caminho — use Save(path).", m_Name);
			return false;
		}

		return Save(m_FilePath);
	}

	std::shared_ptr<SkeletalMeshAsset> SkeletalMeshAsset::LoadFromFile(const fs::path& filepath)
	{
		if (!fs::exists(filepath))
		{
			AXE_CORE_ERROR("SkeletalMeshAsset: '{}' nao encontrado.", filepath.string());
			return nullptr;
		}

		const std::string key = SkelCacheKey(filepath);

		std::error_code mec;
		const auto mtime = fs::last_write_time(filepath, mec);

		std::shared_ptr<SkeletalMeshAsset> reuse;

		if (!key.empty())
		{
			auto it = s_LoadedSkels.find(key);

			if (it != s_LoadedSkels.end())
			{
				if (auto alive = it->second.Asset.lock())
				{
					if (!mec && it->second.MTime == mtime)
						return alive;

					reuse = alive;   // mudou por fora: recarrega no MESMO objeto
				}
			}
		}

		std::ifstream file(filepath);

		json root;
		try
		{
			root = json::parse(file);
		}
		catch (const std::exception& e)
		{
			AXE_CORE_ERROR("SkeletalMeshAsset: '{}' invalido: {}", filepath.string(), e.what());
			return nullptr;
		}

		auto asset = reuse ? reuse : std::make_shared<SkeletalMeshAsset>();

		if (reuse)
		{
			// As entradas vem inteiras do JSON novo; o resolvido fica marcado
			// pra reimportar no proximo Resolve().
			asset->m_Animations.clear();
			asset->m_Resolved = false;
		}

		asset->m_FilePath = filepath;
		asset->m_Name = root.value("name", filepath.stem().string());
		asset->m_SourceFile = fs::path(root.value("source", std::string{}));

		if (root.contains("animations") && root["animations"].is_array())
		{
			for (const auto& j : root["animations"])
			{
				AnimEntry entry;
				entry.Name = j.value("name", std::string{});
				entry.SourceFile = fs::path(j.value("source", std::string{}));

				if (!entry.SourceFile.empty())
					asset->m_Animations.push_back(entry);
			}
		}

		asset->m_ClipMeta.clear();

		if (root.contains("clip_meta") && root["clip_meta"].is_object())
		{
			for (auto it = root["clip_meta"].begin(); it != root["clip_meta"].end(); ++it)
			{
				ClipMeta m;
				m.Loop = it.value().value("loop", true);
				m.RateScale = it.value().value("rate", 1.0f);
				m.RootMotion = it.value().value("root_motion", false);
				m.TrackCount = it.value().value("tracks", 1);

				if (it.value().contains("notifies"))
				{
					for (const auto& jn : it.value()["notifies"])
					{
						AnimNotify n;
						n.Time = jn.value("t", 0.0f);
						n.Name = jn.value("name", std::string{});
						n.Type = (AnimNotify::Kind)jn.value("type", 0);
						n.Payload = jn.value("payload", std::string{});
						n.Track = jn.value("track", 0);
						n.Socket = jn.value("socket", std::string{});
						n.Attached = jn.value("attached", true);

						auto readV3 = [&jn](const char* key, glm::vec3 def) -> glm::vec3
							{
								if (jn.contains(key) && jn[key].is_array() && jn[key].size() == 3)
									return { jn[key][0].get<float>(),
											 jn[key][1].get<float>(),
											 jn[key][2].get<float>() };
								return def;
							};

						n.LocationOffset = readV3("loc", glm::vec3(0.0f));
						n.RotationOffset = readV3("rot", glm::vec3(0.0f));
						n.Scale = readV3("scale", glm::vec3(1.0f));

						// Notifies antigos (sem cor salva): cor do tipo.
						glm::vec3 defCol{ 0.47f, 0.75f, 1.0f };
						if (n.Type == AnimNotify::Kind::Sound)    defCol = { 0.47f, 0.86f, 0.51f };
						if (n.Type == AnimNotify::Kind::Particle) defCol = { 1.0f, 0.67f, 0.31f };
						n.Color = readV3("color", defCol);
						m.Notifies.push_back(std::move(n));
					}
				}

				asset->m_ClipMeta[it.key()] = std::move(m);
			}
		}

		// NÃO resolve aqui: importar um FBX é caro, e o asset browser lista
		// dezenas de assets sem precisar da geometria de nenhum. Quem
		// realmente for usar chama Resolve().
		if (!key.empty() && !mec)
			s_LoadedSkels[key] = { asset, mtime };

		return asset;
	}

} // namespace axe