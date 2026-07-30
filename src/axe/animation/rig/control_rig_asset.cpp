#include "control_rig_asset.hpp"
#include "axe/log/log.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <unordered_map>
#include <algorithm>
#include <cctype>

namespace axe
{
	namespace
	{
		// ── CACHE POR IDENTIDADE ─────────────────────────────────────────────
		//
		// Sem isto, o editor de rig e o AnimGraph carregavam o MESMO .axerig em
		// DOIS objetos independentes. Consequencia pratica: voce editava o rig,
		// dava Play, e o personagem continuava rodando a copia velha do disco —
		// entao QUALQUER teste feito mexendo no grafo mentia ("tirei o Two Bone
		// IK do fluxo e nao mudou nada" era verdade: a mudanca nunca chegou la).
		//
		// weak_ptr: o cache NAO segura o asset vivo. Enquanto alguem usa, todos
		// recebem o MESMO objeto — e o BumpVersion do Save chega em todos, que e
		// o que faz o AnimNode_ControlRig re-clonar a copia de trabalho.
		std::unordered_map<std::string, std::weak_ptr<ControlRigAsset>> s_RigCache;

		std::string RigCacheKey(const std::filesystem::path& p)
		{
			std::error_code ec;
			std::filesystem::path c = std::filesystem::weakly_canonical(p, ec);

			if (ec)
				c = p;

			std::string key = c.string();

			// Windows nao diferencia caixa, e weakly_canonical nao normaliza a
			// caixa de trecho inexistente — sem isto "Assets/Rig.axerig" e
			// "assets/rig.axerig" viravam dois objetos. Mesma licao do
			// FindAnimationEntryBySource.
			std::transform(key.begin(), key.end(), key.begin(),
				[](unsigned char ch) { return (char)std::tolower(ch); });

			return key;
		}
	}

	namespace
	{
		nlohmann::json SaveTransform(const BoneTransform& t)
		{
			return {
				{ "t", { t.Translation.x, t.Translation.y, t.Translation.z } },
				{ "r", { t.Rotation.x, t.Rotation.y, t.Rotation.z, t.Rotation.w } },
				{ "s", { t.Scale.x, t.Scale.y, t.Scale.z } }
			};
		}

		BoneTransform LoadTransform(const nlohmann::json& j)
		{
			BoneTransform t;

			if (j.contains("t") && j["t"].size() == 3)
				t.Translation = { j["t"][0], j["t"][1], j["t"][2] };

			if (j.contains("r") && j["r"].size() == 4)
			{
				// Ordem no arquivo: x, y, z, w. O construtor do glm::quat pede
				// w PRIMEIRO — trocar a ordem aqui produz rotacoes tortas que
				// so aparecem depois de salvar e reabrir.
				t.Rotation = glm::quat(j["r"][3], j["r"][0], j["r"][1], j["r"][2]);
			}

			if (j.contains("s") && j["s"].size() == 3)
				t.Scale = { j["s"][0], j["s"][1], j["s"][2] };

			return t;
		}

		nlohmann::json SavePinValue(const RigPinValue& v)
		{
			// Grava tudo: o tipo do pino pode mudar quando um no e reescrito, e
			// um campo perdido significa o usuario redigitando o valor.
			return {
				{ "b", v.Bool },
				{ "f", v.Float },
				{ "v", { v.Vector.x, v.Vector.y, v.Vector.z } },
				{ "x", SaveTransform(v.Transform) },
				{ "item", v.ItemName },
				{ "itype", (int)v.ItemType }
			};
		}

		RigPinValue LoadPinValue(const nlohmann::json& j)
		{
			RigPinValue v;

			v.Bool = j.value("b", false);
			v.Float = j.value("f", 0.0f);

			if (j.contains("v") && j["v"].size() == 3)
				v.Vector = { j["v"][0], j["v"][1], j["v"][2] };

			if (j.contains("x"))
				v.Transform = LoadTransform(j["x"]);

			v.ItemName = j.value("item", std::string());
			v.ItemType = (RigElementType)j.value("itype", 0);

			return v;
		}
	}

	std::shared_ptr<ControlRigAsset> ControlRigAsset::Create(const std::string& name,
		const std::string& skeletonUUID, const Skeleton* skeleton)
	{
		auto rig = std::make_shared<ControlRigAsset>();

		rig->m_Name = name;
		rig->m_SkeletonUUID = skeletonUUID;

		if (skeleton)
			rig->m_Hierarchy.ImportFromSkeleton(*skeleton);

		// O rig nasce com o evento ja no grafo. Sem ele nada roda, e um grafo
		// em branco nao da nenhuma pista de por onde comecar.
		auto ev = CreateRigNode("ForwardsSolve");

		if (ev)
		{
			ev->EditorX = 60.0f;
			ev->EditorY = 120.0f;
			rig->m_Graph.AddNode(std::move(ev));
		}

		AXE_CORE_INFO("ControlRig - CONTROLRIG_V1: '{}' criado com {} elementos.",
			name, rig->m_Hierarchy.Size());

		return rig;
	}

	int ControlRigAsset::SyncNewBones(const Skeleton& skeleton)
	{
		int added = 0;

		for (const auto& b : skeleton.GetBones())
		{
			if (m_Hierarchy.Find(b.Name, RigElementType::Bone) >= 0)
				continue;

			// O pai precisa ja existir aqui. Como os ossos do esqueleto vem em
			// ordem topologica, um unico passe basta.
			const int parent = (b.ParentIndex < 0)
				? -1
				: m_Hierarchy.Find(skeleton.GetBones()[b.ParentIndex].Name, RigElementType::Bone);

			const int idx = m_Hierarchy.Add(b.Name, RigElementType::Bone, parent);

			if (idx < 0)
				continue;

			m_Hierarchy[idx].Initial = BoneTransform::FromMatrix(b.LocalBindPose);
			m_Hierarchy[idx].Current = m_Hierarchy[idx].Initial;

			++added;
		}

		if (added > 0)
			AXE_CORE_INFO("ControlRig '{}': {} osso(s) novo(s) do esqueleto adicionados.",
				m_Name, added);

		return added;
	}

	bool ControlRigAsset::Save(const std::filesystem::path& filepath)
	{
		nlohmann::json j;

		j["version"] = 1;
		j["name"] = m_Name;
		j["skeleton"] = m_SkeletonUUID;

		// ── Hierarquia ───────────────────────────────────────────────────────
		j["elements"] = nlohmann::json::array();

		for (const auto& e : m_Hierarchy.GetElements())
		{
			nlohmann::json je;

			je["name"] = e.Name;
			je["type"] = (int)e.Type;
			je["parent"] = e.Parent;

			// So o Initial. Ver o comentario do cabecalho do header.
			je["initial"] = SaveTransform(e.Initial);

			if (e.Type == RigElementType::Control)
			{
				je["vtype"] = (int)e.ValueType;
				je["bval"] = e.BoolValue;
				je["fval"] = e.FloatValue;
				je["srcbone"] = e.SourceBone;

				je["shape"] = (int)e.Shape;
				je["color"] = { e.ShapeColor.r, e.ShapeColor.g, e.ShapeColor.b };
				je["size"] = e.ShapeSize;
				je["offset"] = SaveTransform(e.ShapeOffset);
			}

			j["elements"].push_back(std::move(je));
		}

		// ── Grafo ────────────────────────────────────────────────────────────
		j["nodes"] = nlohmann::json::array();

		for (const auto& n : m_Graph.GetNodes())
		{
			nlohmann::json jn;

			jn["type"] = n->TypeName();
			jn["id"] = n->Id;
			jn["title"] = n->Title;
			jn["x"] = n->EditorX;
			jn["y"] = n->EditorY;

			// Os defaults dos pinos SAO o conteudo autoral: e onde ficam os
			// nomes de osso escolhidos e os numeros digitados. Perder isto
			// esvaziaria o rig mesmo com todos os nos e fios intactos.
			jn["pins"] = nlohmann::json::array();

			for (const auto& p : n->Inputs)
				jn["pins"].push_back(SavePinValue(p.Default));

			// OBJETO VAZIO, e nao json default-construido.
			//
			// `nlohmann::json extra;` nasce NULL, e um no cujo Serialize nao
			// escreve nada gravaria "data": null. Na volta, contains("data") da
			// true (a chave existe!) e o Deserialize recebe um null — onde
			// j.value(...) LANCA type_error, derrubando o editor ao abrir o
			// asset. Foi exatamente esse o crash.
			nlohmann::json extra = nlohmann::json::object();
			n->Serialize(extra);
			jn["data"] = std::move(extra);

			j["nodes"].push_back(std::move(jn));
		}

		j["exec_links"] = nlohmann::json::array();

		for (const auto& l : m_Graph.GetExecLinks())
			j["exec_links"].push_back({ { "from", l.FromNode }, { "pin", l.FromExec }, { "to", l.ToNode } });

		j["data_links"] = nlohmann::json::array();

		for (const auto& l : m_Graph.GetDataLinks())
			j["data_links"].push_back({ { "from", l.FromNode }, { "fpin", l.FromPin },
									   { "to", l.ToNode }, { "tpin", l.ToPin } });

		std::ofstream out(filepath);

		if (!out)
		{
			AXE_CORE_ERROR("ControlRig: nao consegui escrever '{}'.", filepath.string());
			return false;
		}

		out << j.dump(2);

		m_Path = filepath;
		return true;
	}

	bool ControlRigAsset::Save()
	{
		if (m_Path.empty())
		{
			AXE_CORE_ERROR("ControlRig '{}': sem caminho — use Save(path).", m_Name);
			return false;
		}

		return Save(m_Path);
	}

	std::shared_ptr<ControlRigAsset> ControlRigAsset::LoadFromFile(
		const std::filesystem::path& filepath)
	{
		// MESMO ARQUIVO = MESMO OBJETO. Se alguem ja tem este rig aberto (o
		// editor de rig, outro personagem), devolvemos a MESMA instancia em vez
		// de reler o disco — senao editar o rig nao tem efeito em quem ja esta
		// rodando, e o proprio teste de "mudei e nao adiantou" fica invalido.
		const std::string cacheKey = RigCacheKey(filepath);

		{
			const auto it = s_RigCache.find(cacheKey);

			if (it != s_RigCache.end())
			{
				if (std::shared_ptr<ControlRigAsset> alive = it->second.lock())
					return alive;

				// Ninguem usa mais: entrada morta, sai do mapa.
				s_RigCache.erase(it);
			}
		}

		std::ifstream in(filepath);

		if (!in)
		{
			AXE_CORE_ERROR("ControlRig: nao consegui abrir '{}'.", filepath.string());
			return nullptr;
		}

		nlohmann::json j;

		try
		{
			in >> j;
		}
		catch (const std::exception& e)
		{
			AXE_CORE_ERROR("ControlRig '{}': JSON invalido ({}).", filepath.string(), e.what());
			return nullptr;
		}

		auto rig = std::make_shared<ControlRigAsset>();

		rig->m_Name = j.value("name", filepath.stem().string());
		rig->m_SkeletonUUID = j.value("skeleton", std::string());
		rig->m_Path = filepath;

		// ── Hierarquia ───────────────────────────────────────────────────────
		if (j.contains("elements"))
		{
			auto& elems = rig->m_Hierarchy.GetElements();

			for (const auto& je : j["elements"])
			{
				RigElement e;

				e.Name = je.value("name", std::string());
				e.Type = (RigElementType)je.value("type", 0);
				e.Parent = je.value("parent", -1);

				if (je.contains("initial"))
					e.Initial = LoadTransform(je["initial"]);

				// Current nasce do Initial: o rig abre em repouso.
				e.Current = e.Initial;

				if (e.Type == RigElementType::Control)
				{
					e.ValueType = (RigControlValue)je.value("vtype", 0);
					e.BoolValue = je.value("bval", false);
					e.FloatValue = je.value("fval", 0.0f);
					e.SourceBone = je.value("srcbone", std::string());

					e.Shape = (RigControlShape)je.value("shape", 0);

					if (je.contains("color") && je["color"].size() == 3)
						e.ShapeColor = { je["color"][0], je["color"][1], je["color"][2] };

					e.ShapeSize = je.value("size", 1.0f);

					if (je.contains("offset"))
						e.ShapeOffset = LoadTransform(je["offset"]);
				}

				// Empurramos direto no vetor em vez de usar Add(): Add recusaria
				// um pai de indice maior, e o arquivo ja veio em ordem valida.
				elems.push_back(std::move(e));
			}
		}

		// ── Grafo ────────────────────────────────────────────────────────────
		if (j.contains("nodes"))
		{
			for (const auto& jn : j["nodes"])
			{
				auto node = CreateRigNode(jn.value("type", std::string()));

				// Tipo desconhecido (arquivo de uma versao mais nova, ou no
				// removido do engine): pula. Os fios dele morrem junto no
				// passo seguinte, porque o Id nunca vai existir.
				if (!node)
					continue;

				node->Title = jn.value("title", node->Title);
				node->EditorX = jn.value("x", 0.0f);
				node->EditorY = jn.value("y", 0.0f);

				// is_object() alem do contains: arquivos salvos ANTES do fix
				// acima tem "data": null, e passar isso pro Deserialize
				// derruba o editor. Com a checagem, o no simplesmente fica
				// nos valores padrao.
				if (jn.contains("data") && jn["data"].is_object())
					node->Deserialize(jn["data"]);

				// Depois do Deserialize: um no pode RECRIAR seus pinos ali (o
				// Sequence faz isso ao restaurar as saidas), e os defaults
				// precisam cair nos pinos ja definitivos.
				if (jn.contains("pins"))
				{
					const auto& jp = jn["pins"];

					for (std::size_t p = 0; p < node->Inputs.size() && p < jp.size(); ++p)
						node->Inputs[p].Default = LoadPinValue(jp[p]);
				}

				// Preserva o Id gravado: os fios referenciam por ele.
				const int wantId = jn.value("id", -1);

				if (wantId > 0)
					rig->m_Graph.AddNodeWithId(std::move(node), wantId);
				else
					rig->m_Graph.AddNode(std::move(node));
			}
		}

		auto nodeExists = [&](int id) { return rig->m_Graph.FindNode(id) != nullptr; };

		if (j.contains("exec_links"))
			for (const auto& jl : j["exec_links"])
			{
				const int from = jl.value("from", -1);
				const int to = jl.value("to", -1);

				if (nodeExists(from) && nodeExists(to))
					rig->m_Graph.LinkExec(from, jl.value("pin", 0), to);
			}

		if (j.contains("data_links"))
			for (const auto& jl : j["data_links"])
			{
				const int from = jl.value("from", -1);
				const int to = jl.value("to", -1);

				if (nodeExists(from) && nodeExists(to))
					rig->m_Graph.LinkData(from, jl.value("fpin", 0), to, jl.value("tpin", 0));
			}

		AXE_CORE_INFO("ControlRig - CONTROLRIG_V1: '{}' carregado ({} elementos, {} nos).",
			rig->m_Name, rig->m_Hierarchy.Size(), rig->m_Graph.GetNodes().size());

		s_RigCache[cacheKey] = rig;

		return rig;
	}

} // namespace axe