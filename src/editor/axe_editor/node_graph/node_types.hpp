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

    // ── MATFUNC_V1 ───────────────────────────────────────────────────────────
    //
    //  PinType <-> string. Por STRING, e nao por indice do enum, de proposito:
    //  a nota logo abaixo, sobre MaterialDomain, documenta que o `.axegraph`
    //  serializa aqueles enums por INDICE e que inserir um valor no meio
    //  re-sombreia todo material ja salvo.
    //
    //  A assinatura de uma Material Function e exatamente o dado que nao pode
    //  ter essa fragilidade, porque ela atravessa DOIS arquivos: o
    //  `.axematfunc` e o material que o chama. Um enum reordenado trocaria os
    //  tipos dos pinos de toda chamada gravada em disco.
    //
    //  Nem `Any` nem `Texture2D` aparecem na lista. `Any` so se resolve dentro
    //  de um node generico, pelo que esta ligado nele — numa assinatura, que e
    //  desenhada como pino no node de chamada antes de qualquer ligacao
    //  existir, nao ha o que resolver. E textura nao passa por parametro: ela
    //  entra na funcao por um Texture Sample dentro dela. Qualquer coisa fora
    //  da lista le como Float, o mesmo default do resto do grafo.
    inline const char* PinTypeToString(PinType type)
    {
        switch (type)
        {
        case PinType::Float: return "Float";
        case PinType::Vec2:  return "Vec2";
        case PinType::Vec3:  return "Vec3";
        case PinType::Vec4:  return "Vec4";
        default:             return "Float";
        }
    }

    inline PinType PinTypeFromString(const std::string& str)
    {
        if (str == "Vec2") return PinType::Vec2;
        if (str == "Vec3") return PinType::Vec3;
        if (str == "Vec4") return PinType::Vec4;
        return PinType::Float;
    }

    // Uma porta nomeada e tipada de uma Material Function — uma entrada ou uma
    // saida. Mora aqui, e nao em material_function.hpp, porque o MaterialGraph
    // precisa dela para montar os pinos do node de chamada, e material_function
    // ja inclui material_graph: no header dele isto seria dependencia circular.
    //
    // Igual em forma ao ScriptFunctionParam do Script Editor de proposito — e o
    // mesmo conceito, e vale que as duas partes da engine se pareçam.
    struct MaterialFunctionParam
    {
        std::string Name = "In";
        PinType     Type = PinType::Float;
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
        Volume,         // VOLUME_DOMAIN_V1 — o MEIO participante (fog) (real)
        PostProcess,    // POSTPROCESS_DOMAIN_V1 — efeito de tela inteira (real)
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
    // SHADING_MODEL_V1 — este enum e da UI e pode crescer/reordenar; quem vai
    // pro G-Buffer e o `ShadingModelID` de axe/material/material_cooked.hpp.
    // Ver a nota longa la sobre por que sao dois enums separados.
    //
    // "Toon" entrou no FIM de proposito: o valor e serializado por INDICE no
    // `.axegraph`, entao inseri-lo no meio re-sombrearia todo material ja
    // salvo. Mesma regra dos pins do Material Output.
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
        Toon,                   // SHADING_MODEL_V1 — sempre no FIM (ver acima)
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

        // ═══════════════════════════════════════════════════════════════════
        //  CUSTOM_NODE_V1 — GLSL escrito a mao dentro do grafo
        //
        //  Usado apenas quando Name == "Custom". E o equivalente do node
        //  Custom da Unreal: em vez de esperar que a engine ganhe um node
        //  para cada operacao imaginavel, o usuario escreve o GLSL.
        //
        //  ── POR QUE UM CAMPO NO NODE, E NAO UM NODE-FILHO ──────────────────
        //
        //  O compilador percorre `Node` cru (nao ha polimorfismo de node no
        //  MaterialGraph: tudo e despachado por `Name` em GenerateNodeCode).
        //  Criar uma subclasse aqui obrigaria a introduzir dynamic_cast no
        //  compilador, na serializacao e no desenho — tres lugares — para um
        //  node so. `StringValue` e `CommentColor` acima ja seguem exatamente
        //  este padrao para o Comment.
        //
        //  Os NOMES e TIPOS das entradas nao moram aqui: eles ja sao os
        //  proprios `Pin` do node (Pin::Name, Pin::Type). O que muda no
        //  Custom e que essa lista e EDITAVEL pelo usuario, e nao fixa pela
        //  fabrica — e por isso ele e o unico node cujos pins precisam ser
        //  reconstruidos na desserializacao (ver MaterialGraph::Deserialize).
        // ═══════════════════════════════════════════════════════════════════
        std::string CustomCode;
        PinType     CustomOutputType = PinType::Float;

        Node(int id, const char* name, ImColor color = ImColor(255, 255, 255)) :
            ID(id), Name(name), Color(color), Type(NodeType::Blueprint), Size(0, 0)
        {}

        virtual ~Node() = default;
    };





} // namespace axe