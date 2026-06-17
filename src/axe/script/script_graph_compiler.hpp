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
        static std::string Generate(const ScriptGraph& graph,
            const std::string& scriptName,
            const std::vector<ScriptVariable>* assetVars = nullptr);

    private:
        struct Context
        {
            const ScriptGraph& graph;
            std::string        code;
            int                indent = 2;
            std::string        eventName;  // nome do evento atual (ex: "OnCollision")
            const std::vector<ScriptVariable>* assetVars = nullptr;  // variáveis tipadas do asset

            void Line(const std::string& s = "")
            {
                for (int i = 0; i < indent; i++) code += "    ";
                code += s + "\n";
            }
        };

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
        static std::string ResolvePin(const Context& ctx,
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