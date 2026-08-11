#pragma once
#include "axe/core/types.hpp"
#include "script_base.hpp"
#include <string>
#include <memory>

namespace axe
{
    struct AXE_API ScriptComponent
    {
        std::string ScriptAssetPath;
        std::string DllPath;
        std::string ScriptName = "Nenhum";

        // ═══════════════════════════════════════════════════════════════════
        //  S0a — AQUI MORAVA UM `std::shared_ptr<ScriptGraph> Graph`.
        //
        //  ── POR QUE SAIU ─────────────────────────────────────────────────
        //
        //  Este e um componente de CENA: ele viaja no registry, entra no
        //  SceneSnapshot e e lido pelo SceneSerializer. Mas ScriptGraph vive
        //  em script_graph.hpp, que inclui <imgui_node_editor.h> e <imgui.h>
        //  e expoe ed::PinId, ImVec2, ImColor.
        //
        //  O resultado era uma cadeia que ninguem tinha escolhido:
        //
        //      scene_snapshot.hpp  ──┐
        //      scene_serializer.cpp ─┴─> script_component.hpp
        //                               └─> script_graph.hpp
        //                                   └─> imgui_node_editor.h
        //
        //  Ou seja: o NUCLEO DA CENA dependia da biblioteca de interface
        //  grafica. Um jogo empacotado carregava o editor de nodes porque um
        //  componente guardava um ponteiro para um grafo que ninguem executa.
        //
        //  ── POR QUE DAVA PARA SIMPLESMENTE APAGAR ────────────────────────
        //
        //  O campo era vestigio do fluxo ANTERIOR ao .axescript, quando o
        //  grafo morava dentro da entidade em vez de num asset. Tres fatos
        //  fecham o caso:
        //
        //    1. O SceneSerializer NUNCA gravou este grafo — so asset_path,
        //       name e compiled. Um grafo aqui morria no primeiro save/load
        //       da cena, entao nao ha nada em disco para migrar.
        //    2. Nem o ScriptWorld nem o compilador liam este campo. Quem
        //       compila le o ScriptAsset, sempre.
        //    3. O unico leitor era ScriptGraphWindow::OpenForEntity, que so
        //       era alcancado quando ScriptAssetPath estava VAZIO — estado
        //       que o fluxo atual nao produz mais.
        //
        //  Nao foi uma remocao de funcionalidade: foi a remocao do cadaver de
        //  uma funcionalidade que ja tinha parado de existir.
        //
        //  ── O QUE ISTO *NAO* RESOLVE ────────────────────────────────────
        //
        //  imgui continua dentro de axe.dll: ScriptAsset tambem guarda um
        //  ScriptGraph, e script_graph.cpp segue compilado na DLL. O que
        //  mudou e que a CENA nao paga mais por isso — a dependencia ficou
        //  contida no modulo de script, onde ao menos e visivel. Tirar o
        //  ScriptGraph inteiro da DLL e outro trabalho, bem maior.
        // ═══════════════════════════════════════════════════════════════════

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
            ScriptName(o.ScriptName),
            Instance(o.Instance), DllHandle(o.DllHandle),
            IsCompiled(o.IsCompiled), IsLoaded(o.IsLoaded),
            NeedsReload(o.NeedsReload) {}

        ScriptComponent& operator=(const ScriptComponent& o)
        {
            if (this == &o) return *this;
            ScriptAssetPath = o.ScriptAssetPath;
            DllPath = o.DllPath;
            ScriptName = o.ScriptName;
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