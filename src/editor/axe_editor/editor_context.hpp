#pragma once
#include "axe/scene/scene.hpp"
#include <entt/entt.hpp>

namespace axe
{

	struct EditorContext
	{
		Scene* ActiveScene = nullptr;
		entt::entity SelectedEntity = entt::null;

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