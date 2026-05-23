#pragma once
#include "axe/core/types.hpp"
#include "editor_context.hpp"
#include "editor_icon_library.hpp"
#include "axe/asset/asset_database.hpp"
#include <string>
#include <vector>
#include <functional>

#include "axe/asset/asset.hpp"
#include "axe/graphics/texture.hpp"
#include <unordered_map>

#include <unordered_set>
#include "material_thumbnail_renderer.hpp"

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
		void DrawContextMenuEmpty();
		void Update();

		using AssetOpenCallback = std::function<void(const AssetRecord&)>;
		void SetAssetOpenCallback(AssetOpenCallback cb) { m_AssetOpenCallback = cb; }

		using MaterialDropCallback = std::function<void(const std::string& uuid)>;
		void SetMaterialDropCallback(MaterialDropCallback cb) { m_MaterialDropCallback = cb; }

		//Thumbnail para o material
		void SetThumbnailRenderer(MaterialThumbnailRenderer* renderer)
		{
			m_ThumbnailRenderer = renderer;
		}

		MaterialThumbnailRenderer* m_ThumbnailRenderer = nullptr;

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
		std::unordered_map<std::string, std::shared_ptr<Texture2D>> m_TextureCache;
		AssetOpenCallback m_AssetOpenCallback;

		std::unordered_set<std::string> m_TexturesPendingLoad;  
		std::unordered_set<std::string> m_TexturesFailedLoad;
		int m_FramesSinceStart = 0;
		
		MaterialDropCallback m_MaterialDropCallback;
	};

} // namespace axe