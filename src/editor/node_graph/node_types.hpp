#pragma once
#include <imgui_node_editor.h>
#include <string>
#include <memory>
#include <vector>
#include <functional>

#include "axe/utils/glm_config.hpp"
#include "axe/graphics/texture.hpp"
#include <imgui.h>

namespace ed = ax::NodeEditor;

namespace axe
{

    enum class PinType
    {
        Float,
        Vec3,
        Vec4,
        Texture2D,
        Any
    };

    enum class NodeType
    {
        Blueprint,
        Simple,
        Tree,
        Comment,
        Houdini
    };

    //enum class PinKind
    //{
    //    Output,
    //    Input
    //};

    struct Node;

    struct Pin
    {
        ed::PinId   ID;
        ::axe::Node* ParentNode;
        std::string Name;
        PinType     Type;
        ed::PinKind     Kind; // Input ou Output

        Pin(int id, const char* name, PinType type, ed::PinKind kind)
            : ID(id), ParentNode(nullptr), Name(name), Type(type),Kind(kind){}
    };

    struct Link
    {
        ed::LinkId ID;
        ed::PinId  StartPin;
        ed::PinId  EndPin;

        ImColor Color;


        Link(int id, ed::PinId start, ed::PinId end)
            : ID(id), StartPin(start), EndPin(end), Color(255, 255, 255) {}
    };

    // Valor que um pin pode carregar
    struct PinValue
    {
        PinType Type = PinType::Float;

        float               FloatVal = 0.0f;
        glm::vec3           Vec3Val = { 0, 0, 0 };
        glm::vec4           Vec4Val = { 0, 0, 0, 1 };
        std::shared_ptr<axe::Texture2D> TextureVal;
        std::string         TextureUUID;
    };

    // Node base
    struct Node
    {
        ed::NodeId          ID;
        std::string         Name;        
        std::vector<Pin>    Inputs;
        std::vector<Pin>    Outputs;
        ImVec4              Color = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);        
        NodeType Type;
        ImVec2 Size;
        // Dados específicos do node
        PinValue            Value; // usado por nodes simples (Color, Float)

        float TitleHeight = 0.0f;
        std::vector<int> ChildNodeIDs;


        Node(int id, const char* name, ImColor color = ImColor(255, 255, 255)) :
            ID(id), Name(name), Color(color), Type(NodeType::Blueprint), Size(0, 0)
        {}

        virtual ~Node() = default;
    };

  
    


} // namespace axe