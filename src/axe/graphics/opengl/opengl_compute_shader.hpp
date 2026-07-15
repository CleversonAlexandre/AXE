#pragma once
#include "axe/core/types.hpp"
#include "axe/graphics/compute_shader.hpp"

#include <unordered_map>

namespace axe
{
	class AXE_API OpenGLComputeShader final : public ComputeShader
	{
	public:
		explicit OpenGLComputeShader(const std::string& source);
		~OpenGLComputeShader() override;

		void Bind() const override;
		void Unbind() const override;

		void Dispatch(std::uint32_t groupsX,
			std::uint32_t groupsY = 1,
			std::uint32_t groupsZ = 1) const override;

		void BarrierForVertexRead() const override;

		void SetInt(const std::string& name, int value) override;
		void SetUint(const std::string& name, std::uint32_t value) override;

		bool IsValid() const { return m_RendererID != 0; }

		// GL >= 4.3? Fica aqui (e não no compute_shader.cpp) pra que o
		// arquivo de factory continue sem incluir glad — só o backend
		// graphics/opengl/ toca GL, como no resto do engine.
		static bool IsComputeAvailable();

	private:
		int GetUniformLocation(const std::string& name);

		unsigned int m_RendererID = 0;

		// Cache de locations — glGetUniformLocation por frame por
		// personagem é custo de driver à toa.
		std::unordered_map<std::string, int> m_UniformCache;
	};

} // namespace axe