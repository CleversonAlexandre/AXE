#pragma once
#include "axe/core/types.hpp"
#include "axe/animation/rig/rig_hierarchy.hpp"
#include "axe/animation/rig/rig_nodes.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace axe
{
	class SkeletalMeshAsset;

	// ═════════════════════════════════════════════════════════════════════════
	//  ASSET .axerig — CONTROLRIG_V1
	//
	//  Guarda DUAS coisas: a hierarquia (ossos, controles, nulls) e o grafo que
	//  a manipula. Uma sem a outra nao serve — o grafo referencia elementos por
	//  nome, e a hierarquia sozinha nao faz nada.
	//
	//  ── SO O "INITIAL" VAI PRO DISCO ─────────────────────────────────────
	//
	//  Cada elemento tem Initial e Current, mas apenas o Initial e salvo. O
	//  Current e o resultado do solve DESTE frame: salvar ele significaria
	//  gravar a pose de um instante qualquer como se fosse a pose de repouso,
	//  e o rig iria "escorrendo" um pouco a cada vez que voce salvasse.
	//
	//  ── O ESQUELETO E REFERENCIA, NAO COPIA ──────────────────────────────
	//
	//  Guardamos o UUID do .axeskel. Os ossos sao IMPORTADOS dele na criacao,
	//  mas o vinculo continua: se o personagem for reimportado com ossos novos,
	//  da pra reconciliar. Copiar o esqueleto pra dentro do rig faria as duas
	//  copias divergirem em silencio.
	// ═════════════════════════════════════════════════════════════════════════
	// ── Parametro de funcao ──────────────────────────────────────────────────
	struct AXE_API RigFunctionParam
	{
		std::string Name = "Param";
		RigPinType  Type = RigPinType::Float;
	};

	// ── Funcao de rig ────────────────────────────────────────────────────────
	//
	// Um grafo nomeado, com entradas e saidas declaradas, que pode ser CHAMADO
	// de qualquer lugar do rig. Mesmo modelo do Script Editor — inclusive na
	// interface, pra quem aprendeu um nao ter que aprender o outro.
	//
	// ── POR QUE ISTO E DIFERENTE DE AGRUPAR NODES ────────────────────────
	//
	// Agrupar esconde; funcao ELIMINA. Um rig de personagem tem quatro blocos
	// quase identicos — perna esquerda, direita, braco esquerdo, direito. Uma
	// caixa por bloco deixaria a tela limpa e as quatro copias intactas: quatro
	// lugares pra corrigir o mesmo erro.
	//
	// Com funcao, e UMA definicao e quatro chamadas, com os nomes de osso
	// entrando por pino. Corrigir a definicao corrige as quatro.
	//
	// Dentro do grafo da funcao vivem exatamente um Entry (expoe os Inputs como
	// saidas) e um Return (expoe os Outputs como entradas), criados junto com
	// ela. Fora, cada uso e um no Call.
	//
	// Graph por VALOR, e nao shared_ptr como no ScriptFunction: o RigGraph tem
	// copia profunda (RigGraph(const RigGraph&), via Clone), entao copiar uma
	// funcao copia o grafo dela — e o undo, que ja guarda o asset inteiro por
	// valor, leva as funcoes junto sem nenhum trabalho extra.
	struct AXE_API RigFunction
	{
		std::string Name = "NewFunction";

		std::vector<RigFunctionParam> Inputs;
		std::vector<RigFunctionParam> Outputs;

		RigGraph Graph;
	};

	class AXE_API ControlRigAsset
	{
	public:
		// Cria um rig ja povoado com os ossos do esqueleto. Um rig vazio nao
		// tem utilidade nenhuma — voce nao teria o que referenciar nos nos.
		static std::shared_ptr<ControlRigAsset> Create(const std::string& name,
			const std::string& skeletonUUID,
			const Skeleton* skeleton);

		static std::shared_ptr<ControlRigAsset> LoadFromFile(const std::filesystem::path& filepath);

		bool Save(const std::filesystem::path& filepath);
		bool Save();

		RigHierarchy& GetHierarchy() { return m_Hierarchy; }
		const RigHierarchy& GetHierarchy() const { return m_Hierarchy; }

		RigGraph& GetGraph() { return m_Graph; }
		const RigGraph& GetGraph() const { return m_Graph; }

		// ── Funcoes ──────────────────────────────────────────────────────────

		std::vector<RigFunction>& GetFunctions() { return m_Functions; }
		const std::vector<RigFunction>& GetFunctions() const { return m_Functions; }

		// Cria uma funcao ja com Entry e Return ligados. O nome e tornado unico
		// se preciso: duas funcoes de mesmo nome fariam o no Call apontar pra
		// qualquer uma das duas.
		RigFunction* AddFunction(const std::string& name);

		// Preenche ctx.ResolveFunction apontando pra biblioteca DESTE asset.
		//
		// Um metodo e nao cada chamador montando o lambda: sao tres lugares que
		// montam contexto (o AnimNode em jogo, o preview do editor, o backward
		// solve), e um que esquecesse deixaria as funcoes silenciosamente
		// mortas naquele caminho.
		void BindFunctionLibrary(RigExecContext& ctx);

		RigFunction* FindFunction(const std::string& name);
		const RigFunction* FindFunction(const std::string& name) const;

		// Remove por indice. Os nos Call orfaos NAO sao apagados: eles viram
		// no-op e continuam na tela mostrando o nome que sumiu — apagar o
		// trabalho do usuario em silencio seria pior que deixar um no morto
		// visivel.
		void RemoveFunction(int index);

		// Renomeia e atualiza TODOS os nos Call, em todos os grafos. Sem isto,
		// renomear quebraria as chamadas sem aviso.
		bool RenameFunction(int index, const std::string& newName);

		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& n) { m_Name = n; }

		const std::string& GetSkeletonUUID() const { return m_SkeletonUUID; }
		void SetSkeletonUUID(const std::string& id) { m_SkeletonUUID = id; }

		const std::filesystem::path& GetPath() const { return m_Path; }

		// Contador de edicao. Cada instancia em execucao roda um CLONE do
		// grafo, e um clone nao ve edicoes feitas depois dele — este numero e
		// como a instancia percebe que ficou velha e precisa re-clonar. Mesmo
		// mecanismo do AnimGraphAsset.
		uint32_t GetVersion() const { return m_Version; }
		void     BumpVersion() { ++m_Version; }

		// Traz ossos que existem no esqueleto e ainda nao estao na hierarquia.
		//
		// NAO remove nada. Se o esqueleto perdeu um osso, o elemento fica —
		// junto com qualquer controle que o usuario tenha pendurado nele.
		// Apagar em silencio destruiria trabalho manual; o certo e avisar e
		// deixar a decisao com quem montou o rig.
		int SyncNewBones(const Skeleton& skeleton);

	private:
		std::string m_Name;
		std::string m_SkeletonUUID;

		RigHierarchy m_Hierarchy;
		RigGraph     m_Graph;

		// Vector e nao map: a ORDEM na lista e autoral (voce arruma as funcoes
		// como quer ver), e o numero de funcoes num rig e pequeno o bastante
		// pra busca linear por nome nao pesar.
		std::vector<RigFunction> m_Functions;

		std::filesystem::path m_Path;
		uint32_t m_Version = 1;
	};

} // namespace axe