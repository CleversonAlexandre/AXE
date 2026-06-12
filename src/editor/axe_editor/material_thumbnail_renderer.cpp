#include "material_thumbnail_renderer.hpp"
#include "axe/mesh/mesh_factory.hpp"
#include "axe/mesh/primitive_uuid.hpp"
#include "axe/scene/components.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/log/log.hpp"
#include "axe/scene/scene_serializer.hpp"
#include "axe/graphics/shader.hpp"
#include "node_graph/material_graph.hpp"
#include "axe/material/material_compiler.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

#include "axe/asset/asset_database.hpp"
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

        // Carrega o material
        auto matAsset = MaterialAsset::LoadFromFile(filePath);
        if (matAsset)
        {
            entry.Material = matAsset->GetMaterial();

            // Tenta compilar o shader do grafo
            auto graphPath = filePath;
            graphPath.replace_extension(".axegraph");

            if (std::filesystem::exists(graphPath))
            {
                std::ifstream file(graphPath);
                try
                {
                    nlohmann::json j = nlohmann::json::parse(file);
                    MaterialGraph graph;
                    graph.Deserialize(j);

                    auto result = MaterialCompiler::Compile(&graph);
                    if (result.Success)
                    {
                        auto shader = Shader::Create(
                            result.VertexShader, result.FragmentShader);
                        if (shader)
                        {
                            entry.Material->SetShader(shader);

                            // Transfere texturas na ordem correta
                            std::function<Node* (ed::PinId)> findTextureSample =
                                [&](ed::PinId startPin) -> Node*
                                {
                                    for (auto& n : graph.GetNodes())
                                        for (auto& outPin : n->Outputs)
                                        {
                                            if (outPin.ID != startPin) continue;
                                            if (n->Name == "Texture Sample") return n.get();
                                            for (auto& inPin : n->Inputs)
                                                for (auto& lnk : graph.GetLinks())
                                                {
                                                    if (lnk.EndPin != inPin.ID) continue;
                                                    Node* found = findTextureSample(lnk.StartPin);
                                                    if (found) return found;
                                                }
                                        }
                                    return nullptr;
                                };

                            Node* outputNode = nullptr;
                            for (auto& n : graph.GetNodes())
                                if (n->Name == "Material Output") { outputNode = n.get(); break; }

                            if (outputNode)
                            {
                                int slot = 0;
                                std::unordered_set<int> processed;

                                for (auto& inputPin : outputNode->Inputs)
                                {
                                    for (auto& lnk : graph.GetLinks())
                                    {
                                        if (lnk.EndPin != inputPin.ID) continue;
                                        Node* texNode = findTextureSample(lnk.StartPin);
                                        if (texNode && !processed.count(texNode->ID.Get())
                                            && texNode->Value.TextureVal)
                                        {
                                            if (slot == 0)
                                            {
                                                entry.Material->AlbedoMap = texNode->Value.TextureVal;
                                                entry.Material->AlbedoUUID = texNode->Value.TextureUUID;
                                            }
                                            processed.insert(texNode->ID.Get());
                                            ++slot;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                catch (...) {}
            }
        }
        else
        {
            entry.Material = m_DefaultMaterial;
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

        // Recarrega o material com o shader novo
        const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);
        if (record)
        {
            auto matAsset = MaterialAsset::LoadFromFile(record->FilePath);
            if (matAsset)
            {
                auto material = matAsset->GetMaterial();

                // Recompila o shader
                auto graphPath = record->FilePath;
                graphPath.replace_extension(".axegraph");
                if (std::filesystem::exists(graphPath))
                {
                    std::ifstream file(graphPath);
                    try
                    {
                        nlohmann::json j = nlohmann::json::parse(file);
                        MaterialGraph graph;
                        graph.Deserialize(j);

                        auto result = MaterialCompiler::Compile(&graph);
                        if (result.Success)
                        {
                            auto shader = Shader::Create(
                                result.VertexShader, result.FragmentShader);
                           
                            if (shader)
                            {
                                material->SetShader(shader);

                                // Transfere texturas na ordem correta
                                std::function<Node* (ed::PinId)> findTextureSample =
                                    [&](ed::PinId startPin) -> Node*
                                    {
                                        for (auto& n : graph.GetNodes())
                                            for (auto& outPin : n->Outputs)
                                            {
                                                if (outPin.ID != startPin) continue;
                                                if (n->Name == "Texture Sample") return n.get();
                                                for (auto& inPin : n->Inputs)
                                                    for (auto& lnk : graph.GetLinks())
                                                    {
                                                        if (lnk.EndPin != inPin.ID) continue;
                                                        Node* found = findTextureSample(lnk.StartPin);
                                                        if (found) return found;
                                                    }
                                            }
                                        return nullptr;
                                    };

                                Node* outputNode = nullptr;
                                for (auto& n : graph.GetNodes())
                                    if (n->Name == "Material Output") { outputNode = n.get(); break; }

                                if (outputNode)
                                {
                                    int slot = 0;
                                    std::unordered_set<int> processed;

                                    for (auto& inputPin : outputNode->Inputs)
                                    {
                                        for (auto& lnk : graph.GetLinks())
                                        {
                                            if (lnk.EndPin != inputPin.ID) continue;
                                            Node* texNode = findTextureSample(lnk.StartPin);
                                            if (texNode && !processed.count(texNode->ID.Get())
                                                && texNode->Value.TextureVal)
                                            {
                                                if (slot == 0)
                                                {
                                                    material->AlbedoMap = texNode->Value.TextureVal;
                                                    material->AlbedoUUID = texNode->Value.TextureUUID;
                                                }
                                                processed.insert(texNode->ID.Get());
                                                ++slot;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    catch (...) {}
                }
                it->second.Material = material;
            }
        }

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