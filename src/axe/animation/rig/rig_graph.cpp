#include "rig_graph.hpp"
#include "axe/log/log.hpp"

#include <algorithm>

namespace axe
{
	// ═══ RigNode — helpers ═══════════════════════════════════════════════════

	void RigNode::AddIn(const std::string& name, RigPinType type)
	{
		RigPin p;
		p.Name = name;
		p.Type = type;
		Inputs.push_back(std::move(p));
	}

	void RigNode::AddInFloat(const std::string& name, float def)
	{
		RigPin p;
		p.Name = name;
		p.Type = RigPinType::Float;
		p.Default.Float = def;
		Inputs.push_back(std::move(p));
	}

	void RigNode::AddInBool(const std::string& name, bool def)
	{
		RigPin p;
		p.Name = name;
		p.Type = RigPinType::Bool;
		p.Default.Bool = def;
		Inputs.push_back(std::move(p));
	}

	void RigNode::AddInItem(const std::string& name, RigElementType type)
	{
		RigPin p;
		p.Name = name;
		p.Type = RigPinType::Item;
		p.Default.ItemType = type;
		Inputs.push_back(std::move(p));
	}

	void RigNode::AddOut(const std::string& name, RigPinType type)
	{
		RigPin p;
		p.Name = name;
		p.Type = type;
		Outputs.push_back(std::move(p));
	}

	RigPinValue RigNode::Read(RigExecContext& ctx, int pin) const
	{
		if (!ctx.Graph)
			return (pin >= 0 && pin < (int)Inputs.size()) ? Inputs[pin].Default : RigPinValue{};

		return ctx.Graph->EvalPin(ctx, Id, pin);
	}

	float RigNode::ReadFloat(RigExecContext& ctx, int pin) const
	{
		return Read(ctx, pin).Float;
	}

	bool RigNode::ReadBool(RigExecContext& ctx, int pin) const
	{
		return Read(ctx, pin).Bool;
	}

	glm::vec3 RigNode::ReadVector(RigExecContext& ctx, int pin) const
	{
		return Read(ctx, pin).Vector;
	}

	int RigNode::ReadItem(RigExecContext& ctx, int pin) const
	{
		if (!ctx.Hierarchy)
			return -1;

		const RigPinValue v = Read(ctx, pin);

		if (v.ItemName.empty())
			return -1;

		// Primeiro o casamento por tipo (Bone e Control podem ter o MESMO
		// nome — "hand_l" o osso e "hand_l" o controle sao coisas distintas, e
		// pegar o errado deforma o personagem em silencio).
		const int exact = ctx.Hierarchy->Find(v.ItemName, v.ItemType);

		if (exact >= 0)
			return exact;

		// Senao, tolerante a prefixo de namespace (mixamorig:).
		return ctx.Hierarchy->FindFlexible(v.ItemName);
	}

	// ═══ RigGraph ════════════════════════════════════════════════════════════

	RigGraph::RigGraph(const RigGraph& other)
	{
		*this = other;
	}

	RigGraph& RigGraph::operator=(const RigGraph& other)
	{
		if (this == &other)
			return *this;

		m_Nodes.clear();
		m_Nodes.reserve(other.m_Nodes.size());

		for (const auto& n : other.m_Nodes)
		{
			auto c = n->Clone();

			// Clone() copia o estado do no; a identidade e a posicao sao do
			// GRAFO, entao sao restauradas aqui — um Clone que esquecesse
			// disso quebraria todos os fios, que referenciam por Id.
			c->Id = n->Id;
			c->Title = n->Title;
			c->EditorX = n->EditorX;
			c->EditorY = n->EditorY;

			m_Nodes.push_back(std::move(c));
		}

		m_ExecLinks = other.m_ExecLinks;
		m_DataLinks = other.m_DataLinks;
		m_NextId = other.m_NextId;

		return *this;
	}

	int RigGraph::AddNode(std::unique_ptr<RigNode> node)
	{
		if (!node)
			return -1;

		const int id = m_NextId++;
		node->Id = id;

		m_Nodes.push_back(std::move(node));
		return id;
	}

	int RigGraph::AddNodeWithId(std::unique_ptr<RigNode> node, int id)
	{
		if (!node || id <= 0)
			return -1;

		// Id ja ocupado: cai no caminho normal em vez de criar um duplicado.
		if (FindNode(id))
			return AddNode(std::move(node));

		node->Id = id;
		m_Nodes.push_back(std::move(node));

		m_NextId = std::max(m_NextId, id + 1);
		return id;
	}

	void RigGraph::RemoveNode(int id)
	{
		// Os fios morrem junto. Um fio apontando pra um no que nao existe mais
		// e a origem classica do crash "so acontece depois de apagar um no".
		m_ExecLinks.erase(
			std::remove_if(m_ExecLinks.begin(), m_ExecLinks.end(),
				[id](const RigExecLink& l) { return l.FromNode == id || l.ToNode == id; }),
			m_ExecLinks.end());

		m_DataLinks.erase(
			std::remove_if(m_DataLinks.begin(), m_DataLinks.end(),
				[id](const RigDataLink& l) { return l.FromNode == id || l.ToNode == id; }),
			m_DataLinks.end());

		m_Nodes.erase(
			std::remove_if(m_Nodes.begin(), m_Nodes.end(),
				[id](const std::unique_ptr<RigNode>& n) { return n->Id == id; }),
			m_Nodes.end());
	}

	RigNode* RigGraph::FindNode(int id)
	{
		for (auto& n : m_Nodes)
			if (n->Id == id)
				return n.get();

		return nullptr;
	}

	const RigNode* RigGraph::FindNode(int id) const
	{
		for (const auto& n : m_Nodes)
			if (n->Id == id)
				return n.get();

		return nullptr;
	}

	void RigGraph::LinkExec(int fromNode, int fromExec, int toNode)
	{
		UnlinkExec(fromNode, fromExec);

		RigExecLink l;
		l.FromNode = fromNode;
		l.FromExec = fromExec;
		l.ToNode = toNode;

		m_ExecLinks.push_back(l);
	}

	void RigGraph::LinkData(int fromNode, int fromPin, int toNode, int toPin)
	{
		UnlinkDataInput(toNode, toPin);

		RigDataLink l;
		l.FromNode = fromNode;
		l.FromPin = fromPin;
		l.ToNode = toNode;
		l.ToPin = toPin;

		m_DataLinks.push_back(l);
	}

	void RigGraph::UnlinkExec(int fromNode, int fromExec)
	{
		m_ExecLinks.erase(
			std::remove_if(m_ExecLinks.begin(), m_ExecLinks.end(),
				[&](const RigExecLink& l)
				{
					return l.FromNode == fromNode && l.FromExec == fromExec;
				}),
			m_ExecLinks.end());
	}

	void RigGraph::UnlinkDataInput(int toNode, int toPin)
	{
		m_DataLinks.erase(
			std::remove_if(m_DataLinks.begin(), m_DataLinks.end(),
				[&](const RigDataLink& l)
				{
					return l.ToNode == toNode && l.ToPin == toPin;
				}),
			m_DataLinks.end());
	}

	void RigGraph::Clear()
	{
		m_Nodes.clear();
		m_ExecLinks.clear();
		m_DataLinks.clear();
		m_NextId = 1;
	}

	int RigGraph::FindExecTarget(int fromNode, int fromExec) const
	{
		for (const auto& l : m_ExecLinks)
			if (l.FromNode == fromNode && l.FromExec == fromExec)
				return l.ToNode;

		return -1;
	}

	void RigGraph::EvalOutputCached(RigExecContext& ctx, int nodeId, int pin,
		RigPinValue& out) const
	{
		// O cache e indexado por posicao no vetor de nos, nao por Id: os Ids
		// crescem pra sempre e nunca sao reaproveitados, entao indexar por eles
		// desperdicaria memoria sem limite num grafo muito editado.
		int slot = -1;

		for (std::size_t i = 0; i < m_Nodes.size(); ++i)
			if (m_Nodes[i]->Id == nodeId) { slot = (int)i; break; }

		if (slot < 0)
			return;

		const RigNode* node = m_Nodes[slot].get();

		const int maxOut = std::max(1, (int)node->Outputs.size());
		const std::size_t key = (std::size_t)slot * 8 + (std::size_t)std::min(pin, 7);

		if (key < m_Cached.size() && m_Cached[key])
		{
			out = m_Cache[key];
			return;
		}

		// ── Protecao contra ciclo ────────────────────────────────────────────
		//
		// Se este no ja esta sendo avaliado mais acima na propria cadeia de
		// pull, o grafo se referencia. Sem esta guarda a recursao desce ate
		// estourar a pilha — e um travamento duro no meio do frame, sem
		// nenhuma mensagem que aponte pro no culpado.
		if (slot < (int)m_Evaluating.size() && m_Evaluating[slot])
		{
			if (!m_CycleReported)
			{
				AXE_CORE_ERROR("RigGraph: CICLO de dados no no '{}' (id {}) — a saida "
					"dele depende dela mesma. O valor sera zero.",
					node->TypeName(), nodeId);

				m_CycleReported = true;
			}

			out = RigPinValue{};
			return;
		}

		if (slot < (int)m_Evaluating.size())
			m_Evaluating[slot] = true;

		const_cast<RigNode*>(node)->EvalOutput(ctx, pin, out);

		if (slot < (int)m_Evaluating.size())
			m_Evaluating[slot] = false;

		if (key < m_Cache.size())
		{
			m_Cache[key] = out;
			m_Cached[key] = true;
		}

		(void)maxOut;
	}

	RigPinValue RigGraph::EvalPin(RigExecContext& ctx, int nodeId, int pin) const
	{
		const RigNode* node = FindNode(nodeId);

		if (!node || pin < 0 || pin >= (int)node->Inputs.size())
			return RigPinValue{};

		// Tem fio? O valor vem da saida do outro no.
		for (const auto& l : m_DataLinks)
		{
			if (l.ToNode != nodeId || l.ToPin != pin)
				continue;

			RigPinValue v;
			EvalOutputCached(ctx, l.FromNode, l.FromPin, v);
			return v;
		}

		// Sem fio: o valor digitado no proprio pino.
		return node->Inputs[pin].Default;
	}

	void RigGraph::RunExecPin(RigExecContext& ctx, int nodeId, int execPin)
	{
		int current = FindExecTarget(nodeId, execPin);

		// Teto COMPARTILHADO: um laco nos fios brancos travaria a engine
		// inteira, entao paramos com uma mensagem que aponta o problema.
		const int kMaxSteps = 4096;

		while (current >= 0)
		{
			if (++m_Steps > kMaxSteps)
			{
				AXE_CORE_ERROR("RigGraph: excedeu {} passos de execucao — provavel "
					"LACO nos fios brancos. O solve foi interrompido.", kMaxSteps);
				return;
			}

			RigNode* node = FindNode(current);

			if (!node)
				return;

			node->Execute(ctx);

			// Quem cuida do proprio fluxo (Sequence) ja rodou TODAS as saidas
			// dentro do Execute — seguir a saida 0 aqui rodaria a primeira
			// corrente duas vezes.
			current = (node->HandlesOwnFlow() || node->ExecOut.empty())
				? -1
				: FindExecTarget(current, 0);
		}
	}

	void RigGraph::InvalidateDataCache()
	{
		std::fill(m_Cached.begin(), m_Cached.end(), false);
	}

	void RigGraph::Execute(RigExecContext& ctx, const char* eventType)
	{
		ctx.Graph = this;

		// Cache e guardas valem por UMA execucao. Um trace de chao do frame
		// anterior nao pode vazar pro proximo.
		m_Cache.assign(m_Nodes.size() * 8, RigPinValue{});
		m_Cached.assign(m_Nodes.size() * 8, false);
		m_Evaluating.assign(m_Nodes.size(), false);
		m_CycleReported = false;
		m_Steps = 0;

		int eventId = -1;

		for (const auto& n : m_Nodes)
			if (std::string(n->TypeName()) == eventType)
			{
				eventId = n->Id;
				break;
			}

		// Sem o evento, nao ha o que rodar. Um rig recem-criado cai aqui, e o
		// certo e o personagem seguir animado normalmente.
		if (eventId < 0)
			return;

		RunExecPin(ctx, eventId, 0);
	}

} // namespace axe