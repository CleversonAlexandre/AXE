#include "hierarchy_window.hpp"
#include "axe/scene/scene_objects.hpp"
#include "axe/mesh/mesh_factory.hpp"

#include <imgui.h>
#include "axe/mesh/primitive_uuid.hpp"
#include "axe/mesh/mesh_factory.hpp"

namespace axe
{

	// hierarchy_window.cpp

	void HierarchyWindow::Draw()
	{
		ImGui::Begin("Hierarchy");

		if (!m_Context || !m_Context->ActiveScene)
		{
			ImGui::TextDisabled("Sem cena ativa.");
			ImGui::End();
			return;
		}

		if (ImGui::IsWindowFocused())
			HandleKeyboardShortcuts();

		// Itera todas as entities que têm NameComponent
		auto view = m_Context->ActiveScene->GetRegistry().view<NameComponent>();
		for (auto entity : view)
			DrawObjectNode(entity);

		if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsWindowHovered())
			m_Context->ClearSelection();

		if (ImGui::BeginPopupContextWindow("##hierarchy_empty",
			ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			DrawContextMenuEmpty();
			ImGui::EndPopup();
		}

		ImGui::End();
	}

	void HierarchyWindow::DrawObjectNode(entt::entity entity)
	{
		auto& registry = m_Context->ActiveScene->GetRegistry();
		auto& name = registry.get<NameComponent>(entity).Name;

		bool isSelected = (m_Context->SelectedEntity == entity);

		ImGuiTreeNodeFlags flags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_Leaf;

		if (isSelected)
			flags |= ImGuiTreeNodeFlags_Selected;

		bool opened = ImGui::TreeNodeEx(
			(void*)(uint64_t)(uint32_t)entity,
			flags,
			"%s", name.c_str()
		);

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			m_Context->Select(entity);

		std::string popupID = "##ctx_" + std::to_string((uint32_t)entity);
		if (ImGui::BeginPopupContextItem(popupID.c_str()))
		{
			m_Context->Select(entity);
			DrawContextMenuObject(entity);
			ImGui::EndPopup();
		}

		if (opened)
			ImGui::TreePop();
	}

	void HierarchyWindow::DrawContextMenuObject(entt::entity entity)
	{
		if (ImGui::MenuItem("Duplicar", "Ctrl+D"))
			DuplicateSelected();

		ImGui::Separator();

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
		if (ImGui::MenuItem("Deletar", "Delete"))
			DeleteSelected();
		ImGui::PopStyleColor();
	}
	
	void HierarchyWindow::CreateObject(const std::string& name, const std::string& primitiveUUID)
	{
		auto entity = m_Context->ActiveScene->CreateEntity(name);
		auto& registry = m_Context->ActiveScene->GetRegistry();

		if (!primitiveUUID.empty())
		{
			auto mesh = MeshFactory::CreateByUUID(primitiveUUID);
			if (mesh)
			{
				auto& mc = registry.emplace<MeshComponent>(entity);
				mc.Data = mesh;
				mc.AssetUUID = primitiveUUID;
			}
		}

		m_Context->Select(entity);
	}

	void HierarchyWindow::CreateLight()
	{
		auto entity = m_Context->ActiveScene->CreateLight();
		m_Context->Select(entity);
	}

	void HierarchyWindow::DeleteSelected()
	{
		if (!m_Context->HasSelection())
			return;

		entt::entity entity = m_Context->SelectedEntity;
		m_Context->ClearSelection();
		m_Context->ActiveScene->DestroyEntity(entity);
	}

	void HierarchyWindow::DuplicateSelected()
	{
		if (!m_Context->HasSelection())
			return;

		entt::entity copy = m_Context->ActiveScene->DuplicateEntity(m_Context->SelectedEntity);
		if (copy != entt::null)
			m_Context->Select(copy);
	}

	HierarchyWindow::HierarchyWindow() = default;

	void HierarchyWindow::SetContext(EditorContext* context)
	{
		m_Context = context;
	}

	void HierarchyWindow::HandleKeyboardShortcuts()
	{
		if (ImGui::IsKeyPressed(ImGuiKey_Delete))
			DeleteSelected();

		if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D))
			DuplicateSelected();
	}

	void HierarchyWindow::DrawContextMenuEmpty()
	{
		if (ImGui::BeginMenu("Criar Objeto"))
		{
			if (ImGui::MenuItem("Objeto Vazio"))
			{
				CreateObject("Objeto Vazio", "");
				CreateObject("Cube", PrimitiveUUID::Cube);
			}

			ImGui::Separator();

			if (ImGui::BeginMenu("Primitivas"))
			{
				if (ImGui::MenuItem("Cubo"))
					CreateObject("Cube", PrimitiveUUID::Cube);

				if (ImGui::MenuItem("Esfera"))
					CreateObject("Sphere", PrimitiveUUID::Sphere);

				if (ImGui::MenuItem("Plano"))
					CreateObject("Plane", PrimitiveUUID::Plane);

				if (ImGui::MenuItem("Cilindro"))
					CreateObject("Cylinder", PrimitiveUUID::Cylinder);

				ImGui::EndMenu();
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Luz Direcional"))
				CreateLight();

			ImGui::EndMenu();
		}
	}
} // namespace axe