workspace "axe"
    location "."
    architecture "x64"
    startproject "editor"

    configurations
    {
        "Debug",
        "Release",
        "Dist"
    }

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

filter "action:vs*"
    -- /bigobj — eleva o limite de seções do arquivo .obj (COFF), que por
    -- padrão é ~65.000.
    --
    -- Não é otimização nem workaround de bug: é um formato de .obj mais
    -- largo. Zero custo em runtime, zero mudança no binário final — só o
    -- objeto intermediário passa a caber.
    --
    -- Ficou necessário porque `components.hpp` é um hub incluído por dezenas
    -- de TUs, carregando EnTT + glm + ImGui + Jolt, e o sistema de animação
    -- (Pose / AnimationPlayer / BlendSpace1D) somou instanciações de template
    -- suficientes pra estourar o teto.
    --
    -- Qualquer projeto que usa EnTT a sério acaba precisando disso. É o
    -- caminho recomendado pela própria Microsoft (a mensagem do C1128 diz
    -- literalmente "compile with /bigobj").
    buildoptions { "/utf-8", "/bigobj" }

IncludeDir = {}
IncludeDir["simdjson"] = "src/vendor/simdjson/singleheader"
IncludeDir["spdlog"] = "src/vendor/spdlog"
IncludeDir["GLFW"] = "src/vendor/GLFW/include"
IncludeDir["Imgui"] = "src/vendor/imgui"
IncludeDir["Glad"] = "src/vendor/Glad/include"
IncludeDir["ImGuizmo"] = "src/vendor/imguizmo"
IncludeDir["assimp"] = "src/vendor/assimp/include"
IncludeDir["entt"] = "src/vendor/entt/src"
IncludeDir["nlohmann"] = "src/vendor"
IncludeDir["stb"] = "src/vendor/stb"
IncludeDir["imguinodeeditor"] = "src/vendor/imgui-node-editor"
-- VIEW_GIZMO_V1 — ImViewGuizmo (MIT, header-only, ja usa glm)
IncludeDir["imviewguizmo"] = "src/vendor/imview-guizmo"
IncludeDir["Jolt"] = "src/vendor/JoltPhysics"
-- miniaudio: header unico, dominio publico / MIT-0. Sem submodulo, sem
-- CMake, sem .lib e sem DLL pra copiar no post-build — so o include.
-- Entra APENAS no projeto `axe`: o editor nunca ve o backend de audio,
-- do mesmo jeito que nao ve Glad fora do backend grafico.
IncludeDir["miniaudio"] = "src/vendor/miniaudio"


include "src/vendor/simdjson/singleheader"
include "src/vendor/spdlog"
include "src/vendor/GLFW"
include "src/vendor/Glad"
include "src/vendor/JoltPhysics"
-- include "src/vendor/assimp"



project "axe"
    location "src/axe"
    kind "SharedLib"
    language "C++"
    targetname "axe"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "src/axe/**.hpp",
        "src/axe/**.cpp",

        -- ── ImGui: fica AQUI, e o motivo nao e inercia ───────────────────
        --
        -- O S0b tirou daqui e o editor passou a crashar no boot, dentro de
        -- `ImGui_ImplGlfw_Init`. A causa nao e de codigo:
        --
        --   GLFW e Glad sao STATIC LIBS linkadas nos DOIS binarios. Cada um
        --   tem a propria copia do estado global. Quem chama `glfwInit` e
        --   cria a janela e a `axe.dll`; se o backend do ImGui for compilado
        --   no `editor.exe`, ele conversa com a OUTRA copia do GLFW — a que
        --   nunca foi inicializada — e recebe um GLFWwindow que ela nao
        --   conhece. O mesmo valeria para o Glad: os ponteiros de funcao GL
        --   foram carregados na copia da DLL.
        --
        -- Ou seja: os backends tem que morar com o dono do estado, e o core
        -- do ImGui vai junto porque os backends dependem dele.
        --
        -- O que o S0b ganhou continua valendo: NENHUM arquivo de `src/axe/`
        -- inclui imgui. O que vive aqui e so o vendor, compilado e exportado
        -- para o editor consumir.
        --
        -- ImGuizmo e imgui-node-editor NAO estao aqui: os dois so falam com o
        -- ImGui (nada de GLFW/Glad) e sao usados so pelo editor.
        "src/vendor/imgui/imgui.cpp",
        "src/vendor/imgui/imgui_draw.cpp",
        "src/vendor/imgui/imgui_tables.cpp",
        "src/vendor/imgui/imgui_widgets.cpp",
        "src/vendor/imgui/imgui_demo.cpp",
        "src/vendor/imgui/backends/imgui_impl_opengl3.cpp",
        "src/vendor/imgui/backends/imgui_impl_glfw.cpp",
    }

    -- S0b — o `removefiles` de imgui que morava aqui foi embora junto: ele
    -- excluia copias de imgui dentro de `src/axe/**`, e nao ha mais nenhuma.

    includedirs
    {
        "src",
        "src/vendor/spdlog/include",
        "src/vendor/fmt/include",
        "src/vendor/geogram/src/lib",
        "src/vendor/simdjson/include",
        "src/vendor/glm",
        "%{IncludeDir.GLFW}",
        "%{IncludeDir.Glad}",
        -- Imgui volta porque este projeto compila o vendor (ver o bloco
        -- `files`). ImGuizmo e imgui-node-editor NAO voltam: nenhum arquivo
        -- deste projeto os toca, e sem o caminho de include um uso novo em
        -- `src/axe/` falha na hora, no arquivo culpado, em vez de so no link.
        "%{IncludeDir.Imgui}",
        "%{IncludeDir.assimp}",
        "%{IncludeDir.entt}",
        "%{IncludeDir.nlohmann}",
        "%{IncludeDir.stb}",
        "%{IncludeDir.Jolt}",
        "%{IncludeDir.miniaudio}",

    }
   libdirs
    {
        "src/vendor/assimp/build/lib/Release"
    }
    links
    {
        "simdjson",
        "spdlog",
        "GLFW",
        "opengl32",
        "Glad",
        -- B2.4: assimp SAIU daqui.
        --
        -- O runtime le apenas os formatos proprios (.axemesh, .axeskelbin,
        -- .axeclipbin). O importador de FBX virou ferramenta de autoria e
        -- mora em src/editor/axe_editor/import; quem o liga ao runtime e o
        -- AssetImportHooks, registrado no boot do editor.
        --
        -- Era o bloqueio B2 do PACKAGING_READINESS: um jogo empacotado nao
        -- carrega mais uma DLL de importacao de FBX que nunca usa.
        --
        -- Se esta linha voltar, alguem religou o runtime ao importador —
        -- procure por um include de mesh_loader/skeletal_mesh_loader dentro
        -- de src/axe.
        "Jolt"
    }

    defines
    {
       "FMT_HEADER_ONLY=1",
        "SIMDJSON_EXCEPTIONS=0",
        "AXE_BUILD_DLL",
        -- Esta DLL compila e EXPORTA o ImGui (core + backends GLFW/OpenGL3)
        -- para o editor consumir por dllimport. Ver a nota no bloco `files`
        -- sobre por que os backends nao podem morar no executavel.
        "IMGUI_API=__declspec(dllexport)",
        "IMGUI_DEFINE_MATH_OPERATORS"
    }
    
    dependson
    {
        "simdjson",
        "spdlog",
        "Jolt"
    }

    -- S0b — os dois `filter "files:src/vendor/..."` que moravam aqui foram
    -- removidos: eles configuravam o imgui-node-editor e o ImGuizmo, e nenhum
    -- dos dois e compilado neste projeto desde que a GUI saiu. Filtro que nao
    -- casa com arquivo nenhum e configuracao morta — e uma delas
    -- (`pchsource ""`) ja estava no KNOWN_LIMITATIONS como suspeita.
    --
    -- No projeto `editor` eles nao precisam existir: o
    -- `IMGUI_DEFINE_MATH_OPERATORS` ja e define do projeto inteiro, e nenhum
    -- projeto usa precompiled header.

    filter "system:windows"
        cppdialect "C++20"
        systemversion "latest"
        debugdir "%{cfg.targetdir}" 

        defines
        {
            "AXE_PLATFORM_WINDOWS",
            "AXE_BUILD_DLL"
        }

     
--    postbuildcommands
--     {
--         '{MKDIR} "%{wks.location}/bin/' .. outputdir .. '/editor"',
--         '{COPYFILE} "%{cfg.targetdir}/axe.dll" "%{wks.location}/bin/' .. outputdir .. '/editor/axe.dll"',
--         '{COPYFILE} "C:/msys64/ucrt64/bin/libassimp-6.dll" "%{cfg.targetdir}"'
--     }
        postbuildcommands
        {
            '{MKDIR} "%{wks.location}/bin/' .. outputdir .. '/editor" >nul 2>nul',
            '{COPYFILE} "%{cfg.targetdir}/axe.dll" "%{wks.location}/bin/' .. outputdir .. '/editor/axe.dll" >nul',

            -- PKG: a mesma copia para o alvo `game`.
            --
            -- Feita AQUI, e nao no postbuild do game, porque quem produz o
            -- axe.dll e este projeto: assim uma recompilacao do runtime
            -- atualiza os dois consumidores sem depender de o outro projeto
            -- ser reconstruido depois.
            '{MKDIR} "%{wks.location}/bin/' .. outputdir .. '/game" >nul 2>nul',
            '{COPYFILE} "%{cfg.targetdir}/axe.dll" "%{wks.location}/bin/' .. outputdir .. '/game/axe.dll" >nul',
            '{COPYDIR} "%{wks.location}src/editor/resources" "%{cfg.targetdir}/resources"',

            -- PKG8 — `resources/` TAMBEM ao lado do game.exe.
            --
            -- A linha acima copia para `%{cfg.targetdir}`, que neste projeto e
            -- `bin/<cfg>/axe` — a pasta do RUNTIME, nao a do jogo. O PKG5 dizia
            -- que os resources iam para `bin/<cfg>/game` e nunca foram: o
            -- empacotador procura `<BinariesDir>/resources` e avisava "no
            -- 'resources' folder next to the binaries - a scene using the
            -- default HDRI will render without a skybox".
            --
            -- La dentro esta o HDRI default (`quarry_04_puresky_2k.hdr`), que e
            -- asset da ENGINE e nao do projeto: uma cena que nunca escolheu HDRI
            -- aponta para ele por caminho relativo.
            '{COPYDIR} "%{wks.location}src/editor/resources" "%{wks.location}bin/' .. outputdir .. '/game/resources"',
            
                
        }

    filter "configurations:Debug"
        defines "AXE_DEBUG"
        runtime "Debug"
        symbols "On"

    filter "configurations:Release"
        defines "AXE_RELEASE"
        runtime "Release"
        optimize "On"

    filter "configurations:Dist"
        defines "AXE_DIST"
        runtime "Release"
        optimize "Full"

    filter {}

----------------EDITOR-----------------
project "editor"
    location "src/editor"
    kind "ConsoleApp"
    language "C++"
    targetname "editor"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "src/editor/**.hpp",
        "src/editor/**.cpp",

        -- S0b — o ImGuizmo veio do projeto `axe`, e o core do ImGui NAO.
        --
        -- O ImGuizmo so fala com o ImGui (nada de GLFW/Glad) e o unico
        -- consumidor dele — o `viewport_renderer` — mora aqui agora. Ja o
        -- core e os backends do ImGui ficaram na `axe.dll`: os backends
        -- precisam da MESMA copia de GLFW/Glad que criou a janela, e essa
        -- copia e a da DLL. Ver a nota longa no projeto `axe`.
        "src/vendor/imguizmo/ImGuizmo.h",
        "src/vendor/imguizmo/ImGuizmo.cpp",
        "src/vendor/imguizmo/ImZoomSlider.h",
        "src/vendor/imview-guizmo/ImViewGuizmo.h",   -- VIEW_GIZMO_V1

          "src/vendor/imgui-node-editor/imgui_node_editor.h",
        "src/vendor/imgui-node-editor/imgui_node_editor.cpp",
        "src/vendor/imgui-node-editor/imgui_node_editor_api.cpp",
        "src/vendor/imgui-node-editor/imgui_node_editor_internal.h",
        "src/vendor/imgui-node-editor/imgui_node_editor_internal.inl",
        "src/vendor/imgui-node-editor/imgui_canvas.h",
        "src/vendor/imgui-node-editor/imgui_canvas.cpp",
        "src/vendor/imgui-node-editor/imgui_bezier_math.h",
        "src/vendor/imgui-node-editor/imgui_bezier_math.inl",
        "src/vendor/imgui-node-editor/imgui_extra_math.h",
        "src/vendor/imgui-node-editor/imgui_extra_math.inl",
        "src/vendor/imgui-node-editor/crude_json.h",
        "src/vendor/imgui-node-editor/crude_json.cpp",
    }

    includedirs
    {
        "src",
        "src/editor",
        "%{IncludeDir.Imgui}",
        "%{IncludeDir.GLFW}",
        "%{IncludeDir.Glad}",
        "src/vendor/spdlog/include",
        "src/vendor/glm",
        "%{IncludeDir.ImGuizmo}",
        "%{IncludeDir.assimp}",
        "%{IncludeDir.entt}",
        "%{IncludeDir.nlohmann}",
         "%{IncludeDir.imguinodeeditor}",
        "%{IncludeDir.imviewguizmo}",   -- VIEW_GIZMO_V1
    }

    libdirs
    {
        "src/vendor/assimp/build/lib/Release"
    }


    links
    {
        "axe",
        "GLFW",
        "opengl32",
        "Glad",
        "assimp-vc145-mt"
    }

    defines
    {
        "AXE_PLATFORM_WINDOWS",
        "FMT_HEADER_ONLY=1",
        -- O ImGui (core + backends) e compilado e exportado pela `axe.dll`;
        -- aqui ele e importado. O ImGuizmo e o node-editor, que este projeto
        -- compila, usam essas declaracoes importadas — e e por isso que
        -- funciona: uma unica instancia do ImGui no processo.
        "IMGUI_API=__declspec(dllimport)",
        "IMGUI_DEFINE_MATH_OPERATORS",

    }

    dependson
    {
        "axe",
        -- O editor NAO linka o game — esta dependencia existe so pela ORDEM
        -- de build. Sem ela, compilar/rodar o editor (F5) reconstroi axe.dll
        -- e editor.exe e deixa o game.exe como estava; o empacotador entao
        -- copia um executavel velho ao lado de uma DLL nova. Como quase toda
        -- a engine vive na DLL, o pacote RODA — so que sem o que mudou em
        -- src/game/main.cpp. Foi assim que a captura do mouse do PKG5 ficou
        -- dois builds "aplicada mas sem efeito".
        --
        -- Custo: o projeto game (um .cpp) recompila junto com o editor.
        "game"
    }

    postbuildcommands
{
    -- Primeiro cria a pasta se não existir
    ('if not exist "%{wks.location}/bin/' .. outputdir .. '/editor" mkdir "%{wks.location}/bin/' .. outputdir .. '/editor"'),
    -- Depois copia o arquivo
    ('xcopy /Y /B "%{cfg.targetdir}/axe.dll" "%{wks.location}/bin/' .. outputdir .. '/editor\\"'),
     '{COPYDIR} "%{wks.location}src/editor/resources" "%{cfg.targetdir}/resources"'
}
    filter "system:windows"
        cppdialect "C++20"
        systemversion "latest"
        debugdir "%{cfg.targetdir}" 

    filter "configurations:Debug"
        defines "AXE_DEBUG"
        runtime "Debug"
        symbols "On"

    filter "configurations:Release"
        defines "AXE_RELEASE"
        runtime "Release"
        optimize "On"

    filter "configurations:Dist"
        defines "AXE_DIST"
        runtime "Release"
        optimize "Full"

    filter {}
----------------GAME-------------------
--
-- O alvo que o PACKAGING_READINESS chamava de "a peca que falta criar".
--
-- ── O QUE ELE NAO LINKA, E POR QUE ISSO E O PONTO ───────────────────────────
--
-- Sem assimp: o runtime le apenas os formatos cozidos (B2.1-B2.4). Se um dia
-- este projeto precisar do assimp de volta, e sinal de que alguma coisa voltou
-- a abrir FBX em tempo de execucao.
--
-- Sem ImGuizmo e sem imgui-node-editor: sao ferramentas de autoria.
--
-- O ImGui AINDA vem junto, e nao por escolha: o projeto `axe` lista os .cpp
-- dele explicitamente, entao ele esta dentro do axe.dll. E o B4 do
-- PACKAGING_READINESS, e ele nao IMPEDE o jogo de rodar — so faz o jogo
-- carregar codigo de GUI que nunca executa.
project "game"
    location "src/game"
    kind "ConsoleApp"
    language "C++"
    targetname "game"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "src/game/**.hpp",
        "src/game/**.cpp",
    }

    includedirs
    {
        "src",
        "%{IncludeDir.GLFW}",
        "%{IncludeDir.Glad}",
        "src/vendor/spdlog/include",
        "src/vendor/glm",
        "%{IncludeDir.entt}",
        "%{IncludeDir.nlohmann}",
        -- S0b — `IncludeDir.Imgui` saiu tambem daqui. O comentario antigo
        -- dizia "so porque axe.dll expoe headers que o alcancam", e isso
        -- deixou de ser verdade: nenhum header do runtime alcanca imgui.
    }

    links
    {
        "axe",
        "GLFW",
        "opengl32",
        "Glad",
    }

    defines
    {
        "AXE_PLATFORM_WINDOWS",
        "FMT_HEADER_ONLY=1",
        -- S0b — o jogo nunca teve imgui e agora nem o define fantasma: nada
        -- em `src/game/` ou em `axe.dll` toca na GUI.
        "IMGUI_DEFINE_MATH_OPERATORS",
    }

    dependson
    {
        "axe"
    }

    -- Sem postbuild.
    --
    -- A primeira versao tinha um `if not exist ... mkdir` com o caminho
    -- montado a mao, e ele falhava (MSB3073): `%{wks.location}` ja termina em
    -- barra invertida, e concatenar `/bin/...` produzia `AXE\/bin/...` — que o
    -- `mkdir` do cmd recusa, porque ele nao aceita barra normal.
    --
    -- O postbuild do `editor` tem o mesmo defeito e nao falha por acidente: a
    -- pasta dele ja existe, entao o `if not exist` nunca chega a rodar o
    -- `mkdir`. A do `game` era nova.
    --
    -- Quem copia o axe.dll para ca agora e o postbuild do projeto `axe`, com os
    -- tokens {MKDIR}/{COPYFILE} do premake — que geram o separador certo para a
    -- plataforma em vez de depender de concatenacao de string.

    filter "system:windows"
        cppdialect "C++20"
        systemversion "latest"
        debugdir "%{cfg.targetdir}"

    filter "configurations:Debug"
        defines "AXE_DEBUG"
        runtime "Debug"
        symbols "On"

    filter "configurations:Release"
        defines "AXE_RELEASE"
        runtime "Release"
        optimize "On"

    filter "configurations:Dist"
        defines "AXE_DIST"
        runtime "Release"
        optimize "Full"

    filter {}