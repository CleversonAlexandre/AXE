#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/script/script_asset.hpp"
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
        // ── Arrays — cada um é um tipo de pin distinto (não um ScriptPinType::
        // Array genérico com subtipo) para manter cores/identidade visual
        // próprias no grafo, igual aos escalares. Sem cast implícito entre
        // tipos de array diferentes — ArePinsCompatible cai no "from==to"
        // (exact match) por padrão para todos eles, sem precisar de regras
        // extras abaixo.
        FloatArray, BoolArray, IntArray, Vec3Array, StringArray,
        Vec2Array, Vec4Array, QuatArray, EntityArray,
    };

    // BUGFIX (mesma causa do bug de Category): pin.Type era serializado como
    // inteiro cru ((int)pin.Type) — qualquer inserção futura no MEIO do enum
    // acima corromperia o tipo (e portanto a cor/comportamento) de todo pin
    // salvo antes. Serializar pelo NOME resolve isso de vez.
    inline std::string ScriptPinTypeToString(ScriptPinType t)
    {
        switch (t)
        {
        case ScriptPinType::Flow:        return "Flow";
        case ScriptPinType::Float:       return "Float";
        case ScriptPinType::Vec3:        return "Vec3";
        case ScriptPinType::Bool:        return "Bool";
        case ScriptPinType::Int:         return "Int";
        case ScriptPinType::String:      return "String";
        case ScriptPinType::Object:      return "Object";
        case ScriptPinType::Vec2:        return "Vec2";
        case ScriptPinType::Vec4:        return "Vec4";
        case ScriptPinType::Quat:        return "Quat";
        case ScriptPinType::Wildcard:    return "Wildcard";
        case ScriptPinType::FloatArray:  return "FloatArray";
        case ScriptPinType::BoolArray:   return "BoolArray";
        case ScriptPinType::IntArray:    return "IntArray";
        case ScriptPinType::Vec3Array:   return "Vec3Array";
        case ScriptPinType::StringArray: return "StringArray";
        case ScriptPinType::Vec2Array:   return "Vec2Array";
        case ScriptPinType::Vec4Array:   return "Vec4Array";
        case ScriptPinType::QuatArray:   return "QuatArray";
        case ScriptPinType::EntityArray: return "EntityArray";
        default:                         return "Float";
        }
    }
    inline ScriptPinType ScriptPinTypeFromString(const std::string& s)
    {
        if (s == "Flow")        return ScriptPinType::Flow;
        if (s == "Float")       return ScriptPinType::Float;
        if (s == "Vec3")        return ScriptPinType::Vec3;
        if (s == "Bool")        return ScriptPinType::Bool;
        if (s == "Int")         return ScriptPinType::Int;
        if (s == "String")      return ScriptPinType::String;
        if (s == "Object")      return ScriptPinType::Object;
        if (s == "Vec2")        return ScriptPinType::Vec2;
        if (s == "Vec4")        return ScriptPinType::Vec4;
        if (s == "Quat")        return ScriptPinType::Quat;
        if (s == "Wildcard")    return ScriptPinType::Wildcard;
        if (s == "FloatArray")  return ScriptPinType::FloatArray;
        if (s == "BoolArray")   return ScriptPinType::BoolArray;
        if (s == "IntArray")    return ScriptPinType::IntArray;
        if (s == "Vec3Array")   return ScriptPinType::Vec3Array;
        if (s == "StringArray") return ScriptPinType::StringArray;
        if (s == "Vec2Array")   return ScriptPinType::Vec2Array;
        if (s == "Vec4Array")   return ScriptPinType::Vec4Array;
        if (s == "QuatArray")   return ScriptPinType::QuatArray;
        if (s == "EntityArray") return ScriptPinType::EntityArray;
        return ScriptPinType::Float;
    }

    // BUGFIX relacionado: em vários lugares do editor, "é um tipo de array?"
    // era checado via (int)tipo >= (int)ScriptPinType::FloatArray — ou seja,
    // a ORDEM do enum também sustentava lógica de runtime, não só
    // serialização. Inserir um tipo novo no meio quebraria isso silenciosamente
    // também. Esta função substitui essa comparação ordinal por uma lista
    // explícita — sem depender de onde cada valor fica no enum.
    inline bool IsArrayPinType(ScriptPinType t)
    {
        switch (t)
        {
        case ScriptPinType::FloatArray:
        case ScriptPinType::BoolArray:
        case ScriptPinType::IntArray:
        case ScriptPinType::Vec3Array:
        case ScriptPinType::StringArray:
        case ScriptPinType::Vec2Array:
        case ScriptPinType::Vec4Array:
        case ScriptPinType::QuatArray:
        case ScriptPinType::EntityArray:
            return true;
        default:
            return false;
        }
    }

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
        Array,     // roxo claro — Array Add/Remove/Get/Length/Clear
        FlowControl, // azul acinzentado — Sequence/For Loop/For Each Loop
        Function,  // verde-azulado — Function Entry/Return Node/Call <Function>
        // Comment não usa cor de header (DrawNode trata ele num caminho
        // totalmente separado, como ed::Group em vez de node normal) — só
        // existe a categoria pra manter o campo Category sempre preenchido.
        Comment,
        // Reroute também não usa cor de header próprio — desenhado num
        // caminho compacto separado, sem header, só os dois pins (igual aos
        // nodes de Cast). Categoria existe só pra manter Category preenchido.
        Reroute,
        Variable,  // pink — Get/Set Variable nodes
        Print,     // rosa — Print String
    };

    // BUGFIX: Category era serializada como inteiro cru ((int)node->Category).
    // Toda vez que uma categoria nova é inserida no MEIO do enum acima (já
    // aconteceu 3x: FlowControl, Function, Comment), os valores numéricos de
    // TODA categoria depois dela mudam de posição — corrompendo a categoria
    // (e portanto a cor do header) de qualquer node salvo ANTES da inserção.
    // Sintoma exato: nodes trocando de cor sozinhos a cada reabertura do
    // projeto, depois de qualquer sessão que adicione uma categoria nova.
    // Serializar pelo NOME em vez do índice numérico resolve isso de vez —
    // mesma lição já aplicada em ScriptVarTypeToString/FromString.
    inline std::string ScriptNodeCategoryToString(ScriptNodeCategory c)
    {
        switch (c)
        {
        case ScriptNodeCategory::Event:       return "Event";
        case ScriptNodeCategory::Action:      return "Action";
        case ScriptNodeCategory::Logic:       return "Logic";
        case ScriptNodeCategory::Math:        return "Math";
        case ScriptNodeCategory::Input:       return "Input";
        case ScriptNodeCategory::Array:       return "Array";
        case ScriptNodeCategory::FlowControl: return "FlowControl";
        case ScriptNodeCategory::Function:    return "Function";
        case ScriptNodeCategory::Comment:     return "Comment";
        case ScriptNodeCategory::Reroute:     return "Reroute";
        case ScriptNodeCategory::Variable:    return "Variable";
        case ScriptNodeCategory::Print:       return "Print";
        default:                              return "Action";
        }
    }
    inline ScriptNodeCategory ScriptNodeCategoryFromString(const std::string& s)
    {
        if (s == "Event")       return ScriptNodeCategory::Event;
        if (s == "Action")      return ScriptNodeCategory::Action;
        if (s == "Logic")       return ScriptNodeCategory::Logic;
        if (s == "Math")        return ScriptNodeCategory::Math;
        if (s == "Input")       return ScriptNodeCategory::Input;
        if (s == "Array")       return ScriptNodeCategory::Array;
        if (s == "FlowControl") return ScriptNodeCategory::FlowControl;
        if (s == "Function")    return ScriptNodeCategory::Function;
        if (s == "Comment")     return ScriptNodeCategory::Comment;
        if (s == "Reroute")     return ScriptNodeCategory::Reroute;
        if (s == "Variable")    return ScriptNodeCategory::Variable;
        if (s == "Print")       return ScriptNodeCategory::Print;
        return ScriptNodeCategory::Action;
    }

    // SC19 — ScriptValue mudou de casa: agora vive em script_asset.hpp.
    //
    // ScriptVariable tambem precisa dele, e ScriptVariable esta la. Como
    // script_graph.hpp JA inclui script_asset.hpp, o caminho so funciona nesse
    // sentido — deixar o valor aqui obrigaria script_asset a incluir
    // script_graph, fechando um ciclo. Valor e mais fundamental que grafo;
    // morar no arquivo mais baixo e o lugar certo dele.

    // ─── SC20: componentes de um tipo vetorial ────────────────────────────────
    //
    // Split Struct Pin era Vec3-e-so-Vec3, com "X","Y","Z" escritos a mao em
    // seis lugares (menu, criacao, remocao, deteccao, recombinacao e o
    // compilador). Vec2, Vec4 e Quat nao tinham como ser abertos.
    //
    // Uma funcao responde quantos componentes o tipo tem; zero significa "nao
    // e vetorial, nao ha o que abrir". Acrescentar um tipo vetorial novo passa
    // a ser um case aqui, e nao uma cacada por strings "Z" no projeto.
    inline int ScriptPinComponentCount(ScriptPinType t)
    {
        switch (t)
        {
        case ScriptPinType::Vec2: return 2;
        case ScriptPinType::Vec3: return 3;
        case ScriptPinType::Vec4: return 4;
        case ScriptPinType::Quat: return 4;
        default: return 0;
        }
    }

    // Sufixos na ordem dos componentes. Quat usa XYZW (a ordem de
    // ARMAZENAMENTO do glm); a ordem do CONSTRUTOR, com w primeiro, e problema
    // do ToCppLiteral e nao aparece na tela.
    inline const char* ScriptPinComponentName(int i)
    {
        static const char* k[4] = { "X", "Y", "Z", "W" };
        return (i >= 0 && i < 4) ? k[i] : "X";
    }

    // ─── SC20: bits de ScriptNode::IntValue ───────────────────────────────────
    //
    // IntValue e um campo de uso geral que ja carregava DUAS coisas somadas: o
    // tipo da variavel nos 8 bits baixos e o flag de "pin splitado" no bit 8,
    // ambos escritos como numeros crus (0xFF, 0x100) espalhados pelo editor e
    // pelo compilador. O SC20 precisa guardar uma terceira — QUAL tipo foi
    // splitado, para o Recombine saber se remonta um Vec2, um Vec4 ou um Quat.
    //
    // Nomear os bits nao paga a divida (ela morre na fase 2 do ScriptValue,
    // quando ScriptNode for consolidado), mas impede que a terceira informacao
    // entre como mais um numero magico.
    namespace ScriptNodeBits
    {
        constexpr int VarTypeMask = 0x000000FF;  // ScriptVarType da variavel
        constexpr int SplitPin = 0x00000100;  // ha pin splitado neste node
        constexpr int SplitTypeShift = 12;
        constexpr int SplitTypeMask = 0x0001F000;  // ScriptPinType que foi splitado
    }

    inline ScriptPinType GetSplitSourceType(int intValue)
    {
        const int v = (intValue & ScriptNodeBits::SplitTypeMask) >> ScriptNodeBits::SplitTypeShift;
        // Zero significa "gravado antes do SC20", quando so Vec3 podia ser
        // splitado — assumir Vec3 mantem os grafos existentes corretos.
        return v == 0 ? ScriptPinType::Vec3 : (ScriptPinType)v;
    }

    inline void SetSplitSourceType(int& intValue, ScriptPinType t)
    {
        intValue &= ~ScriptNodeBits::SplitTypeMask;
        intValue |= ((int)t << ScriptNodeBits::SplitTypeShift) & ScriptNodeBits::SplitTypeMask;
    }

    struct ScriptPin
    {
        ed::PinId   ID;
        std::string Name;
        ScriptPinType Type;
        ed::PinKind Kind;

        // Valor usado quando o pin de entrada não tem fio.
        ScriptValue Default;

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

        // ── Valores locais do Set Variable ────────────────────────────────────
        // Independentes do DefaultValue da variável — cada node Set tem o seu.
        bool              BoolValue = false;
        int               IntLocalValue = 0;
        float             Vec3Value[3] = { 0,0,0 };
        // Valor local de String do Set Variable — StringValue já é usado
        // para guardar o NOME da variável (e o texto de Print String, a
        // Action/Axis selecionada, etc.), então precisa de um campo próprio
        // para não colidir com isso quando o tipo da variável é String.
        std::string       StringLocalValue;

        // ── Comment box ──────────────────────────────────────────────────────
        // Só usado quando Name == "Comment" — StringValue é reaproveitado
        // como o TEXTO do título (mesma convenção de reuso já usada pra
        // nome de variável/Action/etc.). Size e Color são exclusivos do
        // Comment, sem equivalente em nenhum outro tipo de node.
        ImVec2            CommentSize = ImVec2(320, 240);
        float             CommentColor[3] = { 0.10f, 0.35f, 0.45f };

        ScriptNode(int id, const char* name, ScriptNodeCategory cat)
            : ID(id), Name(name), Category(cat) {}

        ScriptNode(const ScriptNode&) = delete;
        ScriptNode(ScriptNode&&) noexcept = default;
        ScriptNode& operator=(const ScriptNode&) = delete;
        ScriptNode& operator=(ScriptNode&&) noexcept = default;
    };

    // ─── S1: validação de ligação — fonte única de verdade ────────────────────
    //
    // ANTES: a decisão de "esta ligação pode existir?" morava dentro do
    // BeginCreate() do canvas (script_node_graph.cpp), numa cadeia de if/else
    // com lambdas isNumeric/isVec redeclaradas em 3 pontos e varreduras
    // ad-hoc de m_Nodes para descobrir se um pin Wildcard pertencia a um
    // Reroute. Consequências mensuráveis, todas confirmadas no código:
    //
    //   1. ArePinsCompatible()/ArePinsExact() existiam neste header e NÃO eram
    //      chamadas por ninguém — a regra "oficial" e a regra realmente
    //      aplicada eram dois textos diferentes que ninguém garantia iguais.
    //   2. Nenhum caminho impedia ligar um pin de saída num pin de entrada do
    //      MESMO node.
    //   3. Nenhum caminho impedia um CICLO de dados. O compilador resolve
    //      entrada por recursão (ResolvePin → FindDataSource → ResolvePin);
    //      um ciclo de dados era stack overflow no Compilar, não erro de autor.
    //   4. Nenhum caminho impedia DOIS fios no mesmo pin de entrada.
    //      FindDataSource() devolve o PRIMEIRO link encontrado: com dois, o
    //      valor compilado passava a depender da ordem do vector — muda ao
    //      salvar/recarregar, sem nenhum aviso.
    //
    // AGORA: o canvas pergunta, o modelo responde. QueryLink() é a única
    // função que decide, e o editor só traduz o veredito em cor/tooltip.
    // Mesma separação já vigente no Control Rig: modelo na axe.dll, canvas
    // no editor.
    enum class ScriptLinkAction
    {
        Reject,             // não pode ligar — Message explica por quê
        Accept,             // tipos idênticos (inclusive Flow → Flow)
        AcceptImplicit,     // aceita com cast implícito (Vec3 ↔ Vec4) — preview laranja
        InsertConversion,   // aceita inserindo ConversionNode entre os dois pins
        AdoptWildcard,      // aceita fixando o Wildcard (Reroute) em AdoptType
        AdoptArrayWildcard, // aceita fixando o node genérico de Array em AdoptType
    };

    struct ScriptLinkQuery
    {
        ScriptLinkAction Action = ScriptLinkAction::Reject;

        // Tipo usado para colorir o preview do fio durante o drag.
        ScriptPinType LinkColorType = ScriptPinType::Wildcard;

        // AdoptWildcard / AdoptArrayWildcard: tipo concreto a fixar e node dono
        // do pin Wildcard. AdoptType é sempre o tipo do PIN (escalar ou *Array);
        // para AdoptArrayWildcard o chamador converte para ScriptVarType do
        // ELEMENTO via GetElementType, como já fazia.
        ScriptPinType     AdoptType = ScriptPinType::Wildcard;
        const ScriptNode* AdoptNode = nullptr;

        // InsertConversion: tipo de node a criar ("ToFloat", "ToInt", "ToBool",
        // "ToString"). Sempre com pins de entrada e saída chamados "Value".
        const char* ConversionNode = nullptr;

        // Tooltip a exibir. Nunca nulo — em Accept traz o rótulo da operação.
        const char* Message = "";

        // true quando a ligação vai SUBSTITUIR um fio existente (entrada de
        // dados já ocupada, ou saída de Flow já ocupada). Não impede nada;
        // existe para o canvas avisar antes de o usuário soltar o botão.
        bool ReplacesExisting = false;

        bool Allowed() const { return Action != ScriptLinkAction::Reject; }
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

        // ── S1: consulta e integridade das ligações ───────────────────────────

        // Node dono de um pin. Existia como lambda copiada em 5 pontos do
        // canvas ("for n : GetNodes() for p : n->Inputs if (&p == pin)") —
        // varredura por ENDEREÇO de pin, que só funciona enquanto ninguém
        // realoca o vector de pins no meio. Aqui a busca é por ID.
        const ScriptNode* FindNodeOfPin(ed::PinId id) const;
        ScriptNode* FindNodeOfPin(ed::PinId id);

        // O veredito único sobre uma ligação candidata. Aceita os dois pins em
        // qualquer ordem (normaliza saída/entrada internamente).
        ScriptLinkQuery QueryLink(ed::PinId a, ed::PinId b) const;

        // true se ligar fromOutput → toInput fechar um ciclo de DADOS.
        // Só dados: ciclo de Flow é legítimo (é assim que se faz um loop
        // manual, igual à Unreal) e o compilador já se protege com o
        // parâmetro depth de GenerateNode. Ciclo de dados não tem semântica
        // nenhuma — é sempre erro de autoria.
        bool WouldCreateDataCycle(ed::PinId fromOutput, ed::PinId toInput) const;

        // Remove links órfãos (pin inexistente), invertidos (saída→saída),
        // e violações de cardinalidade em grafos já salvos, mantendo sempre
        // o PRIMEIRO link de cada pin ocupado. Chamado no fim de Deserialize:
        // .axescript gravado antes deste patch pode conter duas fontes no
        // mesmo pin de entrada, e o valor compilado dependia da ordem do
        // vector. Retorna quantos links foram descartados (0 = arquivo limpo).
        int SanitizeLinks();

        // ── SC8: copiar / colar um pedaço do grafo ────────────────────────────
        //
        // Vive no MODELO, e não no editor, pelo mesmo motivo do ScriptPaths: a
        // reconstrução de um node a partir de JSON já existe aqui dentro
        // (Deserialize), e escrever uma segunda no editor garantiria que as
        // duas divergissem no primeiro campo novo de ScriptNode.
        //
        // SerializeSubset usa exatamente o formato de Serialize(), então o que
        // vai para o clipboard é um grafo válido — dá para inspecionar, e um
        // dia dá para colar entre scripts diferentes sem mudar nada aqui.
        nlohmann::json SerializeSubset(const std::vector<ed::NodeId>& nodes) const;

        // Cria cópias dos nodes com IDs NOVOS e devolve os IDs criados.
        //
        // ID novo não é detalhe: colar preservando o ID original produziria
        // dois nodes com a mesma identidade no mesmo grafo, e o FindNode
        // devolveria sempre o primeiro — o segundo ficaria visível na tela e
        // inalcançável por qualquer operação.
        //
        // Só os links cujas DUAS pontas estão no conjunto colado são
        // recriados. Um fio que saía para fora da seleção não tem onde
        // ancorar; inventar uma ligação para o node mais parecido seria pior
        // do que não ligar nada.
        std::vector<ed::NodeId> PasteSubset(const nlohmann::json& j, ImVec2 offset);

        // Reconstrói os pins de saída de um node "Get Axis" conforme o
        // AxisValueType do Axis Mapping selecionado (1/2/3 pins Float:
        // "Value", ou "X"/"Y", ou "X"/"Y"/"Z"). Remove com segurança quaisquer
        // links que apontavam para pins antigos antes de substituí-los — ver
        // implementação para detalhes. Não faz nada se o node já tiver
        // exatamente essa configuração (idempotente, seguro de chamar todo frame).
        void RebuildAxisOutputPins(ScriptNode* node, int axisValueType);

        // Reconstrói os pins "Item" (ArrayAdd/Get) ou tipo do pin "Array" dos
        // nodes genéricos de array, refletindo o ScriptVarType real do array
        // conectado (ex.: conectar uma variável Vec3Array faz os pins Array e
        // Item virarem Vec3Array/Vec3 respectivamente). arrayElementType é o
        // ScriptVarType ESCALAR do elemento (ex.: Vec3, não Vec3Array) — use
        // ScriptVarType::Float como "desconhecido ainda" junto de
        // node->IntValue == -1 para distinguir de uma conexão real a FloatArray.
        void RebuildArrayNodePins(ScriptNode* node, ScriptVarType arrayElementType);

        // Adiciona/remove pins "Then N" do node Sequence, preservando os pins
        // (e links) já existentes quando pinCount aumenta — só remove do fim
        // quando pinCount diminui, removendo com segurança qualquer link que
        // apontava para os pins descartados. Clampa entre 2 e 8 pins.
        // Idempotente: se node->Outputs.size() já é igual a pinCount, não faz nada.
        void RebuildSequencePins(ScriptNode* node, int pinCount);

        // Mesma ideia do RebuildSequencePins, mas pro Switch on Int — o pin
        // "Default" fica sempre fixo no FIM da lista de Outputs (nunca é
        // tocado), e os pins numerados "0".."caseCount-1" são os únicos
        // adicionados/removidos. Clampa entre 1 e 16 casos.
        void RebuildSwitchPins(ScriptNode* node, int caseCount);

        // Mesma ideia, pro AND/OR com N entradas (A, B, C, D...) — hoje só
        // aceitavam A/B fixos. Clampa entre 2 e 8 inputs. NOT (1 input fixo)
        // e XOR (2 inputs fixos — semântica de N-ário é ambígua) continuam
        // de aridade fixa, não passam por aqui.
        void RebuildLogicInputs(ScriptNode* node, int inputCount);

        // Reconstrói os pins de um node do sistema de Functions, a partir da
        // assinatura ATUAL de uma ScriptFunction — chamar sempre que Inputs/
        // Outputs da função mudarem (adicionar/remover/renomear/mudar tipo de
        // parâmetro). Funciona pros 3 papéis que um node de Function pode ter,
        // decidido pelo node->Name:
        //   "Function Entry" -> pins de SAÍDA = func.Inputs (+ "Flow Out" fixo)
        //   "Return Node"    -> pins de ENTRADA = func.Outputs (+ "Flow In" fixo)
        //   qualquer outro   -> é um node de Call: entrada = func.Inputs (+
        //                       "Flow In"), saída = func.Outputs (+ "Flow Out")
        // Tenta preservar links de pins cujo nome+tipo não mudou (mesma
        // lógica de "não destruir o que ainda é válido" do RebuildSequencePins),
        // removendo apenas os pins que de fato saíram da assinatura.
        void RebuildFunctionNodePins(ScriptNode* node, const ScriptFunction& func);

        // Cria um node "Call <func.Name>" com os pins já corretos pra
        // assinatura atual da função — usado ao arrastar uma Function do
        // Script Members pro canvas. Diferente dos outros node types, Call
        // nodes não passam por CreateNodeByType (o nome da função é dinâmico,
        // definido pelo usuário — não dá pra ter um "if (t == ...)" fixo pra
        // cada função que ainda nem existe).
        ScriptNode* AddCallFunctionNode(const ScriptFunction& func);

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
            // Arrays (índices 9-17, mesma ordem relativa dos escalares 0-8) —
            // tons mais escuros/saturados, espelhando GetPinColor para os tipos
            // *Array.
        case 9:  return ImColor(15, 90, 40);   // FloatArray
        case 10: return ImColor(120, 20, 20);  // BoolArray
        case 11: return ImColor(40, 140, 40);  // IntArray
        case 12: return ImColor(140, 120, 10); // Vec3Array
        case 13: return ImColor(140, 40, 100); // StringArray
        case 14: return ImColor(20, 140, 140); // Vec2Array
        case 15: return ImColor(110, 40, 160); // Vec4Array
        case 16: return ImColor(120, 90, 160); // QuatArray
        case 17: return ImColor(30, 70, 140);  // EntityArray
        case 18: return ImColor(215, 115, 25); // Asset — laranja
        default: return ImColor(180, 60, 140);
        }
    }

    // Converte o tipo de uma ScriptVariable para o ScriptPinType correspondente
    // — usado tanto pelos nodes Get/Set Variable quanto pelos nodes genéricos
    // de array (Array Add/Get/Length/etc.), evitando duplicar este mapeamento
    // em cada arquivo que precisa dele (script_node_draw.cpp, script_node_graph.cpp).
    // SC19 — o caminho inverso, para o lado do PIN poder pedir um literal C++
    // ao ScriptValue (que fala ScriptVarType, por morar em script_asset.hpp).
    // Sem isto haveria um segundo emissor de literais so para pinos — a
    // duplicacao que o SC17 acabou de eliminar.
    inline ScriptVarType PinTypeToVarType(ScriptPinType t)
    {
        switch (t)
        {
        case ScriptPinType::Float:  return ScriptVarType::Float;
        case ScriptPinType::Bool:   return ScriptVarType::Bool;
        case ScriptPinType::Int:    return ScriptVarType::Int;
        case ScriptPinType::Vec3:   return ScriptVarType::Vec3;
        case ScriptPinType::String: return ScriptVarType::String;
        case ScriptPinType::Vec2:   return ScriptVarType::Vec2;
        case ScriptPinType::Vec4:   return ScriptVarType::Vec4;
        case ScriptPinType::Quat:   return ScriptVarType::Quat;
        case ScriptPinType::Object: return ScriptVarType::Entity;
        case ScriptPinType::FloatArray:  return ScriptVarType::FloatArray;
        case ScriptPinType::BoolArray:   return ScriptVarType::BoolArray;
        case ScriptPinType::IntArray:    return ScriptVarType::IntArray;
        case ScriptPinType::Vec3Array:   return ScriptVarType::Vec3Array;
        case ScriptPinType::StringArray: return ScriptVarType::StringArray;
        case ScriptPinType::Vec2Array:   return ScriptVarType::Vec2Array;
        case ScriptPinType::Vec4Array:   return ScriptVarType::Vec4Array;
        case ScriptPinType::QuatArray:   return ScriptVarType::QuatArray;
        case ScriptPinType::EntityArray: return ScriptVarType::EntityArray;
        default: return ScriptVarType::Float;   // Flow e Wildcard nao tem valor
        }
    }

    inline ScriptPinType ScriptVarTypeToPinType(ScriptVarType t)
    {
        switch (t) {
        case ScriptVarType::Bool:   return ScriptPinType::Bool;
        case ScriptVarType::Int:    return ScriptPinType::Int;
        case ScriptVarType::Vec3:   return ScriptPinType::Vec3;
        case ScriptVarType::String: return ScriptPinType::String;
        case ScriptVarType::Vec2:   return ScriptPinType::Vec2;
        case ScriptVarType::Vec4:   return ScriptPinType::Vec4;
        case ScriptVarType::Quat:   return ScriptPinType::Quat;
        case ScriptVarType::Entity: return ScriptPinType::Object;
        case ScriptVarType::FloatArray:  return ScriptPinType::FloatArray;
        case ScriptVarType::BoolArray:   return ScriptPinType::BoolArray;
        case ScriptVarType::IntArray:    return ScriptPinType::IntArray;
        case ScriptVarType::Vec3Array:   return ScriptPinType::Vec3Array;
        case ScriptVarType::StringArray: return ScriptPinType::StringArray;
        case ScriptVarType::Vec2Array:   return ScriptPinType::Vec2Array;
        case ScriptVarType::Vec4Array:   return ScriptPinType::Vec4Array;
        case ScriptVarType::QuatArray:   return ScriptPinType::QuatArray;
        case ScriptVarType::EntityArray: return ScriptPinType::EntityArray;

            // SC30 — Asset vira pino de STRING, e nao um tipo de pino novo.
            //
            // O valor de uma referencia de asset E o UUID, uma string. Como
            // pino String, ele ja liga direto em tudo que a engine consome por
            // nome/UUID: PlaySound2D aceita os dois, o Sound Cue idem. Um
            // ScriptPinType::Asset novo obrigaria a escrever conversao para
            // String em cada um desses pontos para chegar no mesmo lugar.
            //
            // A tipagem forte fica onde ela de fato ajuda: no EDITOR, onde o
            // seletor so lista assets do tipo declarado no TypeQualifier. No
            // grafo, um UUID e texto.
        case ScriptVarType::Asset:  return ScriptPinType::String;
        default: return ScriptPinType::Float; // Float e fallback
        }
    }

    // Retorna a cor do pin para cada tipo
    AXE_API ImColor GetPinColor(ScriptPinType type);

} // namespace axe