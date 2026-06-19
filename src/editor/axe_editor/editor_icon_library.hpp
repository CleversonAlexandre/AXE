#pragma once
#include "axe/graphics/texture.hpp"
#include <memory>

namespace axe
{
	class EditorIconLibrary
	{
	public:
		static EditorIconLibrary& Get();

		void Load(const std::string& resourcesPath);

		std::shared_ptr<Texture2D> GetMesh() const { return m_IconMesh; }
		std::shared_ptr<Texture2D> GetTexture() const { return m_IconTexture; }
		std::shared_ptr<Texture2D> GetScene() const { return m_IconScene; }
		std::shared_ptr<Texture2D> GetFolder() const { return m_IconFolder; }
		std::shared_ptr<Texture2D> GetScript()        const { return m_IconScript; }
		std::shared_ptr<Texture2D> GetScriptEntity()  const { return m_IconScriptEntity; }
		std::shared_ptr<Texture2D> GetScriptAgent()   const { return m_IconScriptAgent; }
		std::shared_ptr<Texture2D> GetScriptCharacter() const { return m_IconScriptCharacter; }
		std::shared_ptr<Texture2D> GetScriptStatic()  const { return m_IconScriptStatic; }
		std::shared_ptr<Texture2D> GetScriptTrigger() const { return m_IconScriptTrigger; }

		// Retorna ícone de script pelo ScriptClassType string
		std::shared_ptr<Texture2D> GetScriptForClass(const std::string& classType) const;
		std::shared_ptr<Texture2D> GetAudio() const { return m_IconAudio; }
		std::shared_ptr<Texture2D> GetMaterial() const { return m_Material; }
		std::shared_ptr<Texture2D> GetDirectionalLight() const { return m_IconDirectionalLight; }
		std::shared_ptr<Texture2D> GetPointLight()       const { return m_IconPointLight; }
		std::shared_ptr<Texture2D> GetPostProcess()      const { return m_IconPostProcess; }
		std::shared_ptr<Texture2D> GetEnvironment()      const { return m_IconEnvironment; }

		// Ícones de componentes (Script Editor / Inspector)
		std::shared_ptr<Texture2D> GetRigidbody()           const { return m_IconRigidbody; }
		std::shared_ptr<Texture2D> GetCollider()            const { return m_IconCollider; }
		std::shared_ptr<Texture2D> GetCharacterController() const { return m_IconCharacterController; }
		std::shared_ptr<Texture2D> GetSpringArm()           const { return m_IconSpringArm; }
		std::shared_ptr<Texture2D> GetCamera()              const { return m_IconCamera; }

		std::shared_ptr<Texture2D> GetSave()       const { return m_IconSave; }
		std::shared_ptr<Texture2D> GetUndo()       const { return m_IconUndo; }
		std::shared_ptr<Texture2D> GetRedo()       const { return m_IconRedo; }
		std::shared_ptr<Texture2D> GetCompile()    const { return m_IconCompile; }
		std::shared_ptr<Texture2D> GetFit()        const { return m_IconFit; }
		std::shared_ptr<Texture2D> GetAdd()        const { return m_IconAdd; }
		std::shared_ptr<Texture2D> GetLockClosed() const { return m_IconLockClosed; }
		std::shared_ptr<Texture2D> GetLockOpen()   const { return m_IconLockOpen; }





		//Retorna icone baseado no tipo de asset
		std::shared_ptr<Texture2D> GetForType(const std::string& type) const;
	private:



		bool IsLoaded() const { return m_Loaded; }

	private:
		EditorIconLibrary() = default;

		std::shared_ptr<Texture2D> m_IconMesh;
		std::shared_ptr<Texture2D> m_IconTexture;
		std::shared_ptr<Texture2D> m_IconScene;
		std::shared_ptr<Texture2D> m_IconFolder;
		std::shared_ptr<Texture2D> m_IconScript;
		std::shared_ptr<Texture2D> m_IconScriptEntity;
		std::shared_ptr<Texture2D> m_IconScriptAgent;
		std::shared_ptr<Texture2D> m_IconScriptCharacter;
		std::shared_ptr<Texture2D> m_IconScriptStatic;
		std::shared_ptr<Texture2D> m_IconScriptTrigger;
		std::shared_ptr<Texture2D> m_IconAudio;
		std::shared_ptr<Texture2D> m_Material;
		std::shared_ptr<Texture2D> m_IconDirectionalLight;
		std::shared_ptr<Texture2D> m_IconPointLight;
		std::shared_ptr<Texture2D> m_IconPostProcess;
		std::shared_ptr<Texture2D> m_IconEnvironment;

		// Ícones de componentes
		std::shared_ptr<Texture2D> m_IconRigidbody;
		std::shared_ptr<Texture2D> m_IconCollider;
		std::shared_ptr<Texture2D> m_IconCharacterController;
		std::shared_ptr<Texture2D> m_IconSpringArm;
		std::shared_ptr<Texture2D> m_IconCamera;

		std::shared_ptr<Texture2D> m_IconSave;
		std::shared_ptr<Texture2D> m_IconUndo;
		std::shared_ptr<Texture2D> m_IconRedo;
		std::shared_ptr<Texture2D> m_IconCompile;
		std::shared_ptr<Texture2D> m_IconFit;
		std::shared_ptr<Texture2D> m_IconAdd;
		std::shared_ptr<Texture2D> m_IconLockClosed;
		std::shared_ptr<Texture2D> m_IconLockOpen;

		bool m_Loaded = true;

	};
}//namespace axe