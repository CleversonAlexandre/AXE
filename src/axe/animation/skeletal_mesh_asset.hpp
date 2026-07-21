#pragma once
#include "axe/core/types.hpp"
#include "axe/animation/skinned_mesh.hpp"
#include "axe/animation/skeleton.hpp"
#include "axe/animation/animation_clip.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace axe
{
	// Asset `.axeskel` — o personagem como o ENGINE o vê.
	//
	// Por que não usar o .fbx direto como asset:
	//
	//   O FBX é uma FONTE, não um asset. Ele traz a malha e o esqueleto, mas
	//   não tem onde guardar "este personagem também usa idle.fbx, run.fbx e
	//   attack.fbx". Sem um asset do engine, essa lista viveria só na
	//   entidade — e você reimportaria tudo em cada personagem, e perderia os
	//   clipes ao recriar a entidade.
	//
	//   O .axeskel é um arquivo pequeno (JSON) que aponta pro FBX de origem e
	//   CARREGA A LISTA DE ANIMAÇÕES. É o mesmo motivo pelo qual a Unreal
	//   cria um SkeletalMesh e AnimSequences ao importar, em vez de deixar o
	//   .fbx solto no Content Browser.
	//
	// Os dados pesados (malha, esqueleto, clipes) são resolvidos sob demanda
	// e cacheados: dez inimigos arrastados pra cena compartilham UM
	// SkeletalMeshAsset, e portanto UMA malha na GPU.
	class AXE_API SkeletalMeshAsset
	{
	public:
		struct AnimEntry
		{
			std::string           Name;         // nome do clipe (exibido na UI)
			std::filesystem::path SourceFile;   // FBX que só tem as curvas
		};

		static std::shared_ptr<SkeletalMeshAsset> Create(const std::string& name,
			const std::filesystem::path& sourceFile);

		static std::shared_ptr<SkeletalMeshAsset> LoadFromFile(const std::filesystem::path& filepath);

		bool Save(const std::filesystem::path& filepath);
		bool Save();   // regrava no próprio m_FilePath

		// Importa o FBX de origem + todos os clipes da lista. Idempotente:
		// chamar de novo não reimporta (o SkeletalMeshLoader ainda cacheia
		// por caminho, mas nem isso é tocado numa segunda chamada).
		bool Resolve();
		bool IsResolved() const { return m_Resolved; }

		// Adiciona um arquivo de animação, religa os clipes ao esqueleto DESTE
		// asset (por nome de osso) e os anexa. Retorna quantos clipes entraram.
		//
		// NÃO salva sozinho — quem chama decide quando gravar o .axeskel.
		int AddAnimation(const std::filesystem::path& file);

		void RemoveAnimation(int index);

		const std::shared_ptr<SkinnedMesh>& GetMesh() const { return m_Mesh; }
		const std::shared_ptr<Skeleton>& GetSkeleton() const { return m_Skeleton; }
		const std::vector<std::shared_ptr<AnimationClip>>& GetClips() const { return m_Clips; }

		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& n) { m_Name = n; }

		const std::filesystem::path& GetSourceFile() const { return m_SourceFile; }
		const std::filesystem::path& GetFilePath() const { return m_FilePath; }
		const std::vector<AnimEntry>& GetAnimations() const { return m_Animations; }

		// Remove uma ENTRADA (o registro do arquivo) e reconstroi a lista de
		// clipes. E o "desimportar" — os clipes daquele arquivo somem do
		// combo, e os sufixos anti-colisao reassentam.
		bool RemoveAnimation(std::size_t index);

		// ── Metadados de autoria por clipe (Animation Editor) ────────────
		//
		// Loop, RateScale, RootMotion e Notifies vivem NO CLIPE em runtime,
		// mas o clipe e reconstruido do FBX a cada Resolve — entao a copia
		// persistente mora aqui, chaveada pelo NOME (a mesma chave que o
		// grafo usa). StoreClipMeta copia do clipe vivo pro mapa; o Resolve
		// aplica o mapa de volta nos clipes recem-importados.
		void StoreClipMeta(const std::shared_ptr<AnimationClip>& clip);

		// Este arquivo esta registrado como animacao AQUI? Devolve o indice
		// da entrada, ou -1. E o que faz duplo-clique num FBX de animacao
		// abrir o Animation Editor no clipe certo — o FBX nao sabe de quem
		// e; quem sabe e o .axeskel que o referencia.
		int FindAnimationEntryBySource(const std::filesystem::path& file) const;

		// Conserta o caminho de uma entrada (arquivo movido de pasta) e
		// salva. Devolve true se ALGO mudou. Auto-cura: quem achou a entrada
		// pelo nome do arquivo chama isto e o .axeskel para de mentir.
		bool UpdateAnimationSource(std::size_t index, const std::filesystem::path& file);

	private:
		// Resolve um caminho do JSON: relativo é interpretado a partir da
		// pasta do .axeskel. É o que permite mover o projeto de máquina sem
		// quebrar as referências — gravar caminho absoluto ("C:\Users\...")
		// funcionaria só no seu PC.
		std::filesystem::path ResolvePath(const std::filesystem::path& p) const;

		// Inverso: ao salvar, torna o caminho relativo ao .axeskel se ele
		// estiver na mesma árvore.
		std::filesystem::path RelativizePath(const std::filesystem::path& p) const;

		std::string           m_Name;
		std::filesystem::path m_SourceFile;   // FBX do personagem (com skin)
		std::filesystem::path m_FilePath;     // o próprio .axeskel

		std::vector<AnimEntry> m_Animations;

		// Autoria por clipe, persistida no JSON (bloco "clip_meta").
		struct ClipMeta
		{
			bool  Loop = true;
			float RateScale = 1.0f;
			bool  RootMotion = false;
			int   TrackCount = 1;
			std::vector<AnimNotify> Notifies;
		};

		std::unordered_map<std::string, ClipMeta> m_ClipMeta;

		// Runtime — não vai pro JSON.
		std::shared_ptr<SkinnedMesh>                m_Mesh;
		std::shared_ptr<Skeleton>                   m_Skeleton;
		std::vector<std::shared_ptr<AnimationClip>> m_Clips;
		bool m_Resolved = false;
	};

} // namespace axe