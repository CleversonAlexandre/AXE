#include "editor_icon_library.hpp"
#include "axe/log/log.hpp"

#include <filesystem>

namespace axe
{
	EditorIconLibrary& EditorIconLibrary::Get()
	{
		static EditorIconLibrary instance;
		return instance;
	}

	void EditorIconLibrary::Load(const std::string& resourcesPath)
	{
		auto load = [&](const std::string& filename) -> std::shared_ptr<Texture2D>
		{
				std::string path = resourcesPath + "/icons/" + filename;
				if (!std::filesystem::exists(path))
				{
					AXE_CORE_WARN("EditorIconLibrary: Ícone não encontrado '{}'", path);
					return nullptr;
				}
				return Texture2D::Create(path);
		};

		m_IconMesh = load("icon_mesh.png");
		m_IconTexture = load("icon_texture.png");
		m_IconScene = load("icon_scene.png");
		m_IconFolder = load("icon_folder.png");
		m_IconScript = load("icon_script.png");
		m_IconAudio = load("icon_audio.png");
		m_Material = load("icon_material.png");
		m_IconSave = load("save.png");
		m_IconUndo = load("arrow_left.png");
		m_IconRedo = load("arrow_right.png");
		m_IconCompile = load("check.png");

		m_IconDirectionalLight = load("directional_light.png");

		m_Loaded = true;
		AXE_CORE_INFO("EditorIconLibrary: ícones carregados.");
	}

	std::shared_ptr<Texture2D> EditorIconLibrary::GetForType(const std::string& type) const
	{
		if (type == "Mesh") return m_IconMesh;
		if (type == "Texture") return m_IconTexture;
		if (type == "Scene") return m_IconScene;
		if (type == "Folder") return m_IconFolder;
		if (type == "Script") return m_IconScript;
		if (type == "Audio") return m_IconAudio;
		if (type == "Material") return m_Material;
		return m_IconMesh; 

	}
}//namespace axe