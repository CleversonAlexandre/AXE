#pragma once
#include "axe/core/types.hpp"
#include "editor_context.hpp"
#include "editor_icon_library.hpp"
#include "axe/asset/asset_database.hpp"
#include <string>
#include <vector>
#include <functional>

namespace axe
{

	class AssetBrowser
	{
	public:
		using FileDropCallback = std::function<void(const std::string&)>;
		using InstantiateCallback = std::function<void(const std::string&)>; // UUID

		AssetBrowser() = default;

		void SetContext(EditorContext* context) { m_Context = context; }
		void SetFileDropCallback(FileDropCallback cb) { m_FileDropCallback = cb; }
		void SetInstantiateCallback(InstantiateCallback cb) { m_InstantiateCallback = cb; }

		void OnFileDrop(const std::string& filepath);
		void Draw();

	private:
		void DrawFolderTree();
		void DrawAssetGrid();
		void DrawAssetItem(const AssetRecord& record);

		bool IsSupported(const std::string& filepath) const;

		EditorContext* m_Context = nullptr;
		FileDropCallback    m_FileDropCallback;
		InstantiateCallback m_InstantiateCallback;

		// Pasta virtual selecionada — "" = raiz
		std::string m_SelectedFolder = "";

		// Tamanho dos ícones no grid
		float m_IconSize = 64.0f;
	};

} // namespace axe