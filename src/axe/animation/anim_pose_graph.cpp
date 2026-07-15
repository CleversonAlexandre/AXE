#include "anim_pose_graph.hpp"
#include "anim_nodes.hpp"   // CreateAnimNode (a fabrica)
#include "axe/log/log.hpp"

#include <algorithm>

namespace axe
{
	int AnimPoseGraph::AddNode(std::unique_ptr<AnimNode> node)
	{
		if (!node)
			return -1;

		const int id = m_NextId++;
		node->Id = id;

		node->Inputs.assign(node->InputCount(), nullptr);

		m_Nodes.push_back(std::move(node));
		return id;
	}

	AnimNode* AnimPoseGraph::FindNode(int id) const
	{
		for (const auto& n : m_Nodes)
			if (n->Id == id)
				return n.get();

		return nullptr;
	}

	void AnimPoseGraph::RemoveLinksOf(int nodeId)
	{
		m_Links.erase(
			std::remove_if(m_Links.begin(), m_Links.end(),
				[nodeId](const AnimLink& l)
				{
					return l.FromNode == nodeId || l.ToNode == nodeId;
				}),
			m_Links.end());
	}

	void AnimPoseGraph::RemoveNode(int id)
	{
		// Os links vão junto — senão sobra um link apontando pra um nó que não
		// existe mais, e o Resolve() vira um ponteiro pendurado.
		RemoveLinksOf(id);

		if (m_OutputNode == id)
			m_OutputNode = -1;

		m_Nodes.erase(
			std::remove_if(m_Nodes.begin(), m_Nodes.end(),
				[id](const std::unique_ptr<AnimNode>& n) { return n->Id == id; }),
			m_Nodes.end());

		Resolve();
	}

	void AnimPoseGraph::AddLink(int fromNode, int toNode, int toPin, AnimLinkKind kind)
	{
		// Um pino de ENTRADA aceita uma origem só. Ligar outra substitui a
		// anterior — que é o que o usuário espera ao arrastar por cima.
		//
		// O `kind` faz parte da identidade do pino: o pino de dado 0 e o pino
		// de pose 0 são pinos DIFERENTES. Sem isso, ligar um float no pino 0
		// derrubaria o link de pose do pino 0.
		RemoveLinksTo(toNode, toPin, kind);

		AnimLink l;
		l.FromNode = fromNode;
		l.ToNode = toNode;
		l.ToPin = toPin;
		l.Kind = kind;

		m_Links.push_back(l);
		Resolve();
	}

	void AnimPoseGraph::RemoveLinksTo(int toNode, int toPin, AnimLinkKind kind)
	{
		m_Links.erase(
			std::remove_if(m_Links.begin(), m_Links.end(),
				[toNode, toPin, kind](const AnimLink& l)
				{
					return l.ToNode == toNode && l.ToPin == toPin && l.Kind == kind;
				}),
			m_Links.end());
	}

	void AnimPoseGraph::Resolve()
	{
		// Zera os DOIS vetores de entrada. Um link removido tem que deixar o
		// ponteiro nulo — senão o nó continua avaliando o nó antigo, que pode
		// já ter sido destruído.
		for (auto& n : m_Nodes)
		{
			n->Inputs.assign(n->InputCount(), nullptr);

			for (auto& d : n->DataInputs)
				d.Link = nullptr;
		}

		for (const auto& l : m_Links)
		{
			AnimNode* to = FindNode(l.ToNode);
			AnimNode* from = FindNode(l.FromNode);

			if (!to || !from)
				continue;

			// Auto-link seria um laço infinito na avaliação. O editor já
			// rejeita, mas um .axeanim editado à mão não passaria por ele.
			if (to == from)
			{
				AXE_CORE_WARN("AnimPoseGraph: no {} ligado a si mesmo — link ignorado.", l.ToNode);
				continue;
			}

			if (l.Kind == AnimLinkKind::Pose)
			{
				if (l.ToPin < 0 || l.ToPin >= (int)to->Inputs.size())
					continue;

				// Um nó de VALOR num pino de POSE produziria bind pose em
				// silêncio. Melhor gritar: o .axeanim está corrompido ou foi
				// editado à mão.
				if (from->OutputType() != AnimPinType::Pose)
				{
					AXE_CORE_WARN("AnimPoseGraph: no {} nao produz pose, mas esta ligado "
						"num pino de pose do no {} — link ignorado.", l.FromNode, l.ToNode);
					continue;
				}

				to->Inputs[l.ToPin] = from;
			}
			else
			{
				if (l.ToPin < 0 || l.ToPin >= (int)to->DataInputs.size())
					continue;

				// Idem no sentido inverso: uma pose ligada num pino de float
				// leria EvaluateFloat() de um nó de pose, que devolve 0.
				if (from->OutputType() == AnimPinType::Pose)
				{
					AXE_CORE_WARN("AnimPoseGraph: no {} produz pose, mas esta ligado num "
						"pino de dado do no {} — link ignorado.", l.FromNode, l.ToNode);
					continue;
				}

				to->DataInputs[l.ToPin].Link = from;
			}
		}
	}

	void AnimPoseGraph::Update(AnimEvalContext& ctx)
	{
		if (m_OutputNode < 0)
			return;

		// Update PULL, a partir da saída: só o que de fato contribui pra pose
		// final é atualizado. Um ramo desconectado no meio da edição não gasta
		// CPU nem avança tempo.
		if (AnimNode* out = FindNode(m_OutputNode))
			out->Update(ctx);
	}

	void AnimPoseGraph::Evaluate(AnimEvalContext& ctx, Pose& out)
	{
		AnimNode* root = (m_OutputNode >= 0) ? FindNode(m_OutputNode) : nullptr;

		if (!root)
		{
			// Grafo sem Output -> bind pose. Nunca pose vazia.
			if (ctx.Skel)
				Pose::FromBindPose(*ctx.Skel, out);
			return;
		}

		root->Evaluate(ctx, out);
	}

	void AnimPoseGraph::Reset()
	{
		for (auto& n : m_Nodes)
			n->Reset();
	}


	// ── Serialização ─────────────────────────────────────────────────────────

	void AnimPoseGraph::ToJson(nlohmann::json& j) const
	{
		j["output"] = m_OutputNode;
		j["next_id"] = m_NextId;

		j["nodes"] = nlohmann::json::array();

		for (const auto& n : m_Nodes)
		{
			nlohmann::json jn;

			// Campos COMUNS: gravados aqui, uma vez.
			//
			// Se cada nó gravasse o próprio Id/tipo/posição, todos repetiriam o
			// mesmo bloco — e um deles esqueceria um campo. O nó só grava o que
			// é DELE.
			jn["id"] = n->Id;
			jn["type"] = n->TypeName();
			jn["title"] = n->Title;
			jn["x"] = n->EditorX;
			jn["y"] = n->EditorY;

			// Valores inline dos pinos de dado. São o que o pino usa quando
			// NADA está ligado nele.
			jn["pins"] = nlohmann::json::array();

			for (const auto& d : n->DataInputs)
			{
				nlohmann::json jp;
				jp["name"] = d.Name;
				jp["f"] = d.InlineFloat;
				jp["b"] = d.InlineBool;
				jn["pins"].push_back(jp);
			}

			// E o que só o nó sabe.
			n->Serialize(jn);

			j["nodes"].push_back(jn);
		}

		j["links"] = nlohmann::json::array();

		for (const auto& l : m_Links)
		{
			nlohmann::json jl;
			jl["from"] = l.FromNode;
			jl["to"] = l.ToNode;
			jl["pin"] = l.ToPin;
			jl["data"] = (l.Kind == AnimLinkKind::Data);
			j["links"].push_back(jl);
		}
	}

	void AnimPoseGraph::FromJson(const nlohmann::json& j)
	{
		m_Nodes.clear();
		m_Links.clear();

		m_OutputNode = j.value("output", -1);
		m_NextId = j.value("next_id", 1);

		if (j.contains("nodes"))
		{
			for (const auto& jn : j["nodes"])
			{
				const std::string type = jn.value("type", std::string{});

				auto node = CreateAnimNode(type);

				if (!node)
				{
					// Tipo desconhecido: um .axeanim de uma versão MAIS NOVA do
					// engine, ou um nó que foi removido. Gritar e pular é melhor
					// que crashar — o resto do grafo ainda abre e o usuário
					// consegue ver o que sobrou.
					AXE_CORE_ERROR("AnimPoseGraph: tipo de no desconhecido '{}' — ignorado.", type);
					continue;
				}

				node->Id = jn.value("id", -1);
				node->Title = jn.value("title", std::string{});
				node->EditorX = jn.value("x", 0.0f);
				node->EditorY = jn.value("y", 0.0f);

				// O nó carrega os campos DELE primeiro: um StateMachine precisa
				// existir por inteiro antes de qualquer coisa olhar pra ele.
				node->Deserialize(jn);

				// Os pinos de dado são declarados pelo CONSTRUTOR do nó. Aqui só
				// restauramos os valores inline — casando por NOME, não por
				// índice.
				//
				// Por nome porque a ordem dos pinos pode mudar entre versões do
				// engine, e casar por índice faria o "Alpha" virar o "BlendTime"
				// em silêncio.
				if (jn.contains("pins"))
				{
					for (const auto& jp : jn["pins"])
					{
						const std::string pname = jp.value("name", std::string{});

						for (auto& d : node->DataInputs)
						{
							if (d.Name != pname)
								continue;

							d.InlineFloat = jp.value("f", 0.0f);
							d.InlineBool = jp.value("b", false);
							break;
						}
					}
				}

				node->Inputs.assign(node->InputCount(), nullptr);
				m_Nodes.push_back(std::move(node));
			}
		}

		if (j.contains("links"))
		{
			for (const auto& jl : j["links"])
			{
				AnimLink l;
				l.FromNode = jl.value("from", -1);
				l.ToNode = jl.value("to", -1);
				l.ToPin = jl.value("pin", 0);
				l.Kind = jl.value("data", false) ? AnimLinkKind::Data : AnimLinkKind::Pose;

				m_Links.push_back(l);
			}
		}

		Resolve();
	}


	void AnimPoseGraph::CopyFrom(const AnimPoseGraph& other)
	{
		m_Nodes.clear();
		m_Links.clear();

		m_OutputNode = other.m_OutputNode;
		m_NextId = other.m_NextId;

		// Os nos sao clonados MANTENDO o Id — e por isso os links, que sao pares
		// de Id, continuam validos sem nenhuma remapeacao.
		//
		// Se o AddNode fosse usado aqui, ele atribuiria Ids NOVOS e todos os
		// links apontariam pra lugar nenhum.
		m_Nodes.reserve(other.m_Nodes.size());

		for (const auto& n : other.m_Nodes)
			m_Nodes.push_back(n->Clone());

		m_Links = other.m_Links;

		// Religa os ponteiros internos aos nos DESTE grafo. Sem isto, o Output
		// da copia apontaria pros nos do ORIGINAL — um ponteiro pra outro grafo,
		// exatamente o bug que a copia profunda existe pra evitar.
		Resolve();
	}

} // namespace axe
