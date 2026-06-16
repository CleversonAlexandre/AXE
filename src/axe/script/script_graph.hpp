#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include <imgui_node_editor.h>
#include <imgui.h>
#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>

namespace ed = ax::NodeEditor;

namespace axe
{
    // Tipo do pin — Flow controla execução, os demais passam dados
    enum class ScriptPinType
    {
        Flow,    // Laranja  — controle de execução
        Float,   // Verde escuro
        Vec3,    // Amarelo
        Bool,    // Vermelho
        Int,     // Verde claro
        String,  // Rosa
        Object,  // Azul — referência a entity
        Vec2,    // Ciano
        Vec4,    // Roxo claro
        Quat,    // Lavanda
        Wildcard,// Branco — aceita qualquer tipo (para cast nodes)
    };

    // Retorna true se os tipos são compatíveis para conexão direta (sem cast)
    inline bool ArePinsExact(ScriptPinType from, ScriptPinType to)
    {
        return from == to;
    }

    // Retorna true se os tipos são compatíveis para conexão (exact ou cast implícito permitido)
    // Regras:
    //   - Flow nunca mistura com dados
    //   - Wildcard: liberado aqui, filtrado por IsWildcardCastCompatible no momento do link
    //   - Numéricas (Float/Int/Bool): cast implícito entre si (com aviso laranja)
    //   - Vec3 ↔ Vec4: cast implícito (perde/adiciona w)
    //   - TUDO MAIS: incompatível — use nodes de cast explícitos
    inline bool ArePinsCompatible(ScriptPinType from, ScriptPinType to)
    {
        if (from == to) return true;

        // Flow nunca mistura com dados
        if (from == ScriptPinType::Flow || to == ScriptPinType::Flow) return false;

        // Wildcard — o node de cast aceita o tipo; validação fina feita em IsWildcardCastCompatible
        if (to == ScriptPinType::Wildcard || from == ScriptPinType::Wildcard) return true;

        // Cast numérico implícito (Float ↔ Int ↔ Bool) — aceito com aviso visual
        auto isNumeric = [](ScriptPinType t) {
            return t == ScriptPinType::Float || t == ScriptPinType::Int || t == ScriptPinType::Bool;
            };
        if (isNumeric(from) && isNumeric(to)) return true;

        // Vec3 ↔ Vec4 (cast implícito — perde/adiciona componente w)
        if ((from == ScriptPinType::Vec3 && to == ScriptPinType::Vec4) ||
            (from == ScriptPinType::Vec4 && to == ScriptPinType::Vec3)) return true;

        // Tudo mais é incompatível — user deve usar node de cast explícito
        return false;
    }

    // Mensagem de erro descritiva para o tooltip quando pins são incompatíveis
    inline const char* GetPinIncompatibleReason(ScriptPinType from, ScriptPinType to)
    {
        if (from == ScriptPinType::Flow || to == ScriptPinType::Flow)
            return "Flow nao conecta com dados";

        auto isNumeric = [](ScriptPinType t) {
            return t == ScriptPinType::Float || t == ScriptPinType::Int || t == ScriptPinType::Bool;
            };
        auto isVec = [](ScriptPinType t) {
            return t == ScriptPinType::Vec3 || t == ScriptPinType::Vec4;
            };

        if (isVec(from) && isNumeric(to))   return "Use Break Vec3 para extrair componentes X/Y/Z";
        if (isNumeric(from) && isVec(to))   return "Use Make Vec3 ou Float to Vec3";
        if (from == ScriptPinType::String && isNumeric(to)) return "Use To Float / To Int / To Bool";
        if (isNumeric(from) && to == ScriptPinType::String) return "Use o node To String";
        if (isVec(from) && to == ScriptPinType::String)     return "Use o node To String";
        if (from == ScriptPinType::String && isVec(to))     return "String nao converte para Vec diretamente";
        if (from == ScriptPinType::Quat || to == ScriptPinType::Quat)
            return "Quat so conecta com Quat";
        if (from == ScriptPinType::Object || to == ScriptPinType::Object)
            return "Object so conecta com Object";

        return "Tipos incompativeis — use um node Cast";
    }

    // Compatibilidade fina para pins Wildcard (ToFloat, ToInt, ToBool, ToString)
    // Chamado ao criar um link onde 'to' é Wildcard, para validar se o cast faz sentido.
    // 'castOutputType' é o tipo de saída do node de cast (ex: Float para ToFloat).
    inline bool IsWildcardCastCompatible(ScriptPinType from, ScriptPinType castOutputType)
    {
        if (from == ScriptPinType::Flow)   return false;
        if (from == ScriptPinType::Object) return false;

        // ToString aceita qualquer tipo de dado
        if (castOutputType == ScriptPinType::String) return true;

        // ToFloat / ToInt / ToBool: só aceita numeric e String
        auto isNumeric = [](ScriptPinType t) {
            return t == ScriptPinType::Float || t == ScriptPinType::Int || t == ScriptPinType::Bool;
            };
        if (isNumeric(castOutputType))
            return isNumeric(from) || from == ScriptPinType::String;
        // Vec3/Vec4/Quat → Float não faz sentido sem especificar componente

        return false;
    }

    // Categoria do node — define a cor do header
    enum class ScriptNodeCategory
    {
        Event,
        Action,
        Logic,
        Math,
        Input,
        Variable,  // pink — Get/Set Variable nodes
        Print,     // rosa — Print String
    };

    struct ScriptPin
    {
        ed::PinId   ID;
        std::string Name;
        ScriptPinType Type;
        ed::PinKind Kind;

        float     DefaultFloat = 0.0f;
        bool      DefaultBool = false;
        int       DefaultInt = 0;
        std::string DefaultString;
        glm::vec3 DefaultVec3 = {};

        ScriptPin(int id, const char* name, ScriptPinType type, ed::PinKind kind)
            : ID(id), Name(name), Type(type), Kind(kind) {}

        // Necessários para vector reallocation
        ScriptPin(const ScriptPin&) = default;
        ScriptPin(ScriptPin&&) noexcept = default;
        ScriptPin& operator=(const ScriptPin&) = default;
        ScriptPin& operator=(ScriptPin&&) noexcept = default;
    };

    struct ScriptLink
    {
        ed::LinkId ID;
        ed::PinId  StartPin;
        ed::PinId  EndPin;

        ScriptLink(int id, ed::PinId start, ed::PinId end)
            : ID(id), StartPin(start), EndPin(end) {}

        ScriptLink(const ScriptLink&) = default;
        ScriptLink(ScriptLink&&) noexcept = default;
        ScriptLink& operator=(const ScriptLink&) = default;
        ScriptLink& operator=(ScriptLink&&) noexcept = default;
    };

    struct ScriptNode
    {
        ed::NodeId        ID;
        std::string       Name;
        ScriptNodeCategory Category;
        std::vector<ScriptPin> Inputs;
        std::vector<ScriptPin> Outputs;
        ImVec2            Position = { 0, 0 };

        std::string       StringValue;
        float             FloatValue = 0.0f;
        int               IntValue = 0;    // uso geral: tipo da variável para Get/Set Variable

        ScriptNode(int id, const char* name, ScriptNodeCategory cat)
            : ID(id), Name(name), Category(cat) {}

        ScriptNode(const ScriptNode&) = delete;
        ScriptNode(ScriptNode&&) noexcept = default;
        ScriptNode& operator=(const ScriptNode&) = delete;
        ScriptNode& operator=(ScriptNode&&) noexcept = default;
    };

    // Grafo completo de script de um objeto
    class AXE_API ScriptGraph
    {
    public:
        ScriptGraph() = default;
        ScriptGraph(const ScriptGraph&) = delete;
        ScriptGraph(ScriptGraph&&) noexcept = default;
        ScriptGraph& operator=(const ScriptGraph&) = delete;
        ScriptGraph& operator=(ScriptGraph&&) noexcept = default;

        ScriptNode* AddNode(const char* type);
        void        RemoveNode(ed::NodeId id);
        ScriptLink* AddLink(ed::PinId startPin, ed::PinId endPin);
        void        RemoveLink(ed::LinkId id);

        ScriptPin* FindPin(ed::PinId id);
        ScriptNode* FindNode(ed::NodeId id);
        bool        IsPinLinked(ed::PinId id) const;

        // Serialização
        nlohmann::json Serialize() const;
        void           Deserialize(const nlohmann::json& j);

        const std::vector<std::unique_ptr<ScriptNode>>& GetNodes() const { return m_Nodes; }
        const std::vector<ScriptLink>& GetLinks() const { return m_Links; }

        int GetNextId() { return m_NextId++; }

    private:
        std::vector<std::unique_ptr<ScriptNode>> m_Nodes;
        std::vector<ScriptLink>                  m_Links;
        int m_NextId = 1;

        // Fábrica de nodes por tipo
        std::unique_ptr<ScriptNode> CreateNodeByType(const char* type, int id);
    };

    // Retorna a cor ImGui do header para cada categoria
    AXE_API ImColor GetNodeHeaderColor(ScriptNodeCategory cat);
    // Inline — não depende de recompilação da axe.dll
    inline ImColor GetVariableNodeColor(int varTypeIndex)
    {
        switch (varTypeIndex) {
        case 0: return ImColor(30, 140, 60);  // Float  - verde escuro
        case 1: return ImColor(180, 40, 40);  // Bool   - vermelho
        case 2: return ImColor(80, 200, 80);  // Int    - verde claro
        case 3: return ImColor(200, 180, 30);  // Vec3   - amarelo
        case 4: return ImColor(200, 80, 150); // String - rosa
        case 5: return ImColor(40, 200, 200); // Vec2   - ciano
        case 6: return ImColor(160, 80, 220); // Vec4   - roxo
        case 7: return ImColor(180, 140, 220); // Quat   - lavanda
        case 8: return ImColor(60, 120, 200); // Entity - azul
        default: return ImColor(180, 60, 140);
        }
    }

    // Retorna a cor do pin para cada tipo
    AXE_API ImColor GetPinColor(ScriptPinType type);

} // namespace axe