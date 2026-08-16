// material_compile.cpp
// Compilação do grafo de nodes em shader real (CompileAndApply) e
// serialização do grafo em disco (.axegraph) — SaveGraph/LoadGraph.

#include "material_editor_window.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/material/material_shader_cache.hpp"   // PKG10 — invalidacao
#include "editor/axe_editor/material/material_compiler.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/log/log.hpp"
#include "editor/axe_editor/inspector_window.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

namespace ed = ax::NodeEditor;

namespace axe
{

    void MaterialEditorWindow::CompileAndApply()
    {
        ClearLog();
        LogInfo("Compilando shader...");

        if (!m_Material || !m_Graph) return;

        // ── PKG9 — cozimento dos domínios que não são superfície ─────────────
        //
        // ANTES da compilação de superfície, e não junto com ela lá embaixo.
        //
        // O motivo é correção: o B4 cozinhava o resultado de
        // `MaterialCompiler::Compile` — que é SEMPRE o domínio Surface —
        // independentemente do domínio do grafo. Clicar em Compile num material
        // de Light Function sobrescrevia o `.axeshader` correto (gravado pelo
        // callback no load de cena) por um shader de superfície. O runtime
        // recusa pelo campo `domain`, mas o arquivo bom já teria ido embora.
        //
        // Os dois booleanos são explícitos de propósito: `MaterialDomain` tem
        // sete valores, e quatro deles (DeferredDecal, Volume, PostProcess,
        // UserInterface) ainda não têm compilador. Um `!= Surface` trataria
        // esses quatro como partícula e gravaria um cozido que não tem nada a
        // ver com o material. Eles não cozinham nada — que é o certo enquanto
        // não existir um compilador para eles.
        const bool isLightDomain = (m_Graph->Domain == MaterialDomain::LightFunction);
        const bool isParticleDomain = (m_Graph->Domain == MaterialDomain::Particle);

        if ((isLightDomain || isParticleDomain) && m_Asset
            && !m_Asset->GetFilePath().empty())
        {
            auto domainResult = isLightDomain
                ? MaterialCompiler::CompileLightFunction(m_Graph.get())
                : MaterialCompiler::CompileParticleFunction(m_Graph.get());

            const auto cookedDomain = isLightDomain
                ? CookedMaterialDomain::LightFunction
                : CookedMaterialDomain::Particle;

            if (!domainResult.Success)
                LogError("Compilação (" + std::string(CookedMaterial::DomainName(cookedDomain))
                    + ") falhou: " + domainResult.ErrorMessage);
            else if (MaterialCompiler::BakeShaderToDisk(domainResult,
                m_Asset->GetFilePath(), cookedDomain))
                LogInfo("Shader cozido (.axeshader, "
                    + std::string(CookedMaterial::DomainName(cookedDomain)) + ") atualizado.");
            else
                LogWarning("Falha ao gravar o .axeshader — o jogo empacotado vai "
                    "carregar este material com defaults.");

            // O cache de tempo de jogo guarda o shader deste material por
            // UUID (sub-emissores, FX de AnimNotify). Sem esta linha, compilar
            // um material de partícula e dar Play mostraria o shader ANTERIOR
            // até reabrir o editor — o cache mentindo, que é o único jeito de
            // ele piorar as coisas.
            if (const AssetRecord* rec = AssetDatabase::Get().GetByPath(m_Asset->GetFilePath()))
                MaterialShaderCache::Invalidate(rec->UUID);

            // Sem `return`: o resto da função (salvar o .axemat, preview,
            // aplicar na cena) continua exatamente como era. Só o cozimento
            // mudou de lugar — e o cozimento de superfície lá embaixo agora
            // sabe que não é a vez dele.
        }

        auto result = MaterialCompiler::Compile(m_Graph.get());
        if (!result.Success) { LogError("Compilação falhou: " + result.ErrorMessage); return; }

        std::shared_ptr<Shader> compiledShader;
        try { compiledShader = Shader::Create(result.VertexShader, result.FragmentShader); }
        catch (const std::exception& e) { LogError(std::string("Shader creation failed: ") + e.what()); return; }
        if (!compiledShader) { LogError("Shader::Create retornou null"); return; }

        m_Material->SetShader(compiledShader);
        m_Material->UsePBR = true;

        // ✅ Geometry shader para deferred
        if (!result.GeometryFragShader.empty())
        {
            try
            {
                auto geometryShader = Shader::Create(result.VertexShader, result.GeometryFragShader);
                if (geometryShader)
                    m_Material->SetGeometryShader(geometryShader);
                else
                {
                    const std::string msg = "GeometryShader retornou null (erro silencioso do driver).";
                    AXE_CORE_WARN("{}", msg);
                    LogWarning(msg);
                }
            }
            catch (const std::exception& e)
            {
                const std::string msg = std::string("GeometryShader: ") + e.what();
                AXE_CORE_WARN("{}", msg);
                LogError(msg);
            }
        }
        m_Material->SamplerTextures = result.SamplerTextures;

        // Emissive médio pro GI — as telas/superfícies emissivas passam a
        // banhar as probes e as reflections no próximo bake
        m_Material->BakedEmissive = MaterialCompiler::ComputeBakedEmissive(m_Graph.get());

        // Usa as texturas extraídas pelo próprio compilador (AlbedoTexture/
        // NormalTexture), exatamente como o callback de recarga de cena faz
        // em SceneSerializer::SetMaterialRecompileCallback. Antes, este
        // caminho usava ApplyOutputTextures() — uma segunda implementação,
        // com lógica de extração diferente — então "compilar" e "reiniciar"
        // produziam resultados divergentes (o bug do corrimão: perdia o
        // visual ao compilar, voltava diferente ao reiniciar).
        m_Material->AlbedoMap = result.AlbedoTexture;
        m_Material->NormalMap = result.NormalTexture;
        m_Material->IsTransparent = result.IsTransparent;

        if (m_Asset) m_Asset->SetMaterial(m_Material);
        if (!m_Asset->GetFilePath().empty()) m_Asset->Save(m_Asset->GetFilePath());
        SaveGraph();

        // B4 — cozinha o `.axeshader` junto do save, como o `.axegraph`. É o
        // arquivo que o game.exe carrega no lugar do recompile callback.
        //
        // PKG9 — só para o domínio Surface: os outros dois já foram cozidos lá
        // em cima, com o compilador do domínio deles.
        if (m_Graph->Domain == MaterialDomain::Surface
            && m_Asset && !m_Asset->GetFilePath().empty())
        {
            if (MaterialCompiler::BakeToDisk(result, m_Asset->GetFilePath(),
                m_Material->BakedEmissive))
                LogInfo("Shader cozido (.axeshader) atualizado.");
            else
                LogWarning("Falha ao gravar o .axeshader — o jogo empacotado "
                    "vai carregar este material com defaults.");
        }

        // Atualiza preview
        if (m_PreviewScene)
        {
            auto& registry = m_PreviewScene->GetRegistry();
            if (registry.valid(m_PreviewEntity))
            {
                if (registry.all_of<MaterialComponent>(m_PreviewEntity))
                    registry.get<MaterialComponent>(m_PreviewEntity).Data = m_Material;
                else
                    registry.emplace<MaterialComponent>(m_PreviewEntity, m_Material);
            }
        }

        // Aplica na cena
        if (m_Context && m_Context->ActiveScene && m_Asset)
        {
            auto& registry = m_Context->ActiveScene->GetRegistry();
            const AssetRecord* record = AssetDatabase::Get().GetByPath(m_Asset->GetFilePath());
            if (!record) { LogWarning("Asset não encontrado."); return; }

            std::string assetUUID = record->UUID;
            int count = 0;
            for (auto entity : registry.view<MaterialComponent>())
            {
                auto& mc = registry.get<MaterialComponent>(entity);
                if (mc.MaterialAssetUUID == assetUUID)
                {
                    mc.Data = m_Material;
                    ++count;
                }
            }
            if (count > 0) LogInfo("Material aplicado em " + std::to_string(count) + " objeto(s).");
            else LogWarning("Nenhum objeto usa este material.");
        }
        else LogWarning("Nenhuma cena ativa.");

        // Só reporta sucesso completo se o geometry shader também compilou —
        // se tiver algum [ERR] no log acima, o usuário já sabe que algo falhou.
        bool hasErrors = false;
        for (auto& e : m_ShaderLog)
            if (e.level == ShaderLogEntry::Level::Error) { hasErrors = true; break; }
        if (!hasErrors)
            LogInfo("Shader compilado com sucesso.");
        else
            LogWarning("Compilação concluída com erros — verifique as mensagens acima.");

        if (m_ThumbnailRenderer && m_Asset)
        {
            const AssetRecord* record = AssetDatabase::Get().GetByPath(m_Asset->GetFilePath());
            if (record) m_ThumbnailRenderer->Invalidate(record->UUID);
        }

        InspectorWindow::MarkGraphCacheDirty();
    }


    // Extrai a textura conectada ao Base Color e ao Normal do Material
    // Output e aplica em m_Material->AlbedoMap/NormalMap (ou limpa, se a
    // conexão foi removida). Usada tanto por CompileAndApply() (botão de
    // compilar) quanto por LoadGraph() (ao abrir o material) — antes esta
    // lógica só existia em CompileAndApply, então o preview ficava sem a
    // textura de Base Color toda vez que o material era aberto, até o
    // usuário clicar em compilar manualmente.
    // [NÃO USADA] Mantida apenas por referência. A extração de texturas do
    // material agora vem direto do MaterialCompiler (result.AlbedoTexture/
    // NormalTexture) em todos os caminhos (CompileAndApply, LoadGraph e o
    // callback de recarga de cena), garantindo resultado idêntico entre
    // compilar / abrir / reiniciar. Esta função usava uma segunda lógica de
    // extração que divergia da do compilador.
    void MaterialEditorWindow::ApplyOutputTextures()
    {
        if (!m_Material || !m_Graph) return;

        Node* outputNode = nullptr;
        for (auto& n : m_Graph->GetNodes())
            if (n->Name == "Material Output") { outputNode = n.get(); break; }
        if (!outputNode) return;

        std::function<Node* (ed::PinId)> findTextureSample =
            [&](ed::PinId startPin) -> Node*
            {
                for (auto& n : m_Graph->GetNodes())
                    for (auto& outPin : n->Outputs)
                    {
                        if (outPin.ID != startPin) continue;
                        if (n->Name == "Texture Sample") return n.get();
                        for (auto& inPin : n->Inputs)
                            for (auto& lnk : m_Graph->GetLinks())
                            {
                                if (lnk.EndPin != inPin.ID) continue;
                                Node* found = findTextureSample(lnk.StartPin);
                                if (found) return found;
                            }
                    }
                return nullptr;
            };

        // Aplica (ou limpa, se desconectado) a textura ligada a um pin do
        // Material Output num slot do material (Albedo/Normal/etc).
        auto applyPin = [&](size_t pinIndex, std::shared_ptr<Texture2D>& mapSlot, std::string& uuidSlot) -> bool
            {
                if (outputNode->Inputs.size() <= pinIndex) return false;
                ed::PinId pinId = outputNode->Inputs[pinIndex].ID;

                bool connected = false;
                for (auto& lnk : m_Graph->GetLinks())
                {
                    if (lnk.EndPin != pinId) continue;
                    connected = true;
                    Node* texNode = findTextureSample(lnk.StartPin);
                    if (texNode && texNode->Value.TextureVal)
                    {
                        mapSlot = texNode->Value.TextureVal;
                        uuidSlot = texNode->Value.TextureUUID;
                    }
                    break;
                }

                if (!connected)
                {
                    mapSlot = nullptr;
                    uuidSlot.clear();
                }
                return connected;
            };

        // Base Color = pin 0, Normal = pin 3 (ver layout do Material Output)
        bool albedoConnected = applyPin(0, m_Material->AlbedoMap, m_Material->AlbedoUUID);
        applyPin(3, m_Material->NormalMap, m_Material->NormalUUID);

        // Sem textura de Base Color conectada — usa cor neutra de fallback
        // para o material não renderizar preto.
        if (!albedoConnected)
            m_Material->Color = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);
    }


    void MaterialEditorWindow::SaveGraph()
    {
        if (!m_Asset || !m_Graph) return;

        auto graphPath = m_Asset->GetFilePath();
        graphPath.replace_extension(".axegraph");

        nlohmann::json j = m_Graph->Serialize();

        std::ofstream file(graphPath);
        if (!file.is_open())
        {
            AXE_EDITOR_INFO("MaterialEditorWindow: falha ao salvar grafo em '{}'", graphPath.string());
            return;
        }
        file << j.dump(4);
        AXE_EDITOR_INFO("MaterialEditorWindow: grafo salvo em '{}'", graphPath.string());
    }


    void MaterialEditorWindow::LoadGraph()
    {
        if (!m_Asset || !m_Graph) return;

        auto graphPath = m_Asset->GetFilePath();
        graphPath.replace_extension(".axegraph");

        if (!std::filesystem::exists(graphPath))
            return;

        std::ifstream file(graphPath);
        nlohmann::json j;
        try { j = nlohmann::json::parse(file); }
        catch (const nlohmann::json::exception& e)
        {
            AXE_CORE_ERROR("MaterialEditorWindow: erro ao carregar grafo: {}", e.what());
            return;
        }

        m_Graph->Deserialize(j);

        // ✅ Compila mas não tenta aplicar na cena — cena pode não estar pronta
        auto result = MaterialCompiler::Compile(m_Graph.get());
        if (!result.Success) return;

        try
        {
            auto compiledShader = Shader::Create(result.VertexShader, result.FragmentShader);
            if (compiledShader && m_Material)
                m_Material->SetShader(compiledShader);

            if (!result.GeometryFragShader.empty())
            {
                try
                {
                    auto geometryShader = Shader::Create(result.VertexShader, result.GeometryFragShader);
                    if (geometryShader && m_Material)
                        m_Material->SetGeometryShader(geometryShader);
                }
                catch (const std::exception& e)
                {
                    AXE_CORE_WARN("GeometryShader (preview): {}", e.what());
                    // Preview — sem acesso ao LogError aqui (função estática),
                    // mas o erro já aparece via LogError no CompileAndApply.
                }
            }
        }
        catch (...) {}

        // Mesmo conjunto de texturas/samplers que CompileAndApply e o callback
        // de recarga de cena usam — todos os três caminhos agora idênticos,
        // então abrir / compilar / reiniciar produzem exatamente o mesmo
        // resultado visual.
        if (m_Material)
        {
            m_Material->SamplerTextures = result.SamplerTextures;
            m_Material->AlbedoMap = result.AlbedoTexture;
            m_Material->NormalMap = result.NormalTexture;
            m_Material->BakedEmissive = MaterialCompiler::ComputeBakedEmissive(m_Graph.get());
            m_Material->IsTransparent = result.IsTransparent;
        }

        AXE_CORE_INFO("MaterialEditorWindow: grafo carregado.");
    }


} // namespace axe