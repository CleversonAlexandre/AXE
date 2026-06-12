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

        // entt usa move semantics internamente — preservar Instance e DllHandle no move
        ScriptComponent() = default;

        // Copy — NÃO copia runtime state (Instance/DllHandle)
        // Usado para clonar definições, não instâncias ativas
        ScriptComponent(const ScriptComponent& o)
            : ScriptAssetPath(o.ScriptAssetPath), DllPath(o.DllPath),
            ScriptName(o.ScriptName), Graph(o.Graph),
            Instance(o.Instance), DllHandle(o.DllHandle),
            IsCompiled(o.IsCompiled), IsLoaded(o.IsLoaded),
            NeedsReload(o.NeedsReload) {}

        ScriptComponent& operator=(const ScriptComponent& o)
        {
            if (this == &o) return *this;
            ScriptAssetPath = o.ScriptAssetPath;
            DllPath = o.DllPath;
            ScriptName = o.ScriptName;
            Graph = o.Graph;
            Instance = o.Instance;   // preserva shared_ptr
            DllHandle = o.DllHandle;  // preserva handle
            IsCompiled = o.IsCompiled;
            IsLoaded = o.IsLoaded;
            NeedsReload = o.NeedsReload;
            return *this;
        }

        // Move — entt usa isso ao realocar storage
        ScriptComponent(ScriptComponent&& o) noexcept
            : ScriptAssetPath(std::move(o.ScriptAssetPath)),
            DllPath(std::move(o.DllPath)),
            ScriptName(std::move(o.ScriptName)),
            Graph(std::move(o.Graph)),
            Instance(std::move(o.Instance)),
            DllHandle(o.DllHandle),
            IsCompiled(o.IsCompiled),
            IsLoaded(o.IsLoaded),
            NeedsReload(o.NeedsReload)
        {
            o.DllHandle = nullptr;
            o.IsLoaded = false;
        }

        ScriptComponent& operator=(ScriptComponent&& o) noexcept
        {
            if (this == &o) return *this;
            ScriptAssetPath = std::move(o.ScriptAssetPath);
            DllPath = std::move(o.DllPath);
            ScriptName = std::move(o.ScriptName);
            Graph = std::move(o.Graph);
            Instance = std::move(o.Instance);
            DllHandle = o.DllHandle;
            IsCompiled = o.IsCompiled;
            IsLoaded = o.IsLoaded;
            NeedsReload = o.NeedsReload;
            o.DllHandle = nullptr;
            o.IsLoaded = false;
            return *this;
        }
    };

} // namespace axe