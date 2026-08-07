#include "axe/audio/sound_cue.hpp"
#include "axe/log/log.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <random>

using json = nlohmann::json;

namespace axe
{
	namespace
	{
		constexpr const char* kHeader = "SOUNDCUE_V1";
		constexpr int         kMaxDepth = 32;

		float RandRange(float a, float b)
		{
			if (b <= a)
				return a;

			// Uma engine por processo, na game thread. Evaluate nunca e
			// chamado da audio thread — se um dia for, isto vira problema, e
			// e por isso que esta escrito aqui.
			static std::mt19937 rng{ std::random_device{}() };
			std::uniform_real_distribution<float> dist(a, b);
			return dist(rng);
		}

		int RandIndexWeighted(const std::vector<float>& weights, int count, int avoid)
		{
			if (count <= 0)
				return -1;

			if (count == 1)
				return 0;

			float total = 0.0f;

			for (int i = 0; i < count; ++i)
			{
				if (i == avoid)
					continue;

				const float w = (i < (int)weights.size()) ? weights[i] : 1.0f;
				total += (w > 0.0f) ? w : 0.0f;
			}

			// Todos os pesos zerados, ou so restou o proibido: ignora o
			// NoRepeat em vez de nao tocar nada. Silencio seria pior que
			// repetir.
			if (total <= 0.0f)
				return (avoid >= 0 && count > 1) ? ((avoid + 1) % count) : 0;

			float pick = RandRange(0.0f, total);

			for (int i = 0; i < count; ++i)
			{
				if (i == avoid)
					continue;

				const float w = (i < (int)weights.size()) ? weights[i] : 1.0f;

				if (w <= 0.0f)
					continue;

				pick -= w;

				if (pick <= 0.0f)
					return i;
			}

			return (avoid == 0 && count > 1) ? 1 : 0;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	std::shared_ptr<SoundCueAsset> SoundCueAsset::Create(const std::string& name,
		const std::string& waveUUID)
	{
		auto cue = std::make_shared<SoundCueAsset>();
		cue->m_Name = name;

		SoundCueNode output;
		output.Id = cue->AllocId();
		output.Type = SoundCueNodeType::Output;
		output.EditorPos = { 520.0f, 120.0f };

		SoundCueNode mod;
		mod.Id = cue->AllocId();
		mod.Type = SoundCueNodeType::Modulator;
		mod.EditorPos = { 320.0f, 120.0f };
		mod.VolumeMin = 0.9f;
		mod.VolumeMax = 1.0f;
		mod.PitchMin = 0.95f;
		mod.PitchMax = 1.05f;

		SoundCueNode rnd;
		rnd.Id = cue->AllocId();
		rnd.Type = SoundCueNodeType::Random;
		rnd.EditorPos = { 140.0f, 120.0f };
		rnd.Weights = { 1.0f };

		SoundCueNode wave;
		wave.Id = cue->AllocId();
		wave.Type = SoundCueNodeType::WavePlayer;
		wave.EditorPos = { -60.0f, 120.0f };
		wave.WaveUUID = waveUUID;

		cue->m_Nodes = { output, mod, rnd, wave };

		cue->m_Links.push_back({ cue->AllocId(), mod.Id,  output.Id, 0 });
		cue->m_Links.push_back({ cue->AllocId(), rnd.Id,  mod.Id,    0 });
		cue->m_Links.push_back({ cue->AllocId(), wave.Id, rnd.Id,    0 });

		return cue;
	}

	SoundCueNode* SoundCueAsset::FindNode(int id)
	{
		for (auto& n : m_Nodes)
			if (n.Id == id)
				return &n;

		return nullptr;
	}

	const SoundCueNode* SoundCueAsset::FindNode(int id) const
	{
		for (const auto& n : m_Nodes)
			if (n.Id == id)
				return &n;

		return nullptr;
	}

	int SoundCueAsset::GetOutputNodeId() const
	{
		for (const auto& n : m_Nodes)
			if (n.Type == SoundCueNodeType::Output)
				return n.Id;

		return 0;
	}

	// ─────────────────────────────────────────────────────────────────────────
	bool SoundCueAsset::Evaluate(SoundCueResult& out) const
	{
		out = SoundCueResult{};

		const int outputId = GetOutputNodeId();

		if (outputId == 0)
		{
			AXE_CORE_WARN("SoundCue '{}': nao tem no Output.", m_Name);
			return false;
		}

		if (!EvalNode(outputId, out, 0))
			return false;

		return out.IsValid();
	}

	bool SoundCueAsset::EvalNode(int nodeId, SoundCueResult& out, int depth) const
	{
		if (depth > kMaxDepth)
		{
			AXE_CORE_ERROR("SoundCue '{}': profundidade maxima excedida — "
				"provavel ciclo no grafo.", m_Name);
			return false;
		}

		const SoundCueNode* node = FindNode(nodeId);

		if (!node)
			return false;

		// Filhos deste no, na ordem das entradas.
		std::vector<int> children;

		for (const auto& l : m_Links)
			if (l.ToNodeId == nodeId)
				children.push_back(l.FromNodeId);

		switch (node->Type)
		{
		case SoundCueNodeType::WavePlayer:
			if (node->WaveUUID.empty())
				return false;

			out.WaveUUID = node->WaveUUID;
			return true;

		case SoundCueNodeType::Output:
			if (children.empty())
			{
				AXE_CORE_WARN("SoundCue '{}': Output sem nada conectado.", m_Name);
				return false;
			}

			return EvalNode(children[0], out, depth + 1);

		case SoundCueNodeType::Random:
		{
			const int avoid = (node->NoRepeat && (int)children.size() > 1)
				? node->_LastPick : -1;

			const int idx = RandIndexWeighted(node->Weights, (int)children.size(), avoid);

			if (idx < 0)
				return false;

			node->_LastPick = idx;
			return EvalNode(children[idx], out, depth + 1);
		}

		case SoundCueNodeType::Modulator:
		{
			if (children.empty())
				return false;

			if (!EvalNode(children[0], out, depth + 1))
				return false;

			// MULTIPLICA em vez de atribuir: modulators aninhados compoem, e
			// o volume do AudioSourceComponent (ou do notify) continua
			// valendo por cima. Atribuir faria o de baixo apagar o de cima
			// dependendo da ordem de avaliacao — dependencia invisivel.
			out.Volume *= RandRange(node->VolumeMin, node->VolumeMax);
			out.Pitch *= RandRange(node->PitchMin, node->PitchMax);
			return true;
		}

		case SoundCueNodeType::Attenuation:
		{
			if (children.empty())
				return false;

			if (!EvalNode(children[0], out, depth + 1))
				return false;

			out.OverrideAttenuation = true;
			out.MinDistance = node->MinDistance;
			out.MaxDistance = node->MaxDistance;
			out.Is3D = node->Is3D;
			return true;
		}
		}

		return false;
	}

	// ─────────────────────────────────────────────────────────────────────────
	std::string SoundCueAsset::ToJson() const
	{
		json j;
		j["header"] = kHeader;
		j["name"] = m_Name;
		j["category"] = SoundCategoryToString(m_Category);
		j["bus"] = AudioBusToString(m_Bus);
		j["priority"] = m_Priority;
		j["next_id"] = m_NextId;

		json nodes = json::array();

		for (const auto& n : m_Nodes)
		{
			json jn;
			jn["id"] = n.Id;
			jn["type"] = (int)n.Type;
			jn["pos"] = { n.EditorPos.x, n.EditorPos.y };

			switch (n.Type)
			{
			case SoundCueNodeType::WavePlayer:
				jn["wave"] = n.WaveUUID;
				break;

			case SoundCueNodeType::Random:
				jn["weights"] = n.Weights;
				jn["no_repeat"] = n.NoRepeat;
				break;

			case SoundCueNodeType::Modulator:
				jn["volume_min"] = n.VolumeMin;
				jn["volume_max"] = n.VolumeMax;
				jn["pitch_min"] = n.PitchMin;
				jn["pitch_max"] = n.PitchMax;
				break;

			case SoundCueNodeType::Attenuation:
				jn["min_distance"] = n.MinDistance;
				jn["max_distance"] = n.MaxDistance;
				jn["is_3d"] = n.Is3D;
				break;

			default:
				break;
			}

			nodes.push_back(jn);
		}

		j["nodes"] = nodes;

		json links = json::array();

		for (const auto& l : m_Links)
			links.push_back({ {"id", l.Id}, {"from", l.FromNodeId},
							  {"to", l.ToNodeId}, {"input", l.ToInputIndex} });

		j["links"] = links;

		return j.dump(2);
	}

	bool SoundCueAsset::FromJson(const std::string& text)
	{
		json j;

		try
		{
			j = json::parse(text);
		}
		catch (const std::exception& e)
		{
			AXE_CORE_ERROR("SoundCue: JSON invalido — {}", e.what());
			return false;
		}

		if (j.value("header", "") != kHeader)
		{
			AXE_CORE_ERROR("SoundCue: header '{}' desconhecido (esperado {}).",
				j.value("header", ""), kHeader);
			return false;
		}

		m_Name = j.value("name", "");
		m_Category = SoundCategoryFromString(j.value("category", "Generic").c_str());
		m_Bus = AudioBusFromString(j.value("bus", "SFX").c_str());
		m_Priority = j.value("priority", 0.5f);
		m_NextId = j.value("next_id", 1);
		m_Nodes.clear();
		m_Links.clear();

		for (const auto& jn : j.value("nodes", json::array()))
		{
			SoundCueNode n;
			n.Id = jn.value("id", 0);
			n.Type = (SoundCueNodeType)jn.value("type", 1);

			if (jn.contains("pos") && jn["pos"].is_array() && jn["pos"].size() == 2)
				n.EditorPos = { jn["pos"][0].get<float>(), jn["pos"][1].get<float>() };

			n.WaveUUID = jn.value("wave", "");
			n.NoRepeat = jn.value("no_repeat", true);
			n.VolumeMin = jn.value("volume_min", 1.0f);
			n.VolumeMax = jn.value("volume_max", 1.0f);
			n.PitchMin = jn.value("pitch_min", 1.0f);
			n.PitchMax = jn.value("pitch_max", 1.0f);
			n.MinDistance = jn.value("min_distance", 1.0f);
			n.MaxDistance = jn.value("max_distance", 100.0f);
			n.Is3D = jn.value("is_3d", true);

			if (jn.contains("weights") && jn["weights"].is_array())
				n.Weights = jn["weights"].get<std::vector<float>>();

			m_Nodes.push_back(n);

			// Defesa contra id duplicado vindo de arquivo editado a mao: o
			// proximo id tem que ser maior que TODOS os existentes, senao o
			// editor cria um no que colide com um antigo e os links passam a
			// apontar pro lugar errado.
			if (n.Id >= m_NextId)
				m_NextId = n.Id + 1;
		}

		for (const auto& jl : j.value("links", json::array()))
		{
			SoundCueLink l;
			l.Id = jl.value("id", 0);
			l.FromNodeId = jl.value("from", 0);
			l.ToNodeId = jl.value("to", 0);
			l.ToInputIndex = jl.value("input", 0);

			if (l.Id >= m_NextId)
				m_NextId = l.Id + 1;

			m_Links.push_back(l);
		}

		return true;
	}

	// ─────────────────────────────────────────────────────────────────────────
	bool SoundCueAsset::Save(const std::filesystem::path& path) const
	{
		std::ofstream file(path);

		if (!file.is_open())
		{
			AXE_CORE_ERROR("SoundCue: nao foi possivel escrever '{}'.", path.string());
			return false;
		}

		file << ToJson();
		return true;
	}

	std::shared_ptr<SoundCueAsset> SoundCueAsset::LoadFromFile(const std::filesystem::path& path)
	{
		std::ifstream file(path);

		if (!file.is_open())
		{
			AXE_CORE_ERROR("SoundCue: nao foi possivel abrir '{}'.", path.string());
			return nullptr;
		}

		std::string text((std::istreambuf_iterator<char>(file)),
			std::istreambuf_iterator<char>());

		auto cue = std::make_shared<SoundCueAsset>();

		if (!cue->FromJson(text))
			return nullptr;

		if (cue->GetName().empty())
			cue->SetName(path.stem().string());

		AXE_CORE_INFO("SoundCue '{}': {} nos, {} links.",
			cue->GetName(), cue->m_Nodes.size(), cue->m_Links.size());

		return cue;
	}

} // namespace axe