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

	// ═══ Funcoes ═════════════════════════════════════════════════════════════

	RigFunction* ControlRigAsset::AddFunction(const std::string& name)
	{
		RigFunction f;

		// Nome unico: o no Call resolve a funcao POR NOME, entao duas iguais
		// fariam a chamada apontar pra qualquer uma das duas — e o usuario nao
		// teria como saber qual.
		const std::string base = name.empty() ? std::string("NewFunction") : name;
		std::string unique = base;

		int n = 1;

		while (FindFunction(unique))
			unique = base + "_" + std::to_string(++n);

		f.Name = unique;

		// Nasce com o par ja ligado. Uma funcao vazia de verdade nao teria como
		// receber a corrente de execucao, e voce descobriria isso so depois de
		// entrar nela e nao achar por onde comecar.
		auto entry = CreateRigNode("Entry");
		auto ret = CreateRigNode("Return");

		if (entry && ret)
		{
			entry->EditorX = 0.0f;
			entry->EditorY = 0.0f;
			ret->EditorX = 400.0f;
			ret->EditorY = 0.0f;

			const int a = f.Graph.AddNode(std::move(entry));
			const int b = f.Graph.AddNode(std::move(ret));

			f.Graph.LinkExec(a, 0, b);
		}

		m_Functions.push_back(std::move(f));

		return &m_Functions.back();
	}

	void ControlRigAsset::BindFunctionLibrary(RigExecContext& ctx)
	{
		// `this` no capture e seguro: o contexto vive um solve, e o asset vive
		// mais que isso — quem executa ja segura um shared_ptr dele.
		ctx.ResolveFunction = [this](const std::string& name) -> RigGraph*
			{
				RigFunction* f = FindFunction(name);
				return f ? &f->Graph : nullptr;
			};
	}

	RigFunction* ControlRigAsset::FindFunction(const std::string& name)
	{
		for (auto& f : m_Functions)
			if (f.Name == name)
				return &f;

		return nullptr;
	}

	const RigFunction* ControlRigAsset::FindFunction(const std::string& name) const
	{
		for (const auto& f : m_Functions)
			if (f.Name == name)
				return &f;

		return nullptr;
	}

	void ControlRigAsset::RemoveFunction(int index)
	{
		if (index < 0 || index >= (int)m_Functions.size())
			return;

		// Os nos Call orfaos ficam. Sem definicao eles viram no-op e continuam na
		// tela com o nome que sumiu — visivel, e desfazivel com Ctrl+Z. Apaga-los
		// seria mexer no grafo do usuario em silencio.
		m_Functions.erase(m_Functions.begin() + index);
	}

	bool ControlRigAsset::RenameFunction(int index, const std::string& newName)
	{
		if (index < 0 || index >= (int)m_Functions.size() || newName.empty())
			return false;

		const std::string old = m_Functions[(std::size_t)index].Name;

		if (old == newName)
			return true;

		if (FindFunction(newName))
			return false;

		m_Functions[(std::size_t)index].Name = newName;

		// Atualiza as chamadas em TODO grafo do asset — o principal e o de cada
		// funcao, inclusive o da propria funcao renomeada (recursao e um caso
		// valido). Sem isto, renomear quebraria as chamadas sem aviso nenhum.
		const auto fix = [&](RigGraph& g)
			{
				for (const auto& n : g.GetNodes())
				{
					if (std::string(n->TypeName()) != "CallFunction")
						continue;

					nlohmann::json j = nlohmann::json::object();
					n->Serialize(j);

					if (j.value("fn", std::string()) != old)
						continue;

					j["fn"] = newName;
					n->Deserialize(j);
				}
			};

		fix(m_Graph);

		for (auto& f : m_Functions)
			fix(f.Graph);

		return true;
	}

	bool ControlRigAsset::Save(const std::filesystem::path& filepath)
	{
		nlohmann::json j;

		j["version"] = 1;
		j["name"] = m_Name;
		j["skeleton"] = m_SkeletonUUID;

		// ── Hierarquia ───────────────────────────────────────────────────────
		j["elements"] = nlohmann::json::array();

		int elementIndex = -1;

		for (const auto& e : m_Hierarchy.GetElements())
		{
			++elementIndex;

			nlohmann::json je;

			je["name"] = e.Name;
			je["type"] = (int)e.Type;
			je["parent"] = e.Parent;

			// So o Initial. Ver o comentario do cabecalho do header.
			je["initial"] = SaveRigTransform(e.Initial);

			// ── A POSE NAO VAI PRO DISCO ─────────────────────────────────────
			//
			// Value e POSE, e pose nao e definicao de rig. Gravar aqui
			// significava que deixar um braco pra cima no editor e salvar
			// levava esse braco pro JOGO, em toda instancia do personagem —
			// e era exatamente o medo que fazia mexer no rig parecer arriscado.
			//
			// Entao a pose e estado de SESSAO: existe enquanto o editor esta
			// aberto, e o preview trabalha numa copia da hierarquia (ver
			// ControlRigWindow::PreviewHierarchy). O que persiste e o Initial,
			// que e onde o controle repousa — isso sim e o rig.
			//
			// Quando o Sequencer existir, ele guarda as keys no PROPRIO asset
			// dele. Pose animada pertence a uma animacao, nao ao rig.
			//
			// (void) elementIndex: o indice segue util pro laco e pra qualquer
			// campo por elemento que venha depois.
			(void)elementIndex;

			if (e.Type == RigElementType::Control)
			{
				je["vtype"] = (int)e.ValueType;
				je["bval"] = e.BoolValue;
				je["fval"] = e.FloatValue;
				je["srcbone"] = e.SourceBone;

				je["shape"] = (int)e.Shape;
				je["color"] = { e.ShapeColor.r, e.ShapeColor.g, e.ShapeColor.b };
				je["size"] = e.ShapeSize;
				je["offset"] = SaveRigTransform(e.ShapeOffset);
			}

			j["elements"].push_back(std::move(je));
		}

		// ── Grafo ────────────────────────────────────────────────────────────
		//
		// O grafo grava a si mesmo. Ate aqui esta funcao conhecia o formato
		// interno de no e de fio, e por isso era o UNICO lugar capaz de gravar
		// um grafo — o que travava qualquer no que quisesse conter um subgrafo.
		//
		// Atribuicao chave a chave, e nao update(): o envelope fica explicito, e
		// nao dependemos de qual versao do nlohmann esta vendorizada. O resto do
		// arquivo (versao, nome, elementos) continua sendo assunto daqui.
		nlohmann::json graph = m_Graph.ToJson();

		j["nodes"] = std::move(graph["nodes"]);
		j["exec_links"] = std::move(graph["exec_links"]);
		j["data_links"] = std::move(graph["data_links"]);

		// ── Funcoes ──────────────────────────────────────────────────────────
		//
		// So gravamos a chave quando ha funcao: um rig que nunca criou nenhuma
		// sai byte a byte igual ao de antes deste recurso existir.
		if (!m_Functions.empty())
		{
			j["functions"] = nlohmann::json::array();

			for (const auto& f : m_Functions)
			{
				nlohmann::json jf;

				jf["name"] = f.Name;
				jf["ins"] = nlohmann::json::array();
				jf["outs"] = nlohmann::json::array();

				for (const auto& p : f.Inputs)
					jf["ins"].push_back({ { "n", p.Name }, { "t", (int)p.Type } });

				for (const auto& p : f.Outputs)
					jf["outs"].push_back({ { "n", p.Name }, { "t", (int)p.Type } });

				// O grafo da funcao se grava sozinho, no mesmo formato do
				// principal. E a razao inteira do passo A1 ter existido.
				jf["graph"] = f.Graph.ToJson();

				j["functions"].push_back(std::move(jf));
			}
		}

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
					e.Initial = LoadRigTransform(je["initial"]);

				// A LEITURA fica, mesmo tendo parado de gravar.
				//
				// Um .axerig salvo enquanto o Value ainda ia pro disco tem a
				// chave, e ignora-la faria a pose sumir de um save pro outro sem
				// explicacao. Lendo, o rig abre igual ao que voce deixou; no
				// proximo save a chave desaparece sozinha.
				//
				// Ausente = identidade, que e o caso de todo arquivo novo.
				if (je.contains("value"))
					e.Value = LoadRigTransform(je["value"]);

				// Current nasce do repouso MAIS a pose.
				e.Current = BoneTransform::FromMatrix(
					e.Initial.ToMatrix() * e.Value.ToMatrix());

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
						e.ShapeOffset = LoadRigTransform(je["offset"]);
				}

				// Empurramos direto no vetor em vez de usar Add(): Add recusaria
				// um pai de indice maior, e o arquivo ja veio em ordem valida.
				elems.push_back(std::move(e));
			}
		}

		// ── Funcoes ──────────────────────────────────────────────────────────
		//
		// ANTES do grafo principal: os nos Call reconstroem os proprios pinos a
		// partir da definicao, e ela precisa existir quando eles carregarem.
		if (j.contains("functions") && j["functions"].is_array())
		{
			for (const auto& jf : j["functions"])
			{
				RigFunction f;

				f.Name = jf.value("name", std::string("Function"));

				const auto loadParams = [](const nlohmann::json& arr,
					std::vector<RigFunctionParam>& out)
					{
						if (!arr.is_array())
							return;

						for (const auto& jp : arr)
						{
							RigFunctionParam p;
							p.Name = jp.value("n", std::string("Param"));
							p.Type = (RigPinType)jp.value("t", (int)RigPinType::Float);

							out.push_back(std::move(p));
						}
					};

				if (jf.contains("ins"))
					loadParams(jf["ins"], f.Inputs);

				if (jf.contains("outs"))
					loadParams(jf["outs"], f.Outputs);

				if (jf.contains("graph") && jf["graph"].is_object())
					f.Graph.FromJson(jf["graph"]);

				rig->m_Functions.push_back(std::move(f));
			}
		}

		// ── Grafo ────────────────────────────────────────────────────────────
		rig->m_Graph.FromJson(j);

		AXE_CORE_INFO("ControlRig - CONTROLRIG_V1: '{}' carregado ({} elementos, {} nos).",
			rig->m_Name, rig->m_Hierarchy.Size(), rig->m_Graph.GetNodes().size());

		s_RigCache[cacheKey] = rig;

		return rig;
	}

} // namespace axe