#include "axe/script/script_asset.hpp"
#pragma once
#include "axe/core/types.hpp"
#include "script_graph.hpp"
#include <string>

namespace axe
{
    class AXE_API ScriptGraphCompiler
    {
    public:
        // Gera o código C++ completo a partir do grafo
        // scriptName — nome da classe gerada (ex: "SphereScript")
        // assetVars: variáveis tipadas do asset (evita duplicação com fallback float)
        // functions: ScriptFunctions do asset (estilo Function da Unreal) —
        // cada uma gera um método próprio na classe, e nodes "Call <Func>" em
        // QUALQUER grafo (principal ou de outra função) conseguem chamá-la.
        static std::string Generate(const ScriptGraph& graph,
            const std::string& scriptName,
            const std::vector<ScriptVariable>* assetVars = nullptr,
            const std::vector<ScriptFunction>* functions = nullptr);

        // Mapeia ScriptVarType -> nome do tipo em C++ (ex: Vec3 -> "glm::vec3").
        // Extraído pra função própria porque é usado em 3 lugares: declaração
        // de variável-membro, assinatura de método de Function (parâmetros +
        // retorno), e variáveis locais de resultado de um Call — duplicar o
        // switch nos 3 lugares era exatamente o tipo de lógica implícita
        // duplicada que é melhor evitar.
        static std::string CppTypeNameFor(ScriptVarType t);

    private:
        struct Context
        {
            const ScriptGraph* graph;
            std::string        code;
            int                indent = 2;
            std::string        eventName;  // nome do evento atual (ex: "OnCollision")
            const std::vector<ScriptVariable>* assetVars = nullptr;  // variáveis tipadas do asset
            const std::vector<ScriptFunction>* functions = nullptr;  // ScriptFunctions do asset
            // Não-nulo enquanto estamos gerando o CORPO de uma função (em vez
            // do corpo de um evento) — usado pelo Return Node pra saber pra
            // qual ScriptFunction emitir o(s) "return"/atribuição dos Outputs.
            const ScriptFunction* currentFunction = nullptr;
            // true enquanto gerando o corpo de um Loop Body (For/While/For
            // Each) ou de uma Function — usado pelo node Delay pra saber se
            // está num contexto onde "pausar e continuar depois" não tem
            // semântica bem definida ainda (precisaria preservar o estado do
            // próprio loop/função também). Fora desses dois casos (direto
            // num Event body, ou dentro de Branch/Sequence/Switch — que não
            // repetem, só desviam uma vez), fica false.
            bool insideLoopOrFunction = false;

            void Line(const std::string& s = "")
            {
                for (int i = 0; i < indent; i++) code += "    ";
                code += s + "\n";
            }
        };

        // Gera o corpo de uma ScriptFunction (a partir do node "Function
        // Entry" do seu próprio grafo) — mesmo papel de GenerateEventBody,
        // só que pra dentro de um método de Function em vez de um evento.
        // IMPORTANTE: troca ctx.graph para func.Graph.get() durante a
        // geração e restaura o grafo anterior ao final (RAII manual — ver
        // implementação), porque cada ScriptFunction tem seu PRÓPRIO
        // ScriptGraph, isolado do grafo principal.
        static void GenerateFunctionBody(Context& ctx, const ScriptFunction& func);

        // Gera o corpo de um evento (OnStart, OnUpdate, etc.)
        static void GenerateEventBody(Context& ctx,
            const ScriptNode* eventNode,
            const std::string& deltaTimeVar);

        // Gera código para um node de ação e segue o flow
        static void GenerateNode(Context& ctx,
            const ScriptNode* node,
            const std::string& deltaTimeVar,
            int depth = 0);

        // Resolve o valor de um pin de dados (segue links para trás)
        static std::string ResolvePin(Context& ctx,
            const ScriptPin& pin);

        // Encontra o node conectado ao Flow Out de um node
        static const ScriptNode* FindNextFlowNode(const Context& ctx,
            const ScriptNode* node,
            const std::string& outPinName = "Flow Out");

        // Encontra o node e pin conectados a um pin de entrada de dados
        static std::pair<const ScriptNode*, const ScriptPin*>
            FindDataSource(const Context& ctx, const ScriptPin& inputPin);
    };

} // namespace axe