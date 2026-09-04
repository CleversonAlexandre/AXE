#include "material_thumbnail_renderer.hpp"
#include "axe/mesh/mesh_factory.hpp"
#include "axe/mesh/primitive_uuid.hpp"
#include "axe/scene/components.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/log/log.hpp"
#include "axe/scene/scene_serializer.hpp"
#include "axe/graphics/shader.hpp"
#include "node_graph/material_graph.hpp"
#include "editor/axe_editor/material/material_compiler.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

#include "axe/asset/asset_database.hpp"
#include "editor/axe_editor/asset/asset_spawn_defaults.hpp"   // THUMB_MATERIAL_V1
#include "axe/asset/asset.hpp"

namespace axe
{
    void MaterialThumbnailRenderer::Initialize()
    {
        m_Renderer = std::make_unique<ViewportRenderer>();
        m_Renderer->Initialize();
        m_Renderer->GetSceneRenderer()->SetDeferredSupported(false);
        m_Renderer->SetPickingEnabled(false);

        m_Scene = std::make_unique<Scene>();
        auto& registry = m_Scene->GetRegistry();

        // Luz
        auto light = m_Scene->CreateEntity("Light");
        auto& lc = registry.emplace<LightComponent>(light);
        lc.Data = std::make_shared<DirectionalLight>();
        lc.Data->Direction = glm::vec3(0.0f, -1.0f, -1.0f);
        lc.Data->Color = glm::vec3(1.0f);
        lc.Data->Intensity = 3.0f;
        lc.Data->AmbientStrength = 0.3f;

        // Esfera
        m_SphereEntity = m_Scene->CreateEntity("Sphere");
        auto& mc = registry.emplace<MeshComponent>(m_SphereEntity);
        mc.Data = MeshFactory::CreateSphere();

        // Material padrão — cinza neutro sem shader compilado
        m_DefaultMaterial = std::make_shared<Material>(nullptr, "Default");
        m_DefaultMaterial->UsePBR = true;
        m_DefaultMaterial->Metallic = 0.0f;
        m_DefaultMaterial->Roughness = 0.5f;
        m_DefaultMaterial->Color = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);

        registry.emplace<MaterialComponent>(m_SphereEntity, m_DefaultMaterial);

        m_Renderer->SetScene(m_Scene.get());

        m_Environment = std::make_unique<SceneEnvironment>();
        // m_Environment->LoadHDRI("resources/quarry_04_puresky_2k.hdr");
        //m_Renderer->SetEnvironment(m_Environment.get());

        //AXE_CORE_INFO("MaterialThumbnailRenderer initialized.");
    }

    void MaterialThumbnailRenderer::Register(const std::string& uuid,
        const std::filesystem::path& filePath)
    {
        if (filePath.extension() != ".axemat") return;

        // Já está no cache — não recarrega
        if (m_Cache.count(uuid)) return;

        ThumbnailEntry entry;

        // Cria framebuffer
        FramebufferSpecification spec;
        spec.Width = k_ThumbnailSize;
        spec.Height = k_ThumbnailSize;
        entry.Framebuffer = Framebuffer::Create(spec);

        // ═══════════════════════════════════════════════════════════════════
        //  THUMB_MATERIAL_V1 — a rotina de material saiu daqui
        //
        //  ── ONDE ELA FOI PARAR, E POR QUE ─────────────────────────────────
        //
        //  As ~90 linhas que moravam aqui liam o `.axemat`, compilavam o
        //  shader e caçavam a textura andando do Material Output para tras.
        //  Eram a QUARTA copia da mesma rotina no editor — e a unica que fazia
        //  a busca de textura direito.
        //
        //  Ela foi promovida a AssetSpawnDefaults::ResolveMaterial, e as
        //  outras tres (spawn do BP, sincronizacao do BP, preview do Script
        //  Editor) passaram a chamar de la. Este bloco agora chama tambem.
        //
        //  Quatro copias significavam quatro respostas possiveis para
        //  "carregar um material", e o usuario via a diferenca sem ter como
        //  explicar: a thumbnail branca, a arma cinza no BP e o material certo
        //  ao aplicar no asset eram o MESMO defeito visto de tres lugares.
        //
        //  ── E O SILENCIO ACABOU ───────────────────────────────────────────
        //
        //  Todo caminho de falha aqui era mudo: `catch (...) {}`, um
        //  `if (result.Success)` sem else, um `if (shader)` sem else. Material
        //  cujo grafo nao compila mostrava uma esfera branca para sempre, sem
        //  uma linha de log. Agora o ResolveMaterial avisa, e o que sobra sem
        //  shader avisa aqui embaixo.
        // ═══════════════════════════════════════════════════════════════════
        entry.Material = AssetSpawnDefaults::ResolveMaterial(uuid);

        if (!entry.Material)
        {
            AXE_EDITOR_WARN("Thumbnail: o material '{}' nao carregou — a "
                "miniatura fica com a esfera padrao.",
                filePath.filename().string());

            entry.Material = m_DefaultMaterial;
        }
        else if (!entry.Material->GetShader())
        {
            // Sem shader a esfera sai chapada. Dizer QUAL material e o que
            // transforma "algumas miniaturas nao aparecem" numa lista de
            // materiais para abrir e recompilar.
            AXE_EDITOR_WARN("Thumbnail: o material '{}' carregou sem shader — "
                "o .axegraph nao compilou. Abra o material e compile.",
                filePath.filename().string());
        }

        entry.Dirty = true;
        entry.Rendered = false;
        m_Cache[uuid] = std::move(entry);
    }

    uint32_t MaterialThumbnailRenderer::GetThumbnail(const std::string& uuid)
    {
        auto it = m_Cache.find(uuid);
        if (it == m_Cache.end()) return 0;
        if (!it->second.Rendered) return 0;
        return it->second.Framebuffer->GetColorAttachmentRendererID();
    }

    void MaterialThumbnailRenderer::Invalidate(const std::string& uuid)
    {
        auto it = m_Cache.find(uuid);
        if (it == m_Cache.end()) return;

        // THUMB_MATERIAL_V1 — a mesma funcao do Register. Este bloco era a
        // QUINTA copia: a diferenca entre ele e o de cima era so quem chamava.
        if (auto mat = AssetSpawnDefaults::ResolveMaterial(uuid))
            it->second.Material = mat;

        it->second.Dirty = true;
        it->second.Rendered = false;
    }

    void MaterialThumbnailRenderer::RenderPending()
    {
        // Renderiza no máximo 1 thumbnail por frame para não travar
        for (auto& [uuid, entry] : m_Cache)
        {
            if (!entry.Dirty) continue;
            RenderThumbnail(uuid);
            entry.Dirty = false;
            entry.Rendered = true;
            break; // só um por frame
        }
    }

    void MaterialThumbnailRenderer::RenderThumbnail(const std::string& uuid)
    {
        auto& entry = m_Cache[uuid];
        auto& registry = m_Scene->GetRegistry();

        // Usa material compilado se tiver shader, senão usa o padrão
        auto mat = (entry.Material && entry.Material->GetShader())
            ? entry.Material : m_DefaultMaterial;

        if (registry.all_of<MaterialComponent>(m_SphereEntity))
            registry.get<MaterialComponent>(m_SphereEntity).Data = mat;

        m_Renderer->SetScene(m_Scene.get());
        m_Renderer->RenderToFramebuffer(
            *entry.Framebuffer,
            k_ThumbnailSize,
            k_ThumbnailSize,
            0.0f);
    }

} // namespace axe