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
    buildoptions { "/utf-8" }

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



include "src/vendor/simdjson/singleheader"
include "src/vendor/spdlog"
include "src/vendor/GLFW"
include "src/vendor/Glad"
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

        -- ImGui do vendor
        "src/vendor/imgui/imgui.cpp",
        "src/vendor/imgui/imgui_draw.cpp",
        "src/vendor/imgui/imgui_tables.cpp",
        "src/vendor/imgui/imgui_widgets.cpp",
        "src/vendor/imgui/imgui_demo.cpp",
        "src/vendor/imgui/backends/imgui_impl_opengl3.cpp",
        "src/vendor/imgui/backends/imgui_impl_glfw.cpp",
        "src/vendor/imguizmo/ImGuizmo.h",
        "src/vendor/imguizmo/ImGuizmo.cpp",
        "src/vendor/imguizmo/ImZoomSlider.h",
    
    }

    removefiles
    {
        "src/axe/**/imgui_impl_glfw.cpp",
        "src/axe/**/imgui_impl_opengl3.cpp",
        "src/axe/**/imgui.cpp",
        "src/axe/**/imgui_draw.cpp",
        "src/axe/**/imgui_tables.cpp",
        "src/axe/**/imgui_widgets.cpp",
        "src/axe/**/imgui_demo.cpp",
    }

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
        "%{IncludeDir.Imgui}",
        "%{IncludeDir.ImGuizmo}",
        "%{IncludeDir.assimp}",
        "%{IncludeDir.entt}",
        "%{IncludeDir.nlohmann}",
        "%{IncludeDir.stb}",
         "%{IncludeDir.imguinodeeditor}",

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
        "assimp-vc145-mt"
    }

    defines
    {
       "FMT_HEADER_ONLY=1",
        "SIMDJSON_EXCEPTIONS=0",
        "AXE_BUILD_DLL",
        "IMGUI_API=__declspec(dllexport)",
        "IMGUI_DEFINE_MATH_OPERATORS" 
    }
    
    dependson
    {
        "simdjson",
        "spdlog"
     
    }

    filter "files:src/vendor/imgui-node-editor/**.cpp"
    defines { "IMGUI_DEFINE_MATH_OPERATORS" }
    filter {}

     filter "files:src/vendor/imguizmo/**.cpp"
        pchheader "None"  -- 
        pchsource "" 

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
            '{COPYDIR} "%{wks.location}src/editor/resources" "%{cfg.targetdir}/resources"',
            
                
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
        "IMGUI_API=__declspec(dllimport)",
        "IMGUI_DEFINE_MATH_OPERATORS",
        
    }

    dependson
    {
        "axe"
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
