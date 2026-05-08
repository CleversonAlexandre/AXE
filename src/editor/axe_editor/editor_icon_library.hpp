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

		//Retorna icone baseado no tipo de asset
		std::shared_ptr<Texture2D> GetForType(const std::string& type) const;

		bool IsLoaded() const { return m_Loaded; }

	private:
		EditorIconLibrary() = default;

		std::shared_ptr<Texture2D> m_IconMesh;
		std::shared_ptr<Texture2D> m_IconTexture;
		std::shared_ptr<Texture2D> m_IconScene;
		std::shared_ptr<Texture2D> m_IconFolder;
		std::shared_ptr<Texture2D> m_IconScript;
		std::shared_ptr<Texture2D> m_IconAudio;

		bool m_Loaded = true;

	};
}//namespace axe