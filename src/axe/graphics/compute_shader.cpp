#include "axe/graphics/compute_shader.hpp"
#include "axe/graphics/opengl/opengl_compute_shader.hpp"

namespace axe
{
	std::shared_ptr<ComputeShader> ComputeShader::Create(const std::string& computeSource)
	{
		auto shader = std::make_shared<OpenGLComputeShader>(computeSource);

		// Compilação falhou — devolve nullptr em vez de um objeto morto,
		// pra que o SkinningPass consiga detectar e cair no fallback.
		if (!shader->IsValid())
			return nullptr;

		return shader;
	}

	bool ComputeShader::IsSupported()
	{
		return OpenGLComputeShader::IsComputeAvailable();
	}

} // namespace axe