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
        Vec2,
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

    // Material Domain — pra onde este material vai ser usado. Estrutura
    // inspirada na Unreal; só Surface e LightFunction são REALMENTE
    // suportados pelo motor hoje. Os outros aparecem no dropdown (pra já
    // deixar o caminho familiar/aberto) mas ficam desabilitados — não
    // fingimos suportar o que ainda não existe de verdade.
    enum class MaterialDomain
    {
        Surface,        // material de superfície normal (já existe)
        LightFunction,  // controla Color/Intensity de uma luz, via Emissive
        Particle,
        DeferredDecal,  // [indisponível] decal projetado num superfície
        Volume,         // [indisponível] material volumétrico
        PostProcess,    // [indisponível] efeito de tela inteira
        UserInterface,  // [indisponível] material pra UI/widgets
    };

    // Blend Mode — como o resultado final é composto com o que já está
    // desenhado. Opaque/Masked/Translucent/Additive são reais; o resto é
    // placeholder.
    enum class MaterialBlendMode
    {
        Opaque,
        Masked,       // corta com alpha (sem precisar do passe transparente)
        Translucent,  // já existente (Opacity + forward pass)
        Additive,     // soma a cor (fogo, energia, holograma)
        Modulate,         // [indisponível]
        AlphaComposite,   // [indisponível]
        AlphaHoldout,     // [indisponível]
    };

    // Shading Model — qual fórmula de iluminação usar. DefaultLit (PBR
    // completo, já existente) e Unlit (sem luz nenhuma, mostra a cor
    // direto — base pra cartoon/VFX/UI) são reais; o resto é placeholder.
    enum class MaterialShadingModel
    {
        DefaultLit,
        Unlit,
        Subsurface,             // [indisponível]
        ClearCoat,              // [indisponível]
        PreintegratedSkin,      // [indisponível]
        TwoSidedFoliage,        // [indisponível]
        Hair,                   // [indisponível]
        Cloth,                  // [indisponível]
        Eye,                    // [indisponível]
        SingleLayerWater,       // [indisponível]
        ThinTranslucent,        // [indisponível]
        FromMaterialExpression, // [indisponível]
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

        // Valor usado quando este pin é um INPUT e está desconectado — evita
        // ter que criar um node "Float" só pra alimentar uma constante
        // simples (igual a digitar direto no pin na Unreal). Só Float por
        // ora; Vec2/Vec3/Color desconectados continuam usando o fallback
        // fixo do node (ou um node constante dedicado).
        float DefaultFloat = 0.0f;

        Pin(int id, const char* name, PinType type, ed::PinKind kind)
            : ID(id), ParentNode(nullptr), Name(name), Type(type), Kind(kind) {}
    };

    struct Link
    {
        ed::LinkId ID;
        ed::PinId  StartPin;
        ed::PinId  EndPin;

        ImColor Color;

        Link() : ID(0), StartPin(0), EndPin(0) {}
        Link(int id, ed::PinId start, ed::PinId end)
            : ID(id), StartPin(start), EndPin(end), Color(255, 255, 255) {}
    };

    // Valor que um pin pode carregar
    struct PinValue
    {
        PinType Type = PinType::Float;

        float               FloatVal = 0.0f;
        glm::vec2           Vec2Val = { 0, 0 };
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
        bool IsConstant = false;

        // Usado apenas quando Name == "Comment" — o TEXTO exibido/editável
        // pelo usuário. Importante: Name continua sendo "Comment" sempre
        // (é o identificador de tipo usado por compilador/serialização/
        // undo); antes disso, o comment guardava o texto direto em Name,
        // o que quebrava a desserialização e o undo depois de renomear
        // (o node deixava de ser reconhecido como tipo "Comment").
        std::string StringValue;
        float CommentColor[3] = { 0.10f, 0.35f, 0.45f };

        Node(int id, const char* name, ImColor color = ImColor(255, 255, 255)) :
            ID(id), Name(name), Color(color), Type(NodeType::Blueprint), Size(0, 0)
        {}

        virtual ~Node() = default;
    };





} // namespace axe