#pragma once
// ═══════════════════════════════════════════════════════════════════════════
//  ANIMATION EDITOR — a "Persona" do AXE                    ANIMCLIP_EDITOR_V1
//
//  Edicao de animacoes INDIVIDUAIS, no espirito da janela de Animation
//  Sequence da Unreal:
//
//    - lista de clipes DESTE esqueleto (o "asset browser compativel")
//    - Skeleton Tree (hierarquia real dos ossos; sockets na proxima etapa)
//    - preview 3D com play/pause/scrub
//    - TIMELINE com track de NOTIFIES (som, particula, evento de script)
//    - Asset Details: Loop, Rate Scale, Root Motion
//
//  Abre com DUPLO-CLIQUE no .axeskel (arrastar pra viewport continua
//  spawnando o personagem — mesma divisao da Unreal).
//
//  ONDE OS DADOS MORAM: as propriedades e os notifies sao gravados no
//  proprio .axeskel (bloco "clip_meta", chaveado pelo NOME do clipe — a
//  mesma chave que o AnimGraph usa). O clipe e reconstruido do FBX a cada
//  Resolve; o meta e reaplicado por cima.
// ═══════════════════════════════════════════════════════════════════════════

#include "axe/animation/skeletal_mesh_asset.hpp"
#include "axe/animation/animation_world.hpp"
#include "axe/particles/particle_world.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/scene_environment.hpp"

#include <imgui.h>
#include <entt/entt.hpp>

#include <memory>
#include <string>
#include <vector>

namespace axe
{
	class ViewportRenderer;
	class Framebuffer;
	class AssetBrowser;

	class AnimClipWindow
	{
	public:
		void Initialize();

		// O botao Find do notify precisa navegar o browser ate o asset.
		void SetAssetBrowser(AssetBrowser* browser) { m_AssetBrowser = browser; }

		// Abre o editor para um personagem (.axeskel ja resolvido).
		void OpenForAsset(const std::shared_ptr<SkeletalMeshAsset>& skeleton);

		bool IsOpen() const { return m_Open; }

		// Seleciona um clipe pelo nome (usado pelo duplo-clique num FBX de
		// animacao — abre ja no clipe daquele arquivo). Nome ausente = fica
		// no primeiro.
		void SelectClipByName(const std::string& name);

		// Render 3D do preview — chamado ANTES do ImGui da frame, junto dos
		// outros previews (material/script/graph), porque desenha num
		// framebuffer proprio.
		void RenderPreview();

		void Draw();

	private:
		// ── Paineis (janelas dockaveis num dockspace proprio) ────────────
		void DrawToolbar();
		void DrawDockLayout(unsigned int dockspaceId);   // layout padrao, uma vez
		void DrawClipList();
		void DrawSkeletonTree();
		void DrawPreviewPanel();
		void DrawTimeline();
		void DrawRightPanel();     // Asset Details + notify selecionado

		// ── Preview (mesmo padrao do AnimGraph: cena propria) ────────────
		void InitPreviewScene();
		void SyncPreviewCharacter();
		void HandlePreviewInput();

		// Emite a particula de um notify Particle no preview — a timeline
		// cruzou o losango, algo tem que APARECER.
		void SpawnNotifyParticle(const AnimNotify& n);

		// ── Edicao ───────────────────────────────────────────────────────
		std::shared_ptr<AnimationClip> CurrentClip() const;
		void MarkMetaEdited();     // copia clip -> ClipMeta e marca dirty
		void SelectClip(int index);

		float PreviewTime() const;
		void  SetPreviewTime(float t);

		// ── Estado ───────────────────────────────────────────────────────
		bool m_Open = false;
		bool m_Dirty = false;

		std::shared_ptr<SkeletalMeshAsset> m_Skeleton;

		AssetBrowser* m_AssetBrowser = nullptr;

		int m_SelectedClip = -1;
		int m_SelectedNotify = -1;   // indice em CurrentClip()->Notifies
		int m_DraggingNotify = -1;   // sendo arrastado na timeline

		// Tempo capturado no right-click da lane (o popup abre depois que o
		// mouse ja saiu do lugar).
		float m_PendingAddTime = 0.0f;

		// Notifies cruzados recentemente (feedback visual no preview).
		struct FiredNotify { std::string Name; double At = 0.0; };
		std::vector<FiredNotify> m_RecentFired;
		float m_LastPreviewTime = 0.0f;

		// ── Preview scene ────────────────────────────────────────────────
		std::unique_ptr<ViewportRenderer>  m_PreviewRenderer;
		std::shared_ptr<Framebuffer>       m_PreviewFramebuffer;
		std::unique_ptr<Scene>             m_PreviewScene;
		std::unique_ptr<SceneEnvironment>  m_PreviewEnvironment;
		std::unique_ptr<AnimationWorld>    m_PreviewAnim;
		std::unique_ptr<ParticleWorld>     m_PreviewParticles;

		// FX spawnados por notify no preview: vivem alguns segundos e somem.
		struct SpawnedFx { entt::entity Entity = entt::null; double At = 0.0; };
		std::vector<SpawnedFx> m_SpawnedFx;

		entt::entity m_PreviewEntity = entt::null;
		ImVec2 m_PreviewSize{ 0, 0 };
		bool   m_PreviewHovered = false;
		bool   m_PreviewInit = false;
		bool   m_Playing = true;

		std::shared_ptr<SkeletalMeshAsset> m_PreviewAssetInScene;
	};

} // namespace axe