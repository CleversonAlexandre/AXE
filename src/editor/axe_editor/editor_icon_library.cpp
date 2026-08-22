#include "editor_icon_library.hpp"
#include "axe/log/log.hpp"

#include <filesystem>
#include <initializer_list>

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
		m_IconScriptEntity = load("icon_script_entity.png");
		m_IconScriptAgent = load("icon_script_agent.png");
		m_IconScriptCharacter = load("icon_script_character.png");
		m_IconScriptStatic = load("icon_script_static.png");
		m_IconScriptTrigger = load("icon_script_trigger.png");
		m_IconAudio = load("icon_audio.png");

		// ── VARIANTES DE NOME ────────────────────────────────────────────
		//
		// A pasta mistura duas convencoes que ja convivem acima
		// (`icon_mesh.png` e `save.png`), entao tentar as duas e mais barato
		// que exigir que quem desenhou o icone acerte qual delas era.
		//
		// `loadAny` so avisa no log quando NENHUMA variante existe — e ai a
		// mensagem lista o que foi procurado, que e a informacao util.
		auto loadAny = [&](std::initializer_list<const char*> names)
			-> std::shared_ptr<Texture2D>
			{
				std::string tried;

				for (const char* n : names)
				{
					const std::string path = resourcesPath + "/icons/" + n;

					if (std::filesystem::exists(path))
						return Texture2D::Create(path);

					if (!tried.empty()) tried += ", ";
					tried += path;
				}

				AXE_CORE_WARN("EditorIconLibrary: icone nao encontrado (procurei: {})",
					tried);
				return nullptr;
			};

		m_IconSequence = loadAny({ "sequencer.png", "icon_sequencer.png", "sequence.png" });
		m_IconGameMode = loadAny({ "game_mode.png", "icon_game_mode.png", "gamemode.png" });
		m_IconParticle = loadAny({ "particle.png", "icon_particle.png", "particles.png" });
		m_Material = load("icon_material.png");
		m_IconSave = load("save.png");
		m_IconUndo = load("arrow_left.png");
		m_IconRedo = load("arrow_right.png");
		m_IconCompile = load("check.png");
		m_IconFit = load("icon_fit.png");
		m_IconAdd = load("icon_add.png");
		m_IconLockClosed = load("icon_lock_closed.png");
		m_IconLockOpen = load("icon_lock_open.png");

		m_IconDirectionalLight = load("directional_light.png");
		m_IconPointLight = load("point_light.png");
		// PostProcess e Environment usam ícones existentes como fallback
		// até termos arte dedicada
		m_IconPostProcess = load("camera.png");
		m_IconEnvironment = load("icon_scene.png"); // globo/ambiente

		// Ícones de componentes (Script Editor / Inspector)
		m_IconRigidbody = load("icon_rigidbody.png");
		m_IconCollider = load("icon_collider.png");
		m_IconCharacterController = load("icon_character_controller.png");
		m_IconSpringArm = load("icon_spring_arm.png");
		m_IconCamera = load("camera.png");

		m_Loaded = true;
		//AXE_CORE_INFO("EditorIconLibrary: ícones carregados.");
	}

	std::shared_ptr<Texture2D> EditorIconLibrary::GetForType(const std::string& type) const
	{
		if (type == "Mesh")        return m_IconMesh;
		if (type == "Texture")     return m_IconTexture;
		if (type == "Scene")       return m_IconScene;
		if (type == "Folder")      return m_IconFolder;
		if (type == "Script")      return m_IconScript;
		if (type == "Audio")       return m_IconAudio;
		if (type == "Material")    return m_Material;
		if (type == "PointLight")  return m_IconPointLight;
		if (type == "PostProcess") return m_IconPostProcess;
		if (type == "Environment") return m_IconEnvironment;
		return m_IconMesh;
	}
	std::shared_ptr<Texture2D> EditorIconLibrary::GetScriptForClass(const std::string& classType) const
	{
		if (classType == "Entity")       return m_IconScriptEntity ? m_IconScriptEntity : m_IconScript;
		if (classType == "Agent")        return m_IconScriptAgent ? m_IconScriptAgent : m_IconScript;
		if (classType == "Character")    return m_IconScriptCharacter ? m_IconScriptCharacter : m_IconScript;
		if (classType == "StaticObject") return m_IconScriptStatic ? m_IconScriptStatic : m_IconScript;
		if (classType == "Trigger")      return m_IconScriptTrigger ? m_IconScriptTrigger : m_IconScript;
		return m_IconScript; // fallback genérico
	}
}//namespace axe