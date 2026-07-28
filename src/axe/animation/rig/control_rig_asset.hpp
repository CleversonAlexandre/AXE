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

		std::filesystem::path m_Path;
		uint32_t m_Version = 1;
	};

} // namespace axe