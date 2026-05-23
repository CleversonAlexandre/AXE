#pragma once
#include "axe/core/types.hpp"
#include "axe/core/command_history.hpp"
#include "axe/material/material_asset.hpp"
#include <memory>
#include <string>
#include <imgui-node-editor/imgui_node_editor.h>

#include "node_graph/material_graph.hpp"

//#include "node_types.hpp"

#include <imgui/imgui_internal.h>
#include "axe/material/material.hpp"

#include "hierarchy_window.hpp"

#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/editor_camera.hpp"
#include "axe/graphics/renderer/viewport_renderer.hpp"
#include <imgui.h>
#include <entt/entt.hpp>

#include "axe/scene/scene_environment.hpp" 
#include "editor_context.hpp"
#include "material_thumbnail_renderer.hpp"


namespace ed = ax::NodeEditor;

namespace axe
{
    class MaterialGraph;

    class MaterialEditorWindow 
    {
    public:
        MaterialEditorWindow();
        ~MaterialEditorWindow();

        struct ShaderLogEntry
        {
            enum class Level {Info, Warnning, Error};
            Level level;
            std::string message;
        };

        void LogInfo(const std::string& msg);
        void LogWarning(const std::string& msg);
        void LogError(const std::string& msg);
        void ClearLog();
        void DrawShaderLog();

        std::vector<ShaderLogEntry> m_ShaderLog;

        void Draw();

        // Abre um material para edição
        void OpenMaterial(std::shared_ptr<MaterialAsset> asset);        
        bool IsOpen() const { return m_Open; }
        void Initialize();

        ImColor GetIconColor(PinType type);
        void DrawPinIcon(const Pin& pin, bool connected, int alpha);
        bool CanCreateLink(Pin* a, Pin* b);
        
        void UpdatePreviewCamera();

        void RenderPreview();
        void HandleInput();
        void SaveGraph();
        void LoadGraph();
        

        void SetContext(EditorContext* context) { m_Context = context; }

        bool IsFocused() const { return m_IsAnyWindowFocused; }

        void SetThumbnailRenderer(MaterialThumbnailRenderer* r) { m_ThumbnailRenderer = r; }
        MaterialThumbnailRenderer* m_ThumbnailRenderer = nullptr;
    private:
        std::unique_ptr<Scene> m_PreviewScene;
        std::unique_ptr<ViewportRenderer> m_PreviewRenderer;
        std::unique_ptr<EditorCamera> m_PreviewCamera;
        std::shared_ptr<Framebuffer> m_PreviewFramebuffer;
        std::shared_ptr<Mesh> m_PreviewMesh;
        entt::entity m_PreviewEntity;

        // Skybox
        std::unique_ptr<SkyboxRenderer> m_SkyboxRenderer;
        std::shared_ptr<CubemapTexture> m_PreviewSkybox;
        
        float m_PreviewRotation = 0.0f;

        //void DrawPreview();
        void InitializePreview();

        // Preview window state        
        ImVec2 m_PreviewSize = ImVec2(512, 512);
        ImVec2 m_PreviewBoundsMin;
        ImVec2 m_PreviewBoundsMax;
        ImVec2 m_PreviewMouseDelta;
        bool m_PreviewHovered = false;
        bool m_PreviewFocused = false;

        std::unique_ptr<SceneEnvironment> m_PreviewEnvironment;

        void DrawPreviewWindow();  
        void HandlePreviewInput();

        EditorContext* m_Context = nullptr;
        ed::EditorContext* m_NodeEditorContext = nullptr;

        void DrawCommentNode(Node* node);
        void DrawMaterialParams(Material& mat);
        void DrawTextureSlot(const char* label,
            std::shared_ptr<Texture2D>& tex,
            std::string& uuid);


        ImRect ImGui_GetItemRect();
        ImRect ImRect_Expanded(const ImRect& rect, float x, float y);
        ImRect bounds;

        std::shared_ptr<MaterialAsset> m_Asset;
        bool m_Open = false;

        void DrawNodeGraph();
        void DrawNode(Node& node);
        
        void CompileAndApply();

        void DrawMaterialParamsWindow();
        void DrawNodeGraphWindow();

        ImColor GetPinColor(PinType type) const;


        std::unique_ptr<MaterialGraph> m_Graph;


        bool m_IsAnyWindowFocused = false;
        bool m_FirstFrame = true;
        int m_FrameCount = 0;
        int            m_PinIconSize = 24;
        bool createNewNode = false;
        Pin* newNodeLinkPin = nullptr;    
        Node* m_EditingCommentNode = nullptr;
        std::string m_CommentEditBuffer;
        ImVec2 m_CommentEditPopupPos;
        void UpdateCommentChildren(Node* commentNode);
        std::shared_ptr<Material> m_Material;
        std::shared_ptr<Material> m_PreviewMaterial;

        ViewportRenderer* m_ExternalRenderer = nullptr;

        void DeleteNodeWithHistory(ed::NodeId nodeId);

        CommandHistory m_History;
        
        std::unordered_map<int, Link> m_OriginalPinLinks;
    };

} // namespace axe