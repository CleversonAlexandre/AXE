#pragma once
#include "axe/core/types.hpp"
#include "axe/animation/anim_graph.hpp"        // AnimParamDecl, AnimTransition
#include "axe/animation/anim_pose_graph.hpp"
#include "axe/animation/anim_nodes.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace axe
{
	class SkeletalMeshAsset;

	// Asset `.axeanim` — o AnimGraph em disco.
	//
	// ── V2: O ASSET GUARDA UM GRAFO DE POSES ─────────────────────────────
	//
	// No v1 o asset era uma lista chapada de estados: o grafo ERA a máquina de
	// estados. Esse é o modelo da Unity, e é um beco sem saída — não existe
	// onde plugar um Two Bone IK ou um Slot de montage num grafo de estados.
	//
	// No v2 o asset guarda um AnimPoseGraph. A máquina de estados é UM NÓ:
	//
	//     State Machine ──► Two Bone IK ──► Output Pose
	//
	// ── OS PARÂMETROS VIVEM NO ASSET ─────────────────────────────────────
	//
	// E não dentro do grafo raiz. Porque eles são o ESQUEMA DO BLACKBOARD do
	// personagem inteiro: um sub-grafo, três níveis abaixo, dentro de um
	// estado, lê o mesmo "Speed". Se cada grafo tivesse os seus, "Speed"
	// significaria coisas diferentes em lugares diferentes — e o bug seria
	// impossível de enxergar no editor.
	//
	// ── CLIPES POR NOME, NUNCA POR ÍNDICE ────────────────────────────────
	//
	// Um shared_ptr<AnimationClip> não vai pro JSON. A alternativa óbvia seria
	// o ÍNDICE do clipe no personagem — e é uma armadilha: o usuário reordena
	// ou remove uma animação no `.axeskel`, e todo nó do grafo passa a tocar o
	// clipe errado, sem erro nenhum.
	class AXE_API AnimGraphAsset
	{
	public:
		static std::shared_ptr<AnimGraphAsset> Create(const std::string& name,
			const std::string& skeletonUUID);

		static std::shared_ptr<AnimGraphAsset> LoadFromFile(const std::filesystem::path& filepath);

		bool Save(const std::filesystem::path& filepath);
		bool Save();

		// Religa os nomes de clipe aos clipes reais e constrói os BlendSpace1D.
		//
		// PERCORRE A ÁRVORE INTEIRA — inclusive os sub-grafos dentro de cada
		// estado de cada máquina de estados, em qualquer profundidade. Um nó
		// esquecido aqui toca bind pose em silêncio.
		//
		// Devolve false se algum clipe referenciado não existir, mas religa
		// tudo que deu: o editor precisa ABRIR pra que você possa CONSERTAR,
		// em vez de encarar uma janela vazia.
		bool Resolve(const SkeletalMeshAsset& skeleton);

		AnimPoseGraph& GetRoot() { return m_Root; }
		const AnimPoseGraph& GetRoot() const { return m_Root; }

		// ── Versao ───────────────────────────────────────────────────────────
		//
		// Toda AnimGraphInstance roda um CLONE do grafo — e um clone nao ve
		// edicoes feitas no asset depois dele. Este contador e o aviso: o
		// editor da um Bump ao SALVAR, e cada instancia (personagens na cena)
		// percebe no proximo Update e re-clona sozinha. Sem isto, o Y Bot da
		// cena tocava eternamente a versao do grafo de quando foi criado.
		int  GetVersion() const { return m_Version; }
		void BumpVersion() { ++m_Version; }

		std::vector<AnimParamDecl>& GetParameters() { return m_Parameters; }
		const std::vector<AnimParamDecl>& GetParameters() const { return m_Parameters; }

		// Semeia o blackboard com os defaults declarados.
		//
		// Triggers são IGNORADOS de propósito: um pulso armado no frame 1
		// dispararia a transição sozinho, sem ninguém ter apertado nada.
		void SeedParameters(AnimParameters& params) const;

		const std::string& GetSkeletonUUID() const { return m_SkeletonUUID; }
		void SetSkeletonUUID(const std::string& uuid) { m_SkeletonUUID = uuid; }

		const std::string& GetName() const { return m_Name; }
		const std::filesystem::path& GetFilePath() const { return m_FilePath; }

	private:
		std::string           m_Name;
		std::string           m_SkeletonUUID;
		std::filesystem::path m_FilePath;

		std::vector<AnimParamDecl> m_Parameters;
		AnimPoseGraph              m_Root;
		int                        m_Version = 0;
	};

} // namespace axe