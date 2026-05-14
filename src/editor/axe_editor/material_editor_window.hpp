#pragma once
#include "axe/core/types.hpp"
#include "axe/material/material_asset.hpp"
#include <memory>
#include <string>
#include <imgui-node-editor/imgui_node_editor.h>

#include "node_graph/material_graph.hpp"
#include <imgui/imgui_internal.h>

namespace ed = ax::NodeEditor;

namespace axe
{

    class MaterialEditorWindow 
    {
    public:
        MaterialEditorWindow();
        ~MaterialEditorWindow();

        void Draw();

        // Abre um material para edição
        void OpenMaterial(std::shared_ptr<MaterialAsset> asset);        
        bool IsOpen() const { return m_Open; }


        ImColor GetIconColor(PinType type);
        void DrawPinIcon(const Pin& pin, bool connected, int alpha);
        bool CanCreateLink(Pin* a, Pin* b);
        
    private:
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
        //bool CanCreateLink(Pin* a, Pin* b);



        ImColor GetPinColor(PinType type) const;


        std::unique_ptr<MaterialGraph> m_Graph;
        
        bool m_FirstFrame = true;
        int m_FrameCount = 0;
        int            m_PinIconSize = 24;
        bool createNewNode = false;
        Pin* newNodeLinkPin = nullptr;    
        Node* m_EditingCommentNode = nullptr;
        std::string m_CommentEditBuffer;
        ImVec2 m_CommentEditPopupPos;
        void UpdateCommentChildren(Node* commentNode);

    };

} // namespace axe