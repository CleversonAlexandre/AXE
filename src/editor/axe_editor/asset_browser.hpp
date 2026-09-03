#pragma once
#include "axe/audio/audio_device.hpp"   // VoiceHandle
#include <algorithm>
#include "axe/core/types.hpp"
#include "editor_context.hpp"
#include "editor_icon_library.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/asset/asset.hpp"
#include "axe/graphics/texture.hpp"
#include "material_thumbnail_renderer.hpp"
#include "mesh_thumbnail_renderer.hpp"
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
        using ScriptOpenCallback = std::function<void(const std::string& uuid)>;
        using MaterialDropCallback = std::function<void(const std::string& uuid)>;

        // Arquivo renomeado: editores abertos precisam saber (o caminho que
        // eles seguram acabou de deixar de existir).
        using AssetRenamedCallback = std::function<void(
            const std::filesystem::path& oldPath,
            const std::filesystem::path& newPath,
            const std::string& newName)>;



        AssetBrowser() = default;

        void SetContext(EditorContext* context) { m_Context = context; }
        void SetFileDropCallback(FileDropCallback cb) { m_FileDropCallback = cb; }
        void SetInstantiateCallback(InstantiateCallback cb) { m_InstantiateCallback = cb; }
        void SetAssetOpenCallback(AssetOpenCallback cb) { m_AssetOpenCallback = cb; }
        void SetScriptOpenCallback(ScriptOpenCallback cb) { m_OnOpenScript = cb; }
        void SetMaterialDropCallback(MaterialDropCallback cb) { m_MaterialDropCallback = cb; }
        void SetAssetRenamedCallback(AssetRenamedCallback cb) { m_AssetRenamedCallback = cb; }

        // ── BATCH_SHADING_MODEL_V1 ───────────────────────────────────────────
        //
        // Troca o Shading Model de VARIOS materiais de uma vez, recompilando e
        // recozinhando cada um. Devolve quantos mudaram de fato.
        //
        // CALLBACK, e nao chamada direta: o trabalho precisa do MaterialCompiler
        // e do MaterialShaderCache, e o Asset Browser e painel de UI — ele lista
        // e seleciona arquivos, nao compila shader. Mesmo desenho de
        // SetAssetOpenCallback e dos outros acima; quem liga os dois e o
        // EditorLayer.
        using BatchShadingModelCallback =
            std::function<int(const std::vector<std::string>& materialUUIDs,
                int shadingModel)>;
        void SetBatchShadingModelCallback(BatchShadingModelCallback cb)
        {
            m_BatchShadingModelCallback = cb;
        }
        void SetThumbnailRenderer(MaterialThumbnailRenderer* r) { m_ThumbnailRenderer = r; }
        void SetMeshThumbnailRenderer(MeshThumbnailRenderer* r) { m_MeshThumbnails = r; }

        void OnFileDrop(const std::string& filepath);

        // Navega ate o asset e o seleciona — o "Browse to Asset" da Unreal.
        // Usado pelo botao Find do Animation Editor (notify -> asset).
        void RevealAsset(const std::string& uuid);
        void Draw();
        void Update();
        void SaveFolders(const std::filesystem::path& projectRoot);
        void LoadFolders(const std::filesystem::path& projectRoot);
        void SaveIfProject();

        // Compat
        void DrawContextMenuEmpty();

        // ── SC41: quais FBX sao ANIMACAO ─────────────────────────────────
        //
        // Para o AssetDatabase, o FBX do 'Running' e um Mesh igual a qualquer
        // outro — quem sabe que ele e uma animacao e o .axeskel que o
        // referencia. A faixa verde na miniatura depende desse cruzamento.
        //
        // Derruba o cache. Chame depois de importar, mover ou apagar um
        // .axeskel (o proprio browser ja chama nos seus caminhos; o Animation
        // Editor pode chamar ao gravar uma entrada nova).
        void InvalidateAnimationSources() { m_AnimSourcesStamp = (std::size_t)-1; }

        MaterialThumbnailRenderer* m_ThumbnailRenderer = nullptr;
        MeshThumbnailRenderer* m_MeshThumbnails = nullptr;

    private:
        // Desenho
        void DrawToolbar();
        void DrawFolderTree();
        void DrawFolderNode(const std::string& folderPath);
        void DrawAssetGrid();

        // Botao de tocar sobreposto ao icone de um asset de audio.
        //
        // Desenhado sobre a miniatura, no hover ou na selecao — do jeito que
        // a Unreal faz. E o gesto certo porque a pergunta "como e este som?"
        // nasce olhando pro icone: qualquer painel separado obriga o olho a
        // ir e voltar.
        //
        // Devolve true se ele consumiu o clique, para que o item nao trate o
        // mesmo clique como abertura.
        // Toca ou para o preview deste asset.
        void TogglePreview(const AssetRecord& record);

        bool DrawAudioPlayOverlay(const AssetRecord& record,
            const ImVec2& iconMin, const ImVec2& iconMax,
            bool hovered, bool selected);
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

        // Relocate Assets — move assets registrados que estão FORA da pasta
        // Assets do projeto (importados antes da correção do OnFileDrop, ou
        // arquivos manualmente referenciados de outro lugar) para dentro dela.
        void DrawRelocateAssetsModals();
        void ScanExternalAssets(std::vector<std::string>& outUUIDs) const;
        void RelocateAssets(const std::vector<std::string>& uuids);

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
        ScriptOpenCallback  m_OnOpenScript;
        MaterialDropCallback m_MaterialDropCallback;
        AssetRenamedCallback m_AssetRenamedCallback;
        BatchShadingModelCallback m_BatchShadingModelCallback;   // BATCH_SHADING_MODEL_V1

        std::string m_SelectedFolder = "";   // path completo da pasta selecionada
        float       m_IconSize = 64.0f;
        float       m_FolderPanelWidth = 200.0f; // largura do painel de pastas — redimensionável com o mouse

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

        // ── SC41: cache de "este FBX e uma animacao" ─────────────────────
        //
        // Reconstruido por VARREDURA dos .axeskel, que sao poucos (um por
        // personagem) e cujo LoadFromFile so le JSON — o FBX so entra no
        // Resolve(), que nao acontece aqui.
        //
        // O carimbo e o tamanho do banco: barato, e o unico evento que
        // importa e "apareceu/sumiu asset". Renomear em cima nao muda o
        // tamanho — por isso os caminhos de import/rename/delete chamam
        // InvalidateAnimationSources() explicitamente, em vez de confiar so
        // no carimbo.
        std::unordered_set<std::string> m_AnimationSourceUUIDs;
        std::size_t m_AnimSourcesStamp = (std::size_t)-1;

        // Sigla curta desenhada sobre o icone dos tipos que COMPARTILHAM
        // desenho. nullptr para quem ja tem icone proprio. Ver o ponto de uso.
        static const char* AssetTypeTag(AssetType type);

        void EnsureAnimationSources();
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

        // Selecao MULTIPLA, para arrastar varios assets de uma vez.
        //
        // Vive ao lado de m_SelectedUUID em vez de substitui-lo: rename,
        // delete, "Move to" e o menu de contexto continuam operando sobre UM
        // asset, e reescrever todos eles seria um patch por si so. Aqui a
        // lista serve so ao arrasto e ao realce.
        std::vector<std::string> m_SelectedUUIDs;

        // Voice do preview. Uma so: ouvir dois sons ao mesmo tempo aqui nao
        // ajuda a comparar nada.
        VoiceHandle m_PreviewVoice = 0;
        std::string m_PreviewUUID;

        bool IsSelected(const std::string& uuid) const
        {
            return uuid == m_SelectedUUID
                || std::find(m_SelectedUUIDs.begin(), m_SelectedUUIDs.end(), uuid)
                != m_SelectedUUIDs.end();
        }

        // Popup de cor de pasta
        std::string m_ColorPickerFolder = "";
        float       m_PickerColor[4] = { 1,1,1,1 };

        // Confirmação de exclusão
        std::string m_DeleteConfirmUUID = "";
        std::string m_DeleteConfirmFolder = "";
        std::string m_DeleteConfirmFolderDiskPath = "";

        // ── BATCH_SHADING_MODEL_V1 — confirmacao antes de recompilar ─────────
        //
        // Modal, e nao aplicacao direta do menu, porque a acao RECOMPILA cada
        // material — dezenas de compilacoes de GLSL numa tacada. E lenta o
        // bastante para travar a janela por um instante, e destrutiva o
        // bastante (regrava o .axegraph e o .axeshader) para merecer um passo
        // a mais. Mesmo fluxo do Relocate Assets logo abaixo.
        std::vector<std::string> m_PendingShadingUUIDs;
        int  m_PendingShadingModel = 0;
        bool m_ShadingConfirmOpen = false;
        int  m_ShadingResultCount = -1;   // -1 = sem resultado a mostrar

        // Coleta os materiais alvo: a selecao (multipla, se houver) ou a
        // subarvore de uma pasta.
        std::vector<std::string> CollectMaterialsInSelection() const;
        std::vector<std::string> CollectMaterialsInFolder(const std::string& folderPath) const;
        void DrawShadingModelMenu(const std::vector<std::string>& targets);
        void DrawShadingModelModals();

        // Relocate Assets — fluxo de confirmação/resultado
        bool                     m_RelocateConfirmOpen = false;
        bool                     m_RelocateResultOpen = false;
        std::vector<std::string> m_PendingRelocateUUIDs;
        int                      m_RelocateSuccessCount = 0;
        std::vector<std::string> m_RelocateErrorMessages;
    };

} // namespace axe