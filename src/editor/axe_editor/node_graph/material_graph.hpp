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
		Node* AddFractNode();          // MATFUNC_V1
		// PRIMITIVES_V1 — builtins de GLSL, 1:1 com a linguagem.
		Node* AddFloorNode();
		Node* AddCeilNode();
		Node* AddRoundNode();
		Node* AddSqrtNode();
		Node* AddSignNode();
		Node* AddModNode();
		Node* AddOneMinusNode();
		Node* AddWorldPositionNode();
		Node* AddVertexNormalNode();   // WPO_V1
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

		// ── WATER_NODES_V1 ────────────────────────────────────────────────
		//
		// Camera Position e Pixel Depth existiam como CONCEITO no shader
		// gerado (u_CameraPosition e v_FragPos sao globais nele) e nao tinham
		// node nenhum. Sem eles nao havia como escrever no grafo o termo mais
		// basico de agua estilizada — "quanta agua ha entre esta superficie e
		// o fundo" — nem nevoa por distancia, nem dissolve por proximidade.
		//
		// O Camera Vector nao servia: ele e NORMALIZADO, entao o comprimento
		// dele e sempre 1.
		Node* AddCameraPositionNode();
		Node* AddPixelDepthNode();
		Node* AddSceneWorldPositionNode();  // WATER_DEPTH_V1
		Node* AddSceneHeightNode();         // SCENE_HEIGHT_V1
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

		// ── MATFUNC_V1 — os tres nodes de Material Function ──────────────────
		//
		// Function Input e Function Output so fazem sentido DENTRO de um
		// `.axematfunc`; Material Function e a CHAMADA, e so faz sentido fora.
		// Os tres aparecem no menu em qualquer grafo assim mesmo, seguindo o que
		// esta escrito no menu dos nodes de Screen: node que some conforme o
		// contexto esconde do autor que ele existe. Fora de lugar, compilam para
		// valor neutro com um aviso no Shader Log.
		//
		// Function Input/Output guardam o nome do parametro em Node::StringValue
		// e o tipo em Node::CustomOutputType — os dois campos livres que o
		// node_types.hpp ja documenta como o padrao desta engine (campo no Node
		// cru em vez de subclasse, porque o compilador despacha por Name).
		// Material Function guarda o UUID do asset no mesmo StringValue, igual ao
		// `Call <nome>` do Script Editor.
		Node* AddFunctionInputNode();
		Node* AddFunctionOutputNode();
		Node* AddMaterialFunctionNode();
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
		Node* AddSunLightNode();      // VOLUME_SUN_V2
		Node* AddFogSettingsNode();   // VOLUME_SUN_V2b

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

		// MATFUNC_V1 — poe o pino unico do Function Input/Output em dia com o
		// nome e o tipo que o autor digitou no painel de detalhes. Nao recria o
		// pino: renomeia e re-tipa no lugar, para o ID sobreviver (o link ligado
		// nele continua valendo, e o remapeamento por posicao do Deserialize
		// continua batendo).
		void SyncFunctionIONode(Node* node);

		// MATFUNC_V1 — reconstroi os pinos de um node Material Function a partir
		// da assinatura do asset. Casa por NOME, e nao por posicao: e o que
		// preserva o fio quando o autor acrescenta um parametro no meio da lista
		// da funcao, ou muda so o tipo de um deles. Mesma regra do
		// ScriptGraph::RebuildFunctionNodePins, e pela mesma razao.
		//
		// Parametro que sumiu da assinatura leva o link junto (RemoveLinksForPin
		// antes de descartar o pino) — deixar um link apontando para pino que nao
		// existe mais e o caminho curto para uma travessia de grafo passear na
		// memoria.
		void RebuildFunctionCallPins(Node* node,
			const std::vector<MaterialFunctionParam>& inputs,
			const std::vector<MaterialFunctionParam>& outputs);
		void BuildNodes();
		void BuildNode(std::unique_ptr<Node>* node);
		void DeleteNode(ed::NodeId nodeId);

		Pin* FindPin(ed::PinId id);
		bool IsPinLinked(ed::PinId id) const;

		int GetNextID() { return m_NextID++; }

		// ── MATFUNC_V1 ───────────────────────────────────────────────────────
		//
		// Empurra o contador de ID para frente. Existe por UMA razao, e ela e
		// especifica: o compilador INLINA o grafo de uma Material Function dentro
		// do grafo do material, e os dois grafos foram desserializados cada um
		// comecando o contador em 1.
		//
		// O MaterialCompiler indexa `m_PinVariables` e `m_VisitedNodes` por ID
		// cru (int). Sem separar as faixas, o pino 7 da funcao e o pino 7 do
		// material sao a MESMA chave: um sobrescreve a variavel GLSL do outro, e
		// o sintoma nao e erro de compilacao — e um valor errado num pino que
		// nao tem nada a ver com o outro grafo.
		//
		// E exatamente a mesma classe de defeito que os grafos de funcao do
		// Script Editor tiveram com um `ed::EditorContext` compartilhado: ID que
		// recomeca por grafo, e um mapa global indexado por ele.
		//
		// So anda para frente: semear com valor MENOR que o atual reabriria a
		// colisao dentro do proprio grafo.
		void SeedNextID(int start) { if (start > m_NextID) m_NextID = start; }
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

		// ── TWO_SIDED_V1 ────────────────────────────────────────────────────
		//
		// Desenha as duas faces do triangulo, com a normal virada na de tras.
		//
		// A pipeline opaca desta engine ja e CullMode::None — quem realmente
		// muda de comportamento e a TRANSLUCIDA, que descarta a face de tras
		// para o vidro nao se sobrepor a si mesmo. Um plano de agua e o caso
		// contrario: e uma folha unica, nao tem "dentro", e some quando a
		// camera desce para debaixo dele.
		//
		// Por material, e nao global, exatamente pelo mesmo motivo que a
		// Unreal poe isto no Details do material: vidro quer uma face, agua e
		// folhagem querem duas.
		bool TwoSided = false;

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

		// ═══════════════════════════════════════════════════════════════════
		//  WPO_V1 — recalculo da NORMAL a partir do World Position Offset
		//
		//  Deslocar o vertice move a superficie, mas NAO muda a normal: ela
		//  continua sendo a do triangulo original. Uma onda alta desenhada com
		//  WPO e normal intacta se move e continua respondendo a luz como um
		//  plano — o que e exatamente a queixa de "a agua esta chapada mesmo
		//  com o plano subdividido".
		//
		//  Ligado, o vertex shader avalia o WPO em mais DOIS pontos vizinhos
		//  (deslocados por Delta ao longo da tangente e da bitangente) e tira
		//  a normal do produto vetorial entre as duas arestas resultantes. E o
		//  mesmo que a Unreal chama de "Recompute Normals" no material de
		//  agua, e custa 3 avaliacoes do subgrafo por vertice em vez de 1.
		//
		//  ── POR QUE NAO E SEMPRE LIGADO ────────────────────────────────────
		//
		//  Nem todo WPO deforma a superficie de um jeito que a normal deva
		//  seguir. Vento em folhagem desloca a folha quase inteira junto: a
		//  diferenca entre dois pontos vizinhos ali e ruido, e recalcular
		//  daria normal errada num caso que hoje funciona. Por isso o padrao e
		//  DESLIGADO — nenhum material existente muda de aparencia.
		//
		//  ── A ARMADILHA QUE O COMPILADOR AVISA ─────────────────────────────
		//
		//  A inclinacao e medida deslocando a POSICAO DE MUNDO. Uma onda
		//  montada a partir de `UV Coordinate` nao muda quando a posicao muda,
		//  entao as tres avaliacoes dao o mesmo valor e a normal sai plana —
		//  sem erro, sem aviso do driver, so o efeito faltando. O
		//  MaterialCompiler detecta isso no codigo gerado e diz no log.
		// ═══════════════════════════════════════════════════════════════════
		bool  RecomputeNormalFromWPO = false;

		// Distancia, em unidades de MUNDO, usada para medir a inclinacao.
		// Tem de ser pequena em relacao ao comprimento da onda (senao a
		// medida atravessa a crista e suaviza tudo) e grande em relacao a
		// precisao do float (senao vira ruido). 0.05 serve para agua em
		// escala de metros; onda de 20 cm pede algo perto de 0.01.
		float WPONormalDelta = 0.05f;


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