#include "axe/graphics/texture.hpp"
#include "axe/graphics/opengl/opengl_texture.hpp"
#include <string>
#include <unordered_map>

namespace axe
{

	std::shared_ptr<Texture2D> Texture2D::Create(std::uint32_t width, std::uint32_t height)
	{
		return std::make_shared<OpenGLTexture2D>(width, height);
	}

	// Cache por filepath — evita decodificar a imagem (stbi_load) e recriar
	// a textura na GPU toda vez que o mesmo arquivo é referenciado (ex:
	// vários materiais usando a mesma textura, ou a cena sendo
	// (re)serializada ao abrir/Play/Stop). Texture2D só expõe métodos de
	// leitura (Bind/Unbind/GetWidth/GetHeight/...), então é seguro
	// compartilhar a mesma instância entre N donos via shared_ptr.
	static std::unordered_map<std::string, std::shared_ptr<Texture2D>> s_TextureCache;

	void Texture2D::ClearCache()
	{
		s_TextureCache.clear();
	}

	void Texture2D::InvalidateCache(const std::string& filepath)
	{
		s_TextureCache.erase(filepath);
	}

	std::shared_ptr<Texture2D> Texture2D::Create(const std::string& filepath)
	{
		auto cached = s_TextureCache.find(filepath);
		if (cached != s_TextureCache.end())
			return cached->second;

		auto tex = std::make_shared<OpenGLTexture2D>(filepath);
		if (tex->IsLoaded())
			s_TextureCache[filepath] = tex;
		return tex;
	}

} // namespace axe