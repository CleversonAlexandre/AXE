#include "asset_browser.hpp"
#include "axe/log/log.hpp"
#include "axe/asset/asset_database.hpp"
#include <imgui.h>
#include <filesystem>

#include "axe/project/project_manager.hpp"
#include "axe/material/material_asset.hpp"
namespace axe
{

	static const std::vector<std::string> s_SupportedExtensions = {
		".gltf", ".glb", ".obj", //Meshes
		".png", ".jpg", ".jpeg", //Texturas
		".axemat"
	};

	bool AssetBrowser::IsSupported(const std::string& filepath) const
	{
		std::string ext = std::filesystem::path(filepath).extension().string();
		for (char& c : ext) c = (char)std::tolower(c);
		for (const auto& s : s_SupportedExtensions)
			if (ext == s) return true;
		return false;
	}

	void AssetBrowser::OnFileDrop(const std::string& filepath)
	{
		if (!IsSupported(filepath))
		{
			AXE_CORE_WARN("AssetBrowser: formato não suportado '{}'", filepath);
			return;
		}

		// Registra no AssetDatabase independente do tipo
		std::string uuid = AssetDatabase::Get().Register(filepath);

		if (ProjectManager::Get().HasProject())
			AssetDatabase::Get().Save(ProjectManager::Get().GetCurrent().RootPath);

		std::string name = std::filesystem::path(filepath).stem().string();
		AXE_CORE_INFO("AssetBrowser: '{}' registrado. UUID: {}", name, uuid);

		// Só instancia na cena se for mesh
		std::string ext = std::filesystem::path(filepath).extension().string();
		for (char& c : ext) c = (char)std::tolower(c);
		AssetType type = AssetTypeFromExtension(ext);

		if (type == AssetType::Mesh && m_FileDropCallback)
			m_FileDropCallback(filepath);
	}

	void AssetBrowser::Update()
	{
		m_FramesSinceStart++;

		// Só começa a carregar texturas após 3 frames
		if (m_FramesSinceStart < 3)
			return;

		// Carrega uma textura pendente por frame
		if (!m_TexturesPendingLoad.empty())
		{
			AXE_CORE_INFO("AssetBrowser: Carregando texture pendente...");

			auto uuid = *m_TexturesPendingLoad.begin();
			m_TexturesPendingLoad.erase(m_TexturesPendingLoad.begin());

			auto& db = AssetDatabase::Get();
			const AssetRecord* record = db.GetByUUID(uuid);

			if (record && std::filesystem::exists(record->FilePath))
			{
				try
				{
					auto tex = Texture2D::Create(record->FilePath.string());

					if (tex && tex->IsLoaded())
					{
						m_TextureCache[uuid] = tex;
						AXE_CORE_INFO("AssetBrowser: Textura carregada com sucesso!");
					}
					else
					{
						m_TexturesFailedLoad.insert(uuid);
						AXE_CORE_WARN("AssetBrowser: Falha ao carregar textura");
					}
				}
				catch (const std::exception& e)
				{
					AXE_CORE_ERROR("AssetBrowser: falha ao carregar '{}': {}",
						record->FilePath.string(), e.what());
					m_TexturesFailedLoad.insert(uuid);
				}
			}
			else
			{
				m_TexturesFailedLoad.insert(uuid);
			}
		}
	}

	void AssetBrowser::Draw()
	{
		if (!ImGui::Begin("Asset Browser"))
		{
			ImGui::End();
			return;
		}

		// Barra superior — slider de tamanho dos ícones
		ImGui::SliderFloat("##iconsize", &m_IconSize, 32.0f, 128.0f, "%.0f");
		ImGui::SameLine();
		ImGui::TextDisabled("Tamanho");
		ImGui::Separator();

		// Dois painéis
		float leftWidth = 160.0f;

		// Painel esquerdo — árvore de pastas
		ImGui::BeginChild("##folders", ImVec2(leftWidth, 0), true);
		DrawFolderTree();
		ImGui::EndChild();

		ImGui::SameLine();

		// Painel direito — grid de assets
		ImGui::BeginChild("##assets", ImVec2(0, 0), true);
		DrawAssetGrid();
		if (ImGui::BeginPopupContextWindow("##assetbrowser_empty",
			ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			DrawContextMenuEmpty();
			ImGui::EndPopup();
		}
		ImGui::EndChild();

		ImGui::End();
	}
	void AssetBrowser::DrawContextMenuEmpty()
	{
		if (ImGui::BeginMenu("Novo Material"))
		{
			if (ImGui::MenuItem("Material"))
			{
				// Cria .axemat na pasta de materiais do projeto
				auto matPath = ProjectManager::Get().GetCurrent().AssetsPath
					/ "Materials" / "NewMaterial.axemat";

				// Garante nome único
				int i = 1;
				while (std::filesystem::exists(matPath))
				{
					matPath = ProjectManager::Get().GetCurrent().AssetsPath
						/ "Materials" / ("NewMaterial_" + std::to_string(i++) + ".axemat");
				}

				auto matAsset = MaterialAsset::Create(matPath.stem().string());
				matAsset->Save(matPath);

				// Registra no AssetDatabase
				AssetDatabase::Get().Register(matPath.string());
				if (ProjectManager::Get().HasProject())
					AssetDatabase::Get().Save(ProjectManager::Get().GetCurrent().RootPath);

				AXE_CORE_INFO("AssetBrowser: material '{}' criado.", matPath.string());
			}

			ImGui::EndMenu();
		}
	}

	void AssetBrowser::DrawFolderTree()
	{
		ImGui::Text("Assets");
		ImGui::Separator();

		// Pasta raiz
		bool rootSelected = m_SelectedFolder.empty();
		if (rootSelected)
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));

		if (ImGui::Selectable("/ Todos", rootSelected))
			m_SelectedFolder = "";

		if (rootSelected)
			ImGui::PopStyleColor();

		ImGui::Separator();

		// Pastas por tipo
		struct FolderItem { std::string Name; std::string Filter; };
		std::vector<FolderItem> folders = {
			{ "Meshes",   "Mesh"    },
			{ "Textures", "Texture" },
			{ "Scenes",   "Scene"   },
			{ "Scripts",  "Script"  },
			{ "Audio",    "Audio"   },
			{ "Materials", "Material"   }
		};

		auto& icons = EditorIconLibrary::Get();

		for (const auto& folder : folders)
		{
			bool selected = (m_SelectedFolder == folder.Filter);

			// Ícone de pasta
			if (icons.GetFolder() && icons.GetFolder()->IsLoaded())
			{
				ImGui::Image(
					(ImTextureID)(uintptr_t)icons.GetFolder()->GetRendererID(),
					ImVec2(16, 16),
					ImVec2(0, 1),  
					ImVec2(1, 0)
				);
				ImGui::SameLine();
			}

			if (ImGui::Selectable(folder.Name.c_str(), selected))
				m_SelectedFolder = folder.Filter;
		}
	}

	void AssetBrowser::DrawAssetGrid()
	{
		auto& db = AssetDatabase::Get();
		auto& records = db.GetAll();

		// Filtra por tipo se pasta selecionada
		std::vector<const AssetRecord*> filtered;
		for (const auto& [uuid, record] : records)
		{
			if (m_SelectedFolder.empty())
			{
				filtered.push_back(&record);
			}
			else
			{
				if (AssetTypeToString(record.Type) == m_SelectedFolder)
					filtered.push_back(&record);
			}
		}

		if (filtered.empty())
		{
			ImGui::TextDisabled("Nenhum asset encontrado.");
			ImGui::TextDisabled("Arraste um arquivo para importar.");
			return;
		}

		// Grid
		float panelWidth = ImGui::GetContentRegionAvail().x;
		float cellSize = m_IconSize + 16.0f;
		int   columns = std::max(1, (int)(panelWidth / cellSize));

		ImGui::Columns(columns, nullptr, false);

		for (const auto* record : filtered)
			DrawAssetItem(*record);

		ImGui::Columns(1);
	}

	

	void AssetBrowser::DrawAssetItem(const AssetRecord& record)
	{
		auto& icons = EditorIconLibrary::Get();		

		std::shared_ptr<Texture2D> icon;

		uint32_t overrideTextureID = 0;

		if (record.Type == AssetType::Material && m_ThumbnailRenderer &&
			record.FilePath.extension() == ".axemat")
		{
			m_ThumbnailRenderer->Register(record.UUID, record.FilePath);
			overrideTextureID = m_ThumbnailRenderer->GetThumbnail(record.UUID);
			icon = icons.GetMaterial(); // fallback enquanto thumbnail não está pronto
		}
		else if (record.Type == AssetType::Texture)
		{
			if (m_TexturesFailedLoad.count(record.UUID) > 0)
				icon = icons.GetForType("Texture");
			else if (m_TextureCache.count(record.UUID) > 0)
			{
				icon = m_TextureCache[record.UUID];
				if (!icon) icon = icons.GetForType("Texture");
			}
			else if (m_TexturesPendingLoad.count(record.UUID) > 0)
				icon = icons.GetForType("Texture");
			else
			{
				if (std::filesystem::exists(record.FilePath))
					m_TexturesPendingLoad.insert(record.UUID);
				icon = icons.GetForType("Texture");
			}
		}
		else
		{
			switch (record.Type)
			{
			case AssetType::Scene:    icon = icons.GetScene();    break;
			case AssetType::Script:   icon = icons.GetScript();   break;
			case AssetType::Audio:    icon = icons.GetAudio();    break;
			default:                  icon = icons.GetMesh();     break;
			}
		}

		ImGui::PushID(record.UUID.c_str());


		ImVec2 itemPos = ImGui::GetCursorScreenPos();
		float  padding = 6.0f;
		float  totalW = m_IconSize + padding * 2.0f;
		float  totalH = totalW + 20.0f;

		// UM único botão invisível — captura hover, clique e drag
		ImGui::InvisibleButton("##item", ImVec2(totalW, totalH));

		bool hovered = ImGui::IsItemHovered();
		bool dclicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

		

		// Drag and drop — deve vir logo após o InvisibleButton
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
		{
			ImGui::SetDragDropPayload("ASSET_UUID", record.UUID.c_str(), record.UUID.size() + 1);

			if (icon && icon->IsLoaded())
				ImGui::Image(
					(ImTextureID)(uintptr_t)icon->GetRendererID(),
					ImVec2(32, 32),
					ImVec2(0, 1), ImVec2(1, 0)
				);

			ImGui::Text("%s", record.Name.c_str());
			ImGui::EndDragDropSource();
		}

		// Duplo clique
		if (dclicked && m_InstantiateCallback)
			m_InstantiateCallback(record.UUID);

		// Tooltip
		if (hovered)
			ImGui::SetTooltip("%s\n%s", record.Name.c_str(), record.FilePath.string().c_str());

		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			if (m_AssetOpenCallback)
				m_AssetOpenCallback(record);
		}

		// Desenha por cima com DrawList
		ImDrawList* draw = ImGui::GetWindowDrawList();

		ImVec2 rectMin = itemPos;
		ImVec2 rectMax = ImVec2(itemPos.x + totalW, itemPos.y + totalH);

		// Fundo hover
		if (hovered)
			draw->AddRectFilled(rectMin, rectMax, IM_COL32(60, 120, 200, 60), 6.0f);

		// Borda do ícone
		ImVec2 iconMin = ImVec2(itemPos.x + padding, itemPos.y + padding);
		ImVec2 iconMax = ImVec2(iconMin.x + m_IconSize, iconMin.y + m_IconSize);

		draw->AddRect(
			iconMin, iconMax,
			hovered ? IM_COL32(80, 150, 255, 200) : IM_COL32(80, 80, 80, 120),
			4.0f, 0, 1.5f
		);

		// Ícone
		if (overrideTextureID != 0)
			draw->AddImage(
				(ImTextureID)(uintptr_t)overrideTextureID,
				iconMin, iconMax,
				ImVec2(0, 1), ImVec2(1, 0));
		else if (icon && icon->IsLoaded())
			draw->AddImage(
				(ImTextureID)(uintptr_t)icon->GetRendererID(),
				iconMin, iconMax,
				ImVec2(0, 1), ImVec2(1, 0));
		else
			draw->AddRectFilled(iconMin, iconMax, IM_COL32(60, 60, 60, 255), 4.0f);			

		// Nome centralizado
		std::string displayName = record.Name;
		if (displayName.size() > 10)
			displayName = displayName.substr(0, 9) + "...";

		float textWidth = ImGui::CalcTextSize(displayName.c_str()).x;
		float textX = itemPos.x + (totalW - textWidth) * 0.5f;
		float textY = itemPos.y + totalH - 18.0f;

		draw->AddText(
			ImVec2(textX, textY),
			hovered ? IM_COL32(140, 180, 255, 255) : IM_COL32(200, 200, 200, 255),
			displayName.c_str()
		);

		

		ImGui::NextColumn();
		ImGui::PopID();
	}

} // namespace axe