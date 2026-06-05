project "Jolt"
    location "."
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"

    targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
    objdir    ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "Jolt/**.cpp",
        "Jolt/**.h",
    }

    includedirs
    {
        ".",
        "%{wks.location}/src"  -- para encontrar axe/physics/jolt_config.hpp
    }

    -- Forceincludes garante que jolt_config.hpp e incluido antes de tudo
    -- mesmo sem modificar os .cpp do Jolt
    forceincludes { "axe/physics/jolt_config.hpp" }

    -- defines base (jolt_config.hpp cuida do resto)
    defines {}

    filter "system:windows"
        systemversion "latest"

    filter "configurations:Debug"
        runtime "Debug"
        symbols "On"
        optimize "Off"

    filter "configurations:Release"
        runtime "Release"
        optimize "Speed"

    filter "configurations:Dist"
        runtime "Release"
        optimize "Full"

    filter {}