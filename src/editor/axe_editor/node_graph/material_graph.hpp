#pragma once
#include "node_types.hpp"
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include "axe/core/types.hpp"
namespace axe
{
	class MaterialGraph
	{
	public:
		MaterialGraph();

		//Nodes
		Node* AddMaterialOutputNode();
		Node* AddTextureSampleNode();
		Node* AddColorNode();
		Node* AddFloatNode();
		Node* AddComment();
		Node* AddRerouteNode();

		Node* AddMultiplyNode();
		Node* AddAddNode();
		Node* AddLerpNode();
		Node* AddSubtractNode();
		Node* AddDivideNode();
		Node* AddPowerNode();

		Node* AddUVNode();
		Node* AddClampNode();
		Node* AddAbsNode();
		Node* AddOneMinusNode();
		Node* AddWorldPositionNode();
		Node* FindNodeByID(int id);
		Node* AddFresnelNode();
		Node* AddNormalMapNode();

		// Lote inspirado na Unreal — math/vector utilities que só usam
		// globais já disponíveis no shader (sem precisar de novos uniforms)
		Node* AddSineNode();
		Node* AddCosineNode();
		Node* AddStepNode();
		Node* AddSmoothStepNode();
		Node* AddNormalizeNode();
		Node* AddDistanceNode();
		Node* AddDotProductNode();
		Node* AddDesaturateNode();
		Node* AddAppendNode();
		Node* AddVectorSplitNode();
		Node* AddCameraVectorNode();
		Node* AddReflectionVectorNode();

		// Animação — dependem do uniform u_Time (passagem de tempo por frame)
		Node* AddTimeNode();
		Node* AddPannerNode();
		Node* AddParticleAgeNode();
		Node* AddParticleColorNode();

		// Mais math/vetor/constantes
		Node* AddMinNode();
		Node* AddMaxNode();
		Node* AddSaturateNode();
		Node* AddLengthNode();
		Node* AddCrossProductNode();
		Node* AddIfNode();
		Node* AddNoiseNode();
		Node* AddVec2Node();
		Node* AddVec3Node();
		Node* AddTextureCoordinateNode();
		Node* AddCustomNode();   // CUSTOM_NODE_V1 — GLSL escrito a mao

		// POSTPROCESS_DOMAIN_V1 — leitura da imagem da cena
		Node* AddSceneColorNode();
		Node* AddSceneUVNode();

		// POSTPROCESS_GBUFFER_V1 — leitura da GEOMETRIA da cena
		Node* AddSceneDepthNode();
		Node* AddSceneNormalNode();
		Node* AddSceneShadingModelNode();

		// POSTPROCESS_SKY_V1 — o ceu autoravel no grafo
		Node* AddSceneIsBackgroundNode();
		Node* AddScreenRayDirectionNode();
		Node* AddSunNode();

		// Dispatcher genérico por nome — usado por Deserialize(), pelo menu
		// de criação (busca) e pelo undo de deleção, eliminando a antiga
		// triplicação do mesmo if/else gigante em 3 lugares diferentes.
		Node* AddNodeByName(const std::string& name);

		//Acesso
		std::vector<std::unique_ptr<Node>>& GetNodes() { return m_Nodes; }
		std::vector<Link>& GetLinks() { return m_Links; }

		//Links
		void AddLink(ed::PinId startPin, ed::PinId endPin);
		void RemoveLink(ed::LinkId id);

		// CUSTOM_NODE_V1 — apaga todos os links que tocam um pin.
		//
		// Necessario porque o node Custom e o unico que PERDE pinos em tempo
		// de edicao (o usuario remove uma entrada no painel). Um link apontando
		// para um pin que nao existe mais nao da erro na hora: da erro depois,
		// quando o compilador ou o desenho tentam resolver o pino.
		void RemoveLinksForPin(ed::PinId pin);
		void BuildNodes();
		void BuildNode(std::unique_ptr<Node>* node);
		void DeleteNode(ed::NodeId nodeId);

		Pin* FindPin(ed::PinId id);
		bool IsPinLinked(ed::PinId id) const;

		int GetNextID() { return m_NextID++; }
		ed::NodeId contextNodeId = 0;
		ed::LinkId contextLinkId = 0;

		std::unique_ptr<Node>* FindNode(ed::NodeId id);

		std::vector<Link> m_Links;
		bool CanCreateLink(Pin* a, Pin* b);

		// material_graph.hpp — adiciona em private:
		std::unordered_map<int, int> m_IdRemap; // ID salvo → ID atual
		Pin* FindPinByOriginalId(int savedId);  // busca pin pelo ID salvo

		nlohmann::json Serialize() const;
		void Deserialize(const nlohmann::json& json);

		// Material Domain / Blend Mode / Shading Model — configuração do
		// material inteiro (não de um node específico). Ver node_types.hpp
		// pra quais valores são realmente suportados hoje.
		MaterialDomain Domain = MaterialDomain::Surface;
		MaterialBlendMode BlendMode = MaterialBlendMode::Opaque;
		MaterialShadingModel ShadingModel = MaterialShadingModel::DefaultLit;

		// SHADING_MODEL_V1 — quantos degraus a luz difusa tem no Toon.
		//
		// Viaja no .a do g_PBR, entao e POR MATERIAL: dois personagens podem
		// ter bandas diferentes na mesma cena, sem uniform global e sem um
		// segundo lighting pass.
		//
		// 3 e a celula classica (luz / meio-tom / sombra). Acima de ~8 o
		// resultado ja e indistinguivel de sombreamento continuo, que e
		// exatamente o que o Toon nao quer — dai o teto de 16.
		int ToonSteps = 3;


		// Posições salvas durante o Draw — válidas para Serialize()
		std::unordered_map<int, ImVec2> m_NodePositions;
		void UpdateNodePosition(int nodeId, ImVec2 pos) { m_NodePositions[nodeId] = pos; }
		ImVec2 GetNodePosition(int nodeId) const {
			auto it = m_NodePositions.find(nodeId);
			return it != m_NodePositions.end() ? it->second : ImVec2(0, 0);
		}

		const std::unordered_map<int, ImVec2>& GetPendingPositions() const { return m_PendingPositions; }
		void ClearPendingPositions() { m_PendingPositions.clear(); }
		std::unordered_map<int, ImVec2> m_PendingPositions;
	private:
		std::vector<std::unique_ptr<Node>> m_Nodes;
		Node* m_MaterialOutputNode = nullptr;
		int m_NextID = 1;


		std::unordered_map<int, int> m_PinRemap;
	};
}//namespace axe