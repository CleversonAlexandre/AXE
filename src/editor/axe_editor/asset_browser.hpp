#pragma once
#include "axe/core/types.hpp"
#include "editor_context.hpp"
#include "editor_icon_library.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/asset/asset.hpp"
#include "axe/graphics/texture.hpp"
#include "material_thumbnail_renderer.hpp"
#include "file_dialog.hpp"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <optional>

namespace axe
{
    class AssetBrowser
    {
    public:
        using FileDropCallback = std::function<void(const std::string&)>;
        using InstantiateCallback = std::function<void(const std::string&)>;
        using AssetOpenCallback = std::function<void(const AssetRecord&)>;
        using MaterialDropCallback = std::function<void(const std::string& uuid)>;

        AssetBrowser() = default;

        void SetContext(EditorContext* context) { m_Context = context; }
        void SetFileDropCallback(FileDropCallback cb) { m_FileDropCallback = cb; }
        void SetInstantiateCallback(InstantiateCallback cb) { m_InstantiateCallback = cb; }
        void SetAssetOpenCallback(AssetOpenCallback cb) { m_AssetOpenCallback = cb; }
        void SetMaterialDropCallback(MaterialDropCallback cb) { m_MaterialDropCallback = cb; }
        void SetThumbnailRenderer(MaterialThumbnailRenderer* r) { m_ThumbnailRenderer = r; }

        void OnFileDrop(const std::string& filepath);
        void Draw();
        void Update();
        void SaveFolders(const std::filesystem::path& projectRoot);
        void LoadFolders(const std::filesystem::path& projectRoot);
        void SaveIfProject();

        // Compat
        void DrawContextMenuEmpty();

        MaterialThumbnailRenderer* m_ThumbnailRenderer = nullptr;

    private:
        // Desenho
        void DrawToolbar();
        void DrawFolderTree();
        void DrawFolderNode(const std::string& folderPath, int depth = 0);
        void DrawAssetGrid();
        void DrawAssetItem(const AssetRecord& record);
        void DrawFolderItem(const VirtualFolderDef& folder);

        // Context menus
        void DrawAssetContextMenu(const AssetRecord& record);
        void DrawFolderContextMenu(const std::string& folderPath);
        void DrawEmptyAreaContextMenu();

        // Operações de arquivo
        void DeleteAsset(const AssetRecord& record);
        void RenameAsset(const AssetRecord& record, const std::string& newName);
        void DuplicateAsset(const AssetRecord& record);
        void OpenInExplorer(const std::filesystem::path& path);
        void MoveAssetToFolder(const std::string& uuid, const std::string& folder);

        // Pastas virtuais
        void CreateFolder(const std::string& name, const std::string& parent = "");
        void DeleteFolder(const std::string& folderPath);
        void RenameFolder(const std::string& oldPath, const std::string& newName);
        std::vector<VirtualFolderDef*> GetSubfolders(const std::string& parent);
        std::string GetFullFolderPath(const std::string& name, const std::string& parent);

        // Utilitários
        bool IsSupported(const std::string& filepath) const;
        std::string GetCurrentFolderFilter() const;

        // Estado
        EditorContext* m_Context = nullptr;
        FileDropCallback    m_FileDropCallback;
        InstantiateCallback m_InstantiateCallback;
        AssetOpenCallback   m_AssetOpenCallback;
        MaterialDropCallback m_MaterialDropCallback;

        std::string m_SelectedFolder = "";   // path completo da pasta selecionada
        float       m_IconSize = 64.0f;

        // Pastas virtuais — path completo (ex: "Meshes/Characters") → def
        std::vector<VirtualFolderDef> m_Folders = {
            { "Meshes",    "", 0xFF4A9EFF },
            { "Textures",  "", 0xFF4AFF7A },
            { "Scenes",    "", 0xFFFF9E4A },
            { "Scripts",   "", 0xFFFF4A9E },
            { "Audio",     "", 0xFF9E4AFF },
            { "Materials", "", 0xFF4AFFFF },
        };

        // Cache de texturas
        std::unordered_map<std::string, std::shared_ptr<Texture2D>> m_TextureCache;
        std::unordered_set<std::string> m_TexturesPendingLoad;
        std::unordered_set<std::string> m_TexturesFailedLoad;
        int m_FramesSinceStart = 0;

        // Rename inline
        std::string m_RenamingUUID = "";
        std::string m_RenamingFolder = "";
        bool        m_RenameFocusNeeded = false;
        char        m_RenameBuffer[256] = {};

        // Pesquisa
        char        m_SearchBuffer[256] = {};

        // Seleção
        std::string m_SelectedUUID = "";

        // Popup de cor de pasta
        std::string m_ColorPickerFolder = "";
        float       m_PickerColor[4] = { 1,1,1,1 };

        // Confirmação de exclusão
        std::string m_DeleteConfirmUUID = "";
        std::string m_DeleteConfirmFolder = "";
        std::string m_DeleteConfirmFolderDiskPath = "";
    };

} // namespace axe