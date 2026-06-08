#pragma once
#include "axe/core/types.hpp"
#include "script_base.hpp"
#include "script_graph.hpp"
#include <string>
#include <memory>

namespace axe
{
    struct AXE_API ScriptComponent
    {
        std::string ScriptAssetPath;
        std::string DllPath;
        std::string ScriptName = "Nenhum";

        // Grafo visual — shared_ptr para evitar cópia de unique_ptr interno
        std::shared_ptr<ScriptGraph> Graph = std::make_shared<ScriptGraph>();

        // Runtime
        std::shared_ptr<ScriptBase> Instance;
        void* DllHandle = nullptr;
        bool IsCompiled = false;
        bool IsLoaded = false;
        bool NeedsReload = false;

        // Cópia segura para entt
        ScriptComponent() = default;
        ScriptComponent(const ScriptComponent& o)
            : ScriptAssetPath(o.ScriptAssetPath), DllPath(o.DllPath),
            ScriptName(o.ScriptName), Graph(o.Graph),
            DllHandle(nullptr), IsCompiled(o.IsCompiled),
            IsLoaded(false), NeedsReload(false) {}
        ScriptComponent& operator=(const ScriptComponent& o)
        {
            ScriptAssetPath = o.ScriptAssetPath;
            DllPath = o.DllPath;
            ScriptName = o.ScriptName;
            Graph = o.Graph;
            IsCompiled = o.IsCompiled;
            IsLoaded = false;
            NeedsReload = false;
            DllHandle = nullptr;
            Instance = nullptr;
            return *this;
        }
    };

} // namespace axe