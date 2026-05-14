#pragma once
#include "node_types.hpp"
#include <vector>
#include <memory>

namespace axe
{
	class MaterialGraph
	{
	public:
		MaterialGraph();

		//Cria Nodes
		Node* AddOutputNode();
		Node* AddTextureSampleNode();
		Node* AddColorNode();
		Node* AddFloatNode();
		Node* AddComment();

		Node* FindNodeByID(int id);

		//Acesso
		std::vector<std::unique_ptr<Node>>& GetNodes() { return m_Nodes; }
		std::vector<Link>& GetLinks() { return m_Links; }

		//Links
		void AddLink(ed::PinId startPin, ed::PinId endPin);
		void RemoveLink(ed::LinkId id);
		void BuildNodes();
		void BuildNode(std::unique_ptr<Node>* node);

		Pin* FindPin(ed::PinId id);
		bool IsPinLinked(ed::PinId id) const;

		int GetNextID() { return m_NextID++; }
		ed::NodeId contextNodeId = 0;
		ed::LinkId contextLinkId = 0;

		std::unique_ptr<Node>* FindNode(ed::NodeId id);
		
		std::vector<Link> m_Links;
		
	private:
		std::vector<std::unique_ptr<Node>> m_Nodes;
		
		int m_NextID = 1;
		
	};
}//namespace axe
