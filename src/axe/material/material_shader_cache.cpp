// material_shader_cache.cpp — ver o header para o porque.

#include "material_shader_cache.hpp"

#include "axe/graphics/shader.hpp"
#include "axe/graphics/texture.hpp"
#include "axe/scene/scene_serializer.hpp"
#include "axe/log/log.hpp"

#include <unordered_map>

namespace axe
{
    namespace
    {
        struct CacheEntry
        {
            std::shared_ptr<Shader> ShaderRef;
            std::map<std::string, std::shared_ptr<Texture2D>> Samplers;

            // Guardado para o caso de o MESMO uuid ser pedido em outro
            // dominio: isso e erro de autoria (um `.axemat` tem um dominio
            // so), e responder com o shader errado seria pior que recusar.
            CookedMaterialDomain Domain = CookedMaterialDomain::Surface;

            // Uma tentativa que falhou tambem e resposta, e vale guardar: sem
            // isto, um material sem cozido tentaria (e falharia) a CADA spawn,
            // que e o custo que este cache existe para evitar.
            bool Ok = false;
        };

        std::unordered_map<std::string, CacheEntry>& Entries()
        {
            static std::unordered_map<std::string, CacheEntry> s_Entries;
            return s_Entries;
        }
    }

    bool MaterialShaderCache::Resolve(const std::string& materialAssetUUID,
        CookedMaterialDomain domain,
        std::shared_ptr<Shader>& outShader,
        std::map<std::string, std::shared_ptr<Texture2D>>& outSamplers)
    {
        if (materialAssetUUID.empty())
            return false;

        auto& entries = Entries();

        if (auto it = entries.find(materialAssetUUID); it != entries.end())
        {
            if (it->second.Domain != domain)
            {
                AXE_CORE_WARN("MaterialShaderCache: '{}' ja foi resolvido no dominio '{}' e "
                    "agora foi pedido como '{}'.", materialAssetUUID,
                    CookedMaterial::DomainName(it->second.Domain),
                    CookedMaterial::DomainName(domain));
                return false;
            }

            if (!it->second.Ok)
                return false;

            outShader = it->second.ShaderRef;
            outSamplers = it->second.Samplers;
            return true;
        }

        CacheEntry entry;
        entry.Domain = domain;

        // Mesma regra de quem manda que o SceneSerializer usa. Ela vive aqui
        // duplicada de proposito? Nao: o SceneSerializer resolve UMA vez por
        // load e nao precisa de cache; este caminho e o de tempo de jogo. O que
        // NAO pode divergir e a ordem — callback primeiro, cozido depois — e e
        // por isso que ela esta escrita igual, e nao "melhorada" aqui.
        // ── POSTPROCESS_DOMAIN_V1 — BUG CORRIGIDO AQUI ───────────────────────
        //
        // Isto era um TERNARIO: LightFunction ? callbackDeLuz : callbackDeParticula.
        // Enquanto so existiam dois dominios, o "senao" era exato. Ao entrar um
        // TERCEIRO (PostProcess), ele caiu em silencio no ramo de particula: o
        // editor compilava o grafo de post process como BILLBOARD DE PARTICULA
        // e devolvia esse shader ao passe de tela cheia.
        //
        // O sintoma nao foi "nao funciona" — foi pior. Um shader que espera
        // atributos de particula, desenhado sobre o quad de tela, sem
        // u_SceneColor e sem escrever o alvo inteiro: a tela ficava com o lixo
        // do buffer nao inicializado, ou preta.
        //
        // Switch explicito, e `default` com callback NULO de proposito: um
        // dominio novo que alguem esqueca de ligar aqui cai no COZIDO — que e
        // conservador e correto — em vez de receber o shader de outro dominio.
        // Um ternario nao tem como avisar; um switch tem.
        SceneSerializer::LightMaterialRecompileCallback cb = nullptr;
        switch (domain)
        {
        case CookedMaterialDomain::LightFunction:
            cb = SceneSerializer::GetLightMaterialRecompileCallback();
            break;
        case CookedMaterialDomain::Particle:
            cb = SceneSerializer::GetParticleMaterialRecompileCallback();
            break;
        case CookedMaterialDomain::PostProcess:
            cb = SceneSerializer::GetPostProcessMaterialRecompileCallback();
            break;
        default:
            cb = nullptr;
            break;
        }

        if (cb)
            entry.Ok = cb(materialAssetUUID, entry.ShaderRef, entry.Samplers);
        else
            entry.Ok = CookedMaterial::LoadShaderAndSamplers(materialAssetUUID, domain,
                entry.ShaderRef, entry.Samplers);

        // Callback que devolve true sem shader existe (o do editor so preenche
        // se compilar) — sem esta checagem o cache guardaria um "sucesso" que
        // faria o renderer usar um shader nulo.
        if (entry.Ok && !entry.ShaderRef)
            entry.Ok = false;

        const bool ok = entry.Ok;
        auto& stored = entries.emplace(materialAssetUUID, std::move(entry)).first->second;

        if (!ok)
            return false;

        outShader = stored.ShaderRef;
        outSamplers = stored.Samplers;
        return true;
    }

    // POSTPROCESS_DOMAIN_V1 — ver a nota na declaracao.
    namespace
    {
        std::uint64_t& GenerationCounter()
        {
            static std::uint64_t s_Generation = 1;
            return s_Generation;
        }
    }

    std::uint64_t MaterialShaderCache::Generation()
    {
        return GenerationCounter();
    }

    void MaterialShaderCache::Invalidate(const std::string& materialAssetUUID)
    {
        Entries().erase(materialAssetUUID);
        ++GenerationCounter();
    }

    void MaterialShaderCache::Clear()
    {
        Entries().clear();
        ++GenerationCounter();
    }

} // namespace axe