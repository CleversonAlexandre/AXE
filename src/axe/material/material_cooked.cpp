// material_cooked.cpp — ver o header para o formato e o porque.

#include "material_cooked.hpp"
#include "material.hpp"

#include "axe/asset/asset_database.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/graphics/texture.hpp"
#include "axe/log/log.hpp"

#include <nlohmann/json.hpp>
#include <fstream>

namespace axe
{
    using json = nlohmann::json;

    const char* CookedMaterial::DomainName(CookedMaterialDomain domain)
    {
        switch (domain)
        {
        case CookedMaterialDomain::LightFunction: return "light_function";
        case CookedMaterialDomain::Particle:      return "particle";
        case CookedMaterialDomain::PostProcess:   return "post_process";
        case CookedMaterialDomain::Surface:
        default:                                  return "surface";
        }
    }

    namespace
    {
        // String -> dominio. Desconhecido cai em Surface E devolve false, para o
        // chamador poder avisar: um dominio que este runtime nao conhece
        // significa cozido gravado por um editor mais novo, e adivinhar o
        // conteudo de um shader e o tipo de erro que aparece como imagem
        // estranha, nunca como falha.
        bool DomainFromName(const std::string& name, CookedMaterialDomain& out)
        {
            if (name == "surface") { out = CookedMaterialDomain::Surface;       return true; }
            if (name == "light_function") { out = CookedMaterialDomain::LightFunction; return true; }
            if (name == "particle") { out = CookedMaterialDomain::Particle;      return true; }
            if (name == "post_process") { out = CookedMaterialDomain::PostProcess;   return true; }

            out = CookedMaterialDomain::Surface;
            return false;
        }

        // Resolve os samplers de um cozido: UUID -> AssetDatabase -> Texture2D.
        //
        // Compartilhado pelo caminho de Material (superficie) e pelo de
        // shader+samplers (luz/particula) — a regra de "textura ausente e AVISO,
        // nao erro" precisa ser uma so, senao um dos dois caminhos degrada de
        // um jeito e o outro de outro.
        std::map<std::string, std::shared_ptr<Texture2D>> ResolveSamplers(
            const std::map<std::string, std::string>& uuids)
        {
            std::map<std::string, std::shared_ptr<Texture2D>> out;

            for (const auto& [samplerName, uuid] : uuids)
            {
                if (uuid.empty()) continue;

                const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);
                std::error_code ec;

                if (!record || !std::filesystem::exists(record->FilePath, ec))
                {
                    AXE_CORE_WARN("CookedMaterial: textura '{}' (sampler '{}') nao encontrada.",
                        uuid, samplerName);
                    continue;
                }

                if (auto texture = Texture2D::Create(record->FilePath.string()))
                    out[samplerName] = texture;
            }

            return out;
        }
    }

    std::filesystem::path CookedMaterial::PathFor(const std::filesystem::path& materialPath)
    {
        // SUBSTITUI a extensao ("X.axemat" -> "X.axeshader"), como os cozidos
        // de malha — e ao contrario do `.axemeta`, que ANEXA. Mesma convencao
        // do `.axegraph`, que e o outro irmao do `.axemat`.
        std::filesystem::path p = materialPath;
        p.replace_extension(".axeshader");
        return p;
    }

    bool CookedMaterial::Save(const std::filesystem::path& path, const CookedMaterialData& data)
    {
        // Um cozido sem fragment shader nao renderiza nada — gravar isso so
        // esconderia um erro de compilacao do grafo atras de um arquivo valido.
        if (data.VertexShader.empty() || data.FragmentShader.empty())
        {
            AXE_CORE_WARN("CookedMaterial: shader vazio, '{}' nao foi gravado.", path.string());
            return false;
        }

        json root;
        root["version"] = 1;
        root["domain"] = DomainName(data.Domain);   // PKG9
        root["vertex"] = data.VertexShader;
        root["fragment"] = data.FragmentShader;
        root["geometry_fragment"] = data.GeometryFragShader;

        json samplers = json::object();
        for (const auto& [name, uuid] : data.SamplerTextureUUIDs)
            samplers[name] = uuid;
        root["samplers"] = samplers;

        root["albedo_sampler"] = data.AlbedoSamplerName;
        root["normal_sampler"] = data.NormalSamplerName;
        root["is_transparent"] = data.IsTransparent;
        root["two_sided"] = data.TwoSided;              // TWO_SIDED_V1
        root["uses_scene_height"] = data.UsesSceneHeight;   // SCENE_HEIGHT_V6
        root["is_masked"] = data.IsMasked;
        root["alpha_cutoff"] = data.AlphaCutoff;
        root["baked_emissive"] = { data.BakedEmissive.r, data.BakedEmissive.g, data.BakedEmissive.b };

        std::ofstream file(path);
        if (!file.is_open())
        {
            AXE_CORE_ERROR("CookedMaterial: falha ao gravar '{}'.", path.string());
            return false;
        }

        // dump(1, '\t') e nao dump(4): os shaders sao strings de milhares de
        // caracteres numa linha so — indentacao larga so incha o arquivo.
        file << root.dump(1, '\t');
        return true;
    }

    bool CookedMaterial::Load(const std::filesystem::path& path, CookedMaterialData& outData)
    {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            return false;

        std::ifstream file(path);
        json root;
        try { root = json::parse(file); }
        catch (const json::exception& e)
        {
            AXE_CORE_ERROR("CookedMaterial: erro ao parsear '{}': {}", path.string(), e.what());
            return false;
        }

        // Versao desconhecida = editor mais novo que este runtime. Melhor
        // cair no default (e logar) do que interpretar campos errado.
        const int version = root.value("version", 0);
        if (version != 1)
        {
            AXE_CORE_WARN("CookedMaterial: '{}' tem versao {} (esperada 1) - ignorado.",
                path.string(), version);
            return false;
        }

        // PKG9 — cozidos do B4 nao tinham o campo; sem ele o arquivo so podia
        // ser de superficie, que e exatamente o default.
        {
            const std::string domainName = root.value("domain", std::string("surface"));

            if (!DomainFromName(domainName, outData.Domain))
                AXE_CORE_WARN("CookedMaterial: dominio '{}' desconhecido em '{}' - "
                    "tratando como surface.", domainName, path.string());
        }

        outData.VertexShader = root.value("vertex", std::string());
        outData.FragmentShader = root.value("fragment", std::string());
        outData.GeometryFragShader = root.value("geometry_fragment", std::string());

        outData.SamplerTextureUUIDs.clear();
        if (root.contains("samplers") && root["samplers"].is_object())
            for (auto it = root["samplers"].begin(); it != root["samplers"].end(); ++it)
                outData.SamplerTextureUUIDs[it.key()] = it.value().get<std::string>();

        outData.AlbedoSamplerName = root.value("albedo_sampler", std::string());
        outData.NormalSamplerName = root.value("normal_sampler", std::string());
        outData.IsTransparent = root.value("is_transparent", false);
        outData.TwoSided = root.value("two_sided", false);   // TWO_SIDED_V1
        outData.UsesSceneHeight = root.value("uses_scene_height", false);   // SCENE_HEIGHT_V6
        outData.IsMasked = root.value("is_masked", false);
        outData.AlphaCutoff = root.value("alpha_cutoff", 0.5f);

        if (root.contains("baked_emissive") && root["baked_emissive"].size() == 3)
            outData.BakedEmissive = { root["baked_emissive"][0].get<float>(),
                                      root["baked_emissive"][1].get<float>(),
                                      root["baked_emissive"][2].get<float>() };

        return !outData.VertexShader.empty() && !outData.FragmentShader.empty();
    }

    bool CookedMaterial::ApplyToMaterial(const CookedMaterialData& data, Material& material)
    {
        // ── Shaders ──────────────────────────────────────────────────────────
        //
        // Shader::Create pode lancar (fonte invalido, driver) — o mesmo motivo
        // pelo qual CompileAndApply envolve em try. Um cozido corrompido nao
        // pode derrubar o jogo inteiro no load da cena.
        std::shared_ptr<Shader> forwardShader;
        try { forwardShader = Shader::Create(data.VertexShader, data.FragmentShader); }
        catch (const std::exception& e)
        {
            AXE_CORE_ERROR("CookedMaterial: shader forward falhou: {}", e.what());
            return false;
        }

        if (!forwardShader)
        {
            AXE_CORE_ERROR("CookedMaterial: Shader::Create retornou null.");
            return false;
        }

        material.SetShader(forwardShader);
        material.UsePBR = true;

        if (!data.GeometryFragShader.empty())
        {
            try
            {
                auto geometryShader = Shader::Create(data.VertexShader, data.GeometryFragShader);
                if (geometryShader)
                    material.SetGeometryShader(geometryShader);
                else
                    AXE_CORE_WARN("CookedMaterial: geometry shader retornou null.");
            }
            catch (const std::exception& e)
            {
                // Sem o passe deferred o material ainda renderiza no forward —
                // degrada, nao derruba.
                AXE_CORE_WARN("CookedMaterial: geometry shader falhou: {}", e.what());
            }
        }

        // ── Texturas ─────────────────────────────────────────────────────────
        //
        // UUID -> AssetDatabase -> Texture2D. Textura ausente e AVISO e nao
        // erro: o pacote pode ter uma referencia quebrada (o packager ja avisa
        // na geracao), e um sampler sem textura renderiza com o default do
        // slot — visivel e diagnosticavel, como os outros buracos de asset.
        material.SamplerTextures = ResolveSamplers(data.SamplerTextureUUIDs);

        // Os slots fixos apontam para as MESMAS instancias do mapa — como no
        // callback do editor, que copia result.AlbedoTexture depois de preencher
        // result.SamplerTextures com o mesmo shared_ptr.
        for (const auto& [samplerName, texture] : material.SamplerTextures)
        {
            if (samplerName == data.AlbedoSamplerName)
            {
                material.AlbedoMap = texture;
                material.AlbedoUUID = data.SamplerTextureUUIDs.at(samplerName);
            }
            if (samplerName == data.NormalSamplerName)
            {
                material.NormalMap = texture;
                material.NormalUUID = data.SamplerTextureUUIDs.at(samplerName);
            }
        }

        material.IsTransparent = data.IsTransparent;
        material.TwoSided = data.TwoSided;                   // TWO_SIDED_V1
        material.UsesSceneHeight = data.UsesSceneHeight;     // SCENE_HEIGHT_V6
        material.BakedEmissive = data.BakedEmissive;

        return true;
    }

    bool CookedMaterial::LoadAndApply(const std::string& materialAssetUUID, Material& material)
    {
        const AssetRecord* record = AssetDatabase::Get().GetByUUID(materialAssetUUID);
        if (!record)
            return false;

        CookedMaterialData data;
        if (!Load(PathFor(record->FilePath), data))
            return false;

        // Um cozido de luz ou de particula aplicado num Material de superficie
        // compilaria (e um shader valido) e desenharia errado. Recusar aqui e o
        // que transforma "a parede ficou estranha" em uma linha de log.
        if (data.Domain != CookedMaterialDomain::Surface)
        {
            AXE_CORE_WARN("CookedMaterial: '{}' foi cozido no dominio '{}', mas esta "
                "sendo usado como material de superficie.",
                materialAssetUUID, DomainName(data.Domain));
            return false;
        }

        return ApplyToMaterial(data, material);
    }

    bool CookedMaterial::LoadShaderAndSamplers(const std::string& materialAssetUUID,
        CookedMaterialDomain expectedDomain,
        std::shared_ptr<Shader>& outShader,
        std::map<std::string, std::shared_ptr<Texture2D>>& outSamplers)
    {
        const AssetRecord* record = AssetDatabase::Get().GetByUUID(materialAssetUUID);
        if (!record)
            return false;

        CookedMaterialData data;
        if (!Load(PathFor(record->FilePath), data))
            return false;

        if (data.Domain != expectedDomain)
        {
            AXE_CORE_WARN("CookedMaterial: '{}' foi cozido no dominio '{}', mas quem pediu "
                "espera '{}'.", materialAssetUUID, DomainName(data.Domain),
                DomainName(expectedDomain));
            return false;
        }

        // Estes dois dominios nao tem passe deferred: light function e avaliada
        // num quad minimo e particula desenha billboard no forward. Um
        // `geometry_fragment` aqui seria cozido gravado errado — ignorar em
        // silencio esconderia isso.
        if (!data.GeometryFragShader.empty())
            AXE_CORE_WARN("CookedMaterial: '{}' ({}) tem geometry shader cozido - ignorado.",
                materialAssetUUID, DomainName(data.Domain));

        std::shared_ptr<Shader> shader;
        try { shader = Shader::Create(data.VertexShader, data.FragmentShader); }
        catch (const std::exception& e)
        {
            AXE_CORE_ERROR("CookedMaterial: shader '{}' ({}) falhou: {}",
                materialAssetUUID, DomainName(data.Domain), e.what());
            return false;
        }

        if (!shader)
        {
            AXE_CORE_ERROR("CookedMaterial: Shader::Create retornou null para '{}' ({}).",
                materialAssetUUID, DomainName(data.Domain));
            return false;
        }

        outShader = shader;
        outSamplers = ResolveSamplers(data.SamplerTextureUUIDs);
        return true;
    }

} // namespace axe