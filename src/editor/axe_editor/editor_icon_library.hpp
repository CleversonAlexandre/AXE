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
		std::shared_ptr<Texture2D> GetScript() const { return m_IconScript; }
		std::shared_ptr<Texture2D> GetAudio() const { return m_IconAudio; }
		std::shared_ptr<Texture2D> GetMaterial() const { return m_Material; }
		std::shared_ptr<Texture2D> GetDirectionalLight() const { return m_IconDirectionalLight; }
		std::shared_ptr<Texture2D> GetPointLight()       const { return m_IconPointLight; }
		std::shared_ptr<Texture2D> GetPostProcess()      const { return m_IconPostProcess; }
		std::shared_ptr<Texture2D> GetEnvironment()      const { return m_IconEnvironment; }

		std::shared_ptr<Texture2D> GetSave()       const { return m_IconSave; }
		std::shared_ptr<Texture2D> GetUndo()       const { return m_IconUndo; }
		std::shared_ptr<Texture2D> GetRedo()       const { return m_IconRedo; }
		std::shared_ptr<Texture2D> GetCompile()    const { return m_IconCompile; }





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
		std::shared_ptr<Texture2D> m_IconAudio;
		std::shared_ptr<Texture2D> m_Material;
		std::shared_ptr<Texture2D> m_IconDirectionalLight;
		std::shared_ptr<Texture2D> m_IconPointLight;
		std::shared_ptr<Texture2D> m_IconPostProcess;
		std::shared_ptr<Texture2D> m_IconEnvironment;

		std::shared_ptr<Texture2D> m_IconSave;
		std::shared_ptr<Texture2D> m_IconUndo;
		std::shared_ptr<Texture2D> m_IconRedo;
		std::shared_ptr<Texture2D> m_IconCompile;

		bool m_Loaded = true;

	};
}//namespace axe