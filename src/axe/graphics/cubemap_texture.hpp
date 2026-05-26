#pragma once
#include "axe/core/types.hpp"
#include <memory>
#include <string>
#include <cstdint>

namespace axe
{
    class AXE_API CubemapTexture
    {
    public:
        virtual ~CubemapTexture() = default;

        virtual void Bind(uint32_t slot = 0) const = 0;

        // IBL
        virtual void BindIrradiance(uint32_t slot) const = 0;
        virtual void BindPrefiltered(uint32_t slot) const = 0;
        virtual void BindBRDFLut(uint32_t slot) const = 0;
        virtual bool HasIBL() const = 0;

        virtual uint32_t GetRendererID() const = 0;
        virtual bool     IsLoaded()      const = 0;

        static std::shared_ptr<CubemapTexture> CreateFromHDRI(const std::string& filepath);
    };
}