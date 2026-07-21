#include "anim_graph_asset.hpp"
#include "axe/animation/skeletal_mesh_asset.hpp"
#include "axe/animation/blend_space_1d.hpp"
#include "axe/log/log.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <unordered_map>

namespace axe
{
	// Versão do formato. O v1 (lista chapada de estados) ainda é LIDO — ver
	// MigrateV1 lá embaixo.
	static constexpr int kAnimGraphVersion = 2;

	std::shared_ptr<AnimGraphAsset> AnimGraphAsset::Create(const std::string& name,
		const std::string& skeletonUUID)
	{
		auto asset = std::make_shared<AnimGraphAsset>();
		asset->m_Name = name;
		asset->m_SkeletonUUID = skeletonUUID;

		// Um grafo NOVO nasce com o Output e um parâmetro de exemplo — e SÓ.
		//
		// A State Machine "Locomotion" que nascia junto parecia ajuda, mas era
		// mobília na casa dos outros: o usuário não pediu, não pode nomear
		// antes de existir, e todo grafo começava igual. Como na Unreal, quem
		// decide a estrutura é o usuário — botão direito no fundo cria o que
		// ele quiser.
		auto out = std::make_unique<AnimNode_Output>();
		out->Title = "Output Pose";
		out->EditorX = 620.0f;
		out->EditorY = 200.0f;
		const int outId = asset->m_Root.AddNode(std::move(out));

		asset->m_Root.SetOutputNode(outId);

		// Um parâmetro de exemplo: dá ao usuário algo pra ligar no primeiro
		// blend space sem ter que descobrir o painel de parâmetros primeiro.
		AnimParamDecl speed;
		speed.Name = "Speed";
		speed.Type = AnimParamType::Float;
		asset->m_Parameters.push_back(speed);

		return asset;
	}

	void AnimGraphAsset::SeedParameters(AnimParameters& params) const
	{
		for (const auto& p : m_Parameters)
		{
			switch (p.Type)
			{
			case AnimParamType::Float: params.SetFloat(p.Name, p.DefaultF); break;
			case AnimParamType::Int:   params.SetInt(p.Name, p.DefaultI);   break;
			case AnimParamType::Bool:  params.SetBool(p.Name, p.DefaultB);  break;

				// Trigger NÃO é semeado. Ver o comentário no header.
			case AnimParamType::Trigger: break;
			}
		}
	}

	// ── Resolve ──────────────────────────────────────────────────────────────
	//
	// Recursivo: desce em toda máquina de estados, em todo estado, em qualquer
	// profundidade. Se isso fosse escrito "à mão" nível por nível, o dia em que
	// alguém aninhasse uma state machine dentro de um estado, os clipes de
	// dentro dela tocariam bind pose — sem erro nenhum.
	static bool ResolveGraph(AnimPoseGraph& graph, const SkeletalMeshAsset& skel,
		const std::string& assetName)
	{
		bool ok = true;

		auto findClip = [&](const std::string& name) -> std::shared_ptr<AnimationClip>
			{
				for (const auto& c : skel.GetClips())
					if (c && c->GetName() == name)
						return c;

				return nullptr;
			};

		for (const auto& node : graph.GetNodes())
		{
			if (auto* clipNode = dynamic_cast<AnimNode_ClipPlayer*>(node.get()))
			{
				if (clipNode->ClipName.empty())
					continue;

				clipNode->Clip = findClip(clipNode->ClipName);

				if (!clipNode->Clip)
				{
					AXE_CORE_ERROR("AnimGraph '{}': o clipe '{}' nao existe no personagem. "
						"O no vai tocar bind pose.", assetName, clipNode->ClipName);
					ok = false;
				}
			}
			else if (auto* bs = dynamic_cast<AnimNode_BlendSpacePlayer*>(node.get()))
			{
				if (bs->Samples.empty())
					continue;

				auto space = std::make_shared<BlendSpace1D>();
				bool anySample = false;

				for (const auto& sample : bs->Samples)
				{
					auto clip = findClip(sample.first);

					if (!clip)
					{
						AXE_CORE_ERROR("AnimGraph '{}': blend space referencia o clipe '{}', "
							"que nao existe no personagem.", assetName, sample.first);
						ok = false;
						continue;
					}

					space->AddSample(clip, sample.second);
					anySample = true;
				}

				bs->Space = anySample ? space : nullptr;
			}
			else if (auto* sm = dynamic_cast<AnimNode_StateMachine*>(node.get()))
			{
				// Recursão: cada estado tem um grafo inteiro dentro.
				for (auto& st : sm->States)
					ok &= ResolveGraph(st.Graph, skel, assetName);
			}
		}

		graph.Resolve();
		return ok;
	}

	bool AnimGraphAsset::Resolve(const SkeletalMeshAsset& skeleton)
	{
		return ResolveGraph(m_Root, skeleton, m_Name);
	}

	// ── Migração do v1 ───────────────────────────────────────────────────────
	//
	// O v1 era uma lista chapada de estados, cada um com um clipe (ou um blend
	// space) e transições entre eles.
	//
	// Isso é EXATAMENTE o conteúdo de um nó State Machine. Então a migração é:
	// criar um Output, criar uma State Machine, despejar os estados do v1
	// dentro dela — cada um virando um sub-grafo de um nó só — e ligar.
	//
	// Quebrar o formato e mandar o usuário recriar seria mais rápido pra mim e
	// pior pra ele. Trinta linhas evitam isso.
	static void MigrateV1(const nlohmann::json& j, AnimPoseGraph& root,
		std::vector<AnimParamDecl>& params)
	{
		auto out = std::make_unique<AnimNode_Output>();
		out->Title = "Output Pose";
		out->EditorX = 620.0f;
		out->EditorY = 200.0f;
		const int outId = root.AddNode(std::move(out));

		auto smNode = std::make_unique<AnimNode_StateMachine>();
		smNode->Title = "Locomotion";
		smNode->EditorX = 300.0f;
		smNode->EditorY = 190.0f;

		if (j.contains("states"))
		{
			for (const auto& js : j["states"])
			{
				AnimSmState st;
				st.Name = js.value("name", std::string("Estado"));
				st.EditorX = js.value("editor_x", 0.0f);
				st.EditorY = js.value("editor_y", 0.0f);

				// Cada estado do v1 vira um sub-grafo: player -> Output.
				auto stOut = std::make_unique<AnimNode_Output>();
				stOut->Title = "Output Animation Pose";
				stOut->EditorX = 420.0f;
				stOut->EditorY = 160.0f;
				const int stOutId = st.Graph.AddNode(std::move(stOut));
				st.Graph.SetOutputNode(stOutId);

				const auto samples = js.value("blend_samples", nlohmann::json::array());

				int playerId = -1;

				if (!samples.empty())
				{
					auto bs = std::make_unique<AnimNode_BlendSpacePlayer>();
					bs->Title = "Blend Space";
					bs->EditorX = 140.0f;
					bs->EditorY = 150.0f;

					for (const auto& jsm : samples)
						bs->Samples.emplace_back(jsm.value("clip", std::string{}),
							jsm.value("value", 0.0f));

					// O parâmetro do v1 era um NOME digitado. No v2 é um pino —
					// então criamos o nó de variável e ligamos, que é o que o
					// usuário faria à mão.
					const std::string param = js.value("blend_parameter", std::string("Speed"));

					auto getf = std::make_unique<AnimNode_GetFloat>();
					getf->Parameter = param;
					getf->Title = param;
					getf->EditorX = -60.0f;
					getf->EditorY = 160.0f;
					const int getId = st.Graph.AddNode(std::move(getf));

					playerId = st.Graph.AddNode(std::move(bs));
					st.Graph.AddLink(getId, playerId, 0, AnimLinkKind::Data);
				}
				else
				{
					auto cp = std::make_unique<AnimNode_ClipPlayer>();
					cp->ClipName = js.value("clip", std::string{});
					cp->PlayRate = js.value("play_rate", 1.0f);
					cp->Title = cp->ClipName.empty() ? "Clip" : cp->ClipName;
					cp->EditorX = 140.0f;
					cp->EditorY = 150.0f;

					playerId = st.Graph.AddNode(std::move(cp));
				}

				if (playerId >= 0)
					st.Graph.AddLink(playerId, stOutId, 0, AnimLinkKind::Pose);

				smNode->States.push_back(std::move(st));
			}
		}

		if (j.contains("transitions"))
		{
			for (const auto& jt : j["transitions"])
			{
				AnimTransition tr;
				tr.From = jt.value("from", -1);
				tr.To = jt.value("to", -1);
				tr.Duration = jt.value("duration", 0.2f);
				tr.HasExitTime = jt.value("has_exit_time", false);
				tr.ExitTime = jt.value("exit_time", 1.0f);
				tr.Priority = jt.value("priority", 0);
				tr.CanRetriggerSelf = jt.value("can_retrigger_self", false);

				if (jt.contains("conditions"))
				{
					for (const auto& jc : jt["conditions"])
					{
						AnimCondition c;
						c.Parameter = jc.value("parameter", std::string{});
						c.Op = (AnimCompare)jc.value("op", 0);
						c.Value = jc.value("value", 0.0f);
						tr.Conditions.push_back(c);
					}
				}

				smNode->Transitions.push_back(tr);
			}
		}

		smNode->EntryState = j.value("entry_state", 0);

		const std::size_t migratedStates = smNode->States.size();

		const int smId = root.AddNode(std::move(smNode));

		root.SetOutputNode(outId);
		root.AddLink(smId, outId, 0, AnimLinkKind::Pose);

		if (j.contains("parameters"))
		{
			for (const auto& jp : j["parameters"])
			{
				AnimParamDecl d;
				d.Name = jp.value("name", std::string{});
				d.Type = (AnimParamType)jp.value("type", 0);
				d.DefaultF = jp.value("default_f", 0.0f);
				d.DefaultI = jp.value("default_i", 0);
				d.DefaultB = jp.value("default_b", false);
				params.push_back(d);
			}
		}

		AXE_CORE_INFO("AnimGraph: formato v1 migrado para v2 — {} estado(s) viraram um no "
			"State Machine ligado ao Output. Salve para gravar no formato novo.", migratedStates);
	}

	// ── Cache de identidade dos .axeanim ────────────────────────────────────
	//
	// Mesmo arquivo => MESMO objeto vivo. Sem isto, o editor de AnimGraph e o
	// componente da cena carregavam o .axeanim em objetos SEPARADOS — e salvar
	// no editor nunca alcancava o personagem da cena.
	//
	// O mtime guarda a validade: se o arquivo mudou POR FORA (apagado e
	// recriado, editado externamente, sincronizado), o proximo LoadFromFile
	// recarrega o conteudo NOVO dentro do MESMO objeto e da BumpVersion — quem
	// segurava o ponteiro (cena, editor) continua com ele, e as instancias
	// re-clonam sozinhas. weak_ptr: o cache nao segura o asset vivo.
	namespace
	{
		struct CachedGraph
		{
			std::weak_ptr<AnimGraphAsset>   Asset;
			std::filesystem::file_time_type MTime{};
		};

		std::unordered_map<std::string, CachedGraph> s_LoadedGraphs;

		std::string GraphCacheKey(const std::filesystem::path& p)
		{
			std::error_code ec;
			auto canon = std::filesystem::weakly_canonical(p, ec);
			return ec ? std::string{} : canon.string();
		}
	}

	std::shared_ptr<AnimGraphAsset> AnimGraphAsset::LoadFromFile(const std::filesystem::path& filepath)
	{
		const std::string key = GraphCacheKey(filepath);

		std::error_code mec;
		const auto mtime = std::filesystem::last_write_time(filepath, mec);

		std::shared_ptr<AnimGraphAsset> reuse;

		if (!key.empty())
		{
			auto it = s_LoadedGraphs.find(key);

			if (it != s_LoadedGraphs.end())
			{
				if (auto alive = it->second.Asset.lock())
				{
					// Arquivo intacto desde o load/save anterior? O objeto em
					// memoria E o arquivo — devolve direto.
					if (!mec && it->second.MTime == mtime)
						return alive;

					// Mudou por fora: recarregar DENTRO deste mesmo objeto,
					// preservando a identidade que a cena/editor ja seguram.
					reuse = alive;
				}
			}
		}

		std::ifstream in(filepath);

		if (!in.is_open())
		{
			AXE_CORE_ERROR("AnimGraphAsset: nao consegui abrir '{}'.", filepath.string());
			return nullptr;
		}

		nlohmann::json j;

		try
		{
			in >> j;
		}
		catch (const std::exception& e)
		{
			AXE_CORE_ERROR("AnimGraphAsset: '{}' nao e um JSON valido: {}", filepath.string(), e.what());
			return nullptr;
		}

		auto asset = reuse ? reuse : std::make_shared<AnimGraphAsset>();

		// No reload-in-place, limpar o estado anterior — FromJson/Migrate
		// preenchem por append.
		asset->m_Parameters.clear();
		asset->m_Root = AnimPoseGraph{};

		asset->m_FilePath = filepath;
		asset->m_Name = j.value("name", filepath.stem().string());
		asset->m_SkeletonUUID = j.value("skeleton", std::string{});

		const int version = j.value("version", 1);

		if (version < 2)
		{
			MigrateV1(j, asset->m_Root, asset->m_Parameters);
		}
		else
		{
			if (j.contains("parameters"))
			{
				for (const auto& jp : j["parameters"])
				{
					AnimParamDecl d;
					d.Name = jp.value("name", std::string{});
					d.Type = (AnimParamType)jp.value("type", 0);
					d.DefaultF = jp.value("default_f", 0.0f);
					d.DefaultI = jp.value("default_i", 0);
					d.DefaultB = jp.value("default_b", false);
					asset->m_Parameters.push_back(d);
				}
			}

			if (j.contains("root"))
				asset->m_Root.FromJson(j["root"]);
		}

		if (reuse)
			asset->BumpVersion();   // instancias re-clonam no proximo Update

		if (!key.empty() && !mec)
			s_LoadedGraphs[key] = { asset, mtime };

		return asset;
	}

	bool AnimGraphAsset::Save(const std::filesystem::path& filepath)
	{
		nlohmann::json j;

		j["version"] = kAnimGraphVersion;
		j["name"] = m_Name;
		j["skeleton"] = m_SkeletonUUID;

		j["parameters"] = nlohmann::json::array();

		for (const auto& p : m_Parameters)
		{
			nlohmann::json jp;
			jp["name"] = p.Name;
			jp["type"] = (int)p.Type;
			jp["default_f"] = p.DefaultF;
			jp["default_i"] = p.DefaultI;
			jp["default_b"] = p.DefaultB;
			j["parameters"].push_back(jp);
		}

		nlohmann::json jroot;
		m_Root.ToJson(jroot);
		j["root"] = jroot;

		std::ofstream out(filepath);

		if (!out.is_open())
		{
			AXE_CORE_ERROR("AnimGraphAsset: nao consegui gravar '{}'.", filepath.string());
			return false;
		}

		out << j.dump(2);
		out.close();
		m_FilePath = filepath;

		// Atualiza o cache: este save NAO e "mudanca externa" — sem isto, o
		// proximo LoadFromFile veria mtime novo e recarregaria do disco a toa
		// (perdendo os ponteiros de clipe ja resolvidos em memoria).
		{
			const std::string key = GraphCacheKey(filepath);

			std::error_code mec;
			const auto mtime = std::filesystem::last_write_time(filepath, mec);

			if (!key.empty() && !mec)
			{
				auto it = s_LoadedGraphs.find(key);

				if (it != s_LoadedGraphs.end())
					it->second.MTime = mtime;
			}
		}

		AXE_CORE_INFO("AnimGraphAsset: salvo em '{}'.", filepath.string());
		return true;
	}

	bool AnimGraphAsset::Save()
	{
		if (m_FilePath.empty())
		{
			AXE_CORE_ERROR("AnimGraphAsset '{}': sem caminho — nunca foi salvo.", m_Name);
			return false;
		}

		return Save(m_FilePath);
	}

} // namespace axe