#pragma once
#include "axe/scene/scene.hpp"
#include <entt/entt.hpp>

namespace axe
{
	class ViewportRenderer;

	struct EditorContext
	{
		Scene* ActiveScene = nullptr;
		entt::entity SelectedEntity = entt::null;

		// ── O RENDERER DO VIEWPORT ───────────────────────────────────────────
		//
		// Ponteiro cru e nao-dono: o EditorLayer e quem o cria e o destroi. Esta
		// aqui porque o contexto ja e o lugar onde uma janela pergunta coisas do
		// editor inteiro (a cena, a selecao), e o gizmo e a proxima delas.
		//
		// Quem precisa: ferramentas que manipulam algo que NAO e uma entidade —
		// o Sequencer move ossos e controles de rig, que nao tem
		// TransformComponent e portanto nao entram pelo caminho normal do
		// gizmo. Elas pedem um gizmo externo por aqui (ver
		// ViewportRenderer::SetExternalGizmo).
		//
		// Sempre testar contra nulo: em modos headless e no preview isolado do
		// Control Rig nao ha viewport principal nenhum.
		ViewportRenderer* Viewport = nullptr;

		bool HasSelection() const
		{
			return SelectedEntity != entt::null &&
				ActiveScene &&
				ActiveScene->GetRegistry().valid(SelectedEntity);
		}

		void ClearSelection()
		{
			SelectedEntity = entt::null;
		}

		void Select(entt::entity entity)
		{
			SelectedEntity = entity;
		}

		// Atalho para buscar componente do objeto selecionado
		template<typename T>
		T* GetComponent()
		{
			if (!HasSelection()) return nullptr;
			return ActiveScene->GetRegistry().try_get<T>(SelectedEntity);
		}
	};

} // namespace axe