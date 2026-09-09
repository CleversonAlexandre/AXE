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
#include "editor/axe_editor/asset/asset_spawn_defaults.hpp"   // MATFUNC_V2
#include "editor/axe_editor/material_thumbnail_renderer.hpp"   // MATFUNC_V2
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>    // SHADER_SOURCE_ECHO_V1
#include <algorithm>  // SHADER_SOURCE_ECHO_V1 — std::find
#include <cctype>     // SHADER_SOURCE_ECHO_V1 — isdigit
#include <cstdlib>    // SHADER_SOURCE_ECHO_V1 — atoi

namespace ed = ax::NodeEditor;

namespace axe
{
    // ═════════════════════════════════════════════════════════════════════════
    //  SHADER_SOURCE_ECHO_V1 — mostrar a linha que o driver reclamou
    //
    //  Ver a nota na declaracao (material_editor_window.hpp). Em resumo: o
    //  GLSL nunca chega ao disco quando a compilacao FALHA, entao um
    //  "ERROR: 0:57" apontava para um texto que o autor nao tinha como abrir.
    // ═════════════════════════════════════════════════════════════════════════
    void MaterialEditorWindow::LogShaderSource(const std::string& source,
        const std::string& driverError,
        const char* label)
    {
        if (source.empty()) return;

        // ── 1. quais linhas o driver citou ───────────────────────────────
        //
        // Formato do GLSL: "ERROR: <arquivo>:<linha>:". O <arquivo> e sempre 0
        // aqui (fonte unico), entao o numero util e o SEGUNDO de cada par
        // separado por ':'. Varrido a mao em vez de regex para nao depender do
        // texto exato do driver — AMD, NVIDIA e Intel divergem na frase, mas
        // os dois numeros com dois-pontos no meio sao universais.
        std::vector<int> lines;

        for (std::size_t i = 0; i < driverError.size(); )
        {
            if (!std::isdigit((unsigned char)driverError[i])) { ++i; continue; }

            std::size_t a = i;
            while (a < driverError.size() && std::isdigit((unsigned char)driverError[a])) ++a;

            // Dois formatos no mercado: "0:57:" (AMD/Intel/Mesa) e "0(57)"
            // (NVIDIA). Aceitar os dois custa um caractere a mais no teste e
            // evita que a ferramenta minta numa maquina diferente.
            if (a >= driverError.size() ||
                (driverError[a] != ':' && driverError[a] != '(')) {
                i = a; continue;
            }

            std::size_t b = a + 1, c = b;
            while (c < driverError.size() && std::isdigit((unsigned char)driverError[c])) ++c;

            if (c > b)
            {
                const int ln = std::atoi(driverError.substr(b, c - b).c_str());
                if (ln > 0 && std::find(lines.begin(), lines.end(), ln) == lines.end())
                    lines.push_back(ln);
                i = c;
            }
            else i = b;
        }

        // ── 2. quebra o fonte gerado em linhas ───────────────────────────
        std::vector<std::string> src;
        {
            std::istringstream in(source);
            std::string l;
            while (std::getline(in, l)) src.push_back(l);
        }

        if (lines.empty())
        {
            // Driver que nao cita linha. Melhor dizer isso do que despejar o
            // shader inteiro no painel.
            LogWarning(std::string("[") + label + "] o driver nao informou a linha. "
                "O fonte gerado tem " + std::to_string(src.size()) + " linhas.");
            return;
        }

        LogWarning(std::string("[") + label + "] fonte gerado, nas linhas citadas:");

        for (int ln : lines)
        {
            const int from = (ln - 2 < 1) ? 1 : ln - 2;
            const int to = (ln + 2 > (int)src.size()) ? (int)src.size() : ln + 2;

            for (int i = from; i <= to; ++i)
            {
                // A linha culpada leva seta; as de contexto, espaco. Sem isso o
                // olho tem de contar linhas dentro do painel.
                const std::string num = std::to_string(i);
                const std::string pad(num.size() < 4 ? 4 - num.size() : 0, ' ');

                LogError((i == ln ? "  >>" : "    ") + pad + num + " | " + src[(std::size_t)i - 1]);
            }
        }
    }

    void MaterialEditorWindow::CompileAndApply()
    {
        ClearLog();

        // ── MATFUNC_V1 ───────────────────────────────────────────────────────
        //
        // Compilar uma FUNCAO nao gera shader: ela nao tem Material Output, nao
        // tem dominio e nao vira `.axeshader`. O que este botao faz aqui e
        // VALIDAR e SALVAR — e a validacao que importa e a que so da para fazer
        // olhando o grafo, nao o GLSL.
        if (IsFunctionMode())
        {
            LogInfo("Validando Material Function...");

            int inputs = 0, outputs = 0, unbound = 0;
            for (auto& node : m_Graph->GetNodes())
            {
                if (node->Name == "Function Input") inputs++;
                else if (node->Name == "Function Output")
                {
                    outputs++;

                    // Saida sem nada ligado devolve zero em todo material que
                    // chamar esta funcao, e compila limpo — exatamente o tipo
                    // de defeito silencioso que so um aviso aqui pega.
                    if (node->Inputs.empty() || !m_Graph->IsPinLinked(node->Inputs[0].ID))
                        unbound++;
                }
            }

            if (outputs == 0)
                LogWarning("Nenhum Function Output: esta funcao nao devolve nada.");
            if (unbound > 0)
                LogWarning("Ha " + std::to_string(unbound) + " Function Output sem nada "
                    "ligado — cada um deles vale zero em quem chamar.");

            SaveGraph();

            // ── MATFUNC_V2 — a esfera de preview da funcao ───────────────
            //
            // Depois do Save, e nao antes: o envelope de preview e montado a
            // partir do ARQUIVO em disco (o inlining le o `.axematfunc`), e
            // nao do grafo em memoria. Compilar antes de salvar mostraria a
            // versao anterior e faria o autor achar que a mudanca nao pegou.
            RefreshFunctionPreview();

            LogInfo("Funcao validada: " + std::to_string(inputs) + " entrada(s), "
                + std::to_string(outputs) + " saida(s).");
            return;
        }

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

        // POSTPROCESS_DOMAIN_V1 — terceiro dominio com compilador proprio.
        // Continua na forma explicita (e nao `!= Surface`) pela razao descrita
        // acima: DeferredDecal, Volume e UserInterface seguem sem compilador, e
        // um `else` os coziria como se fossem outra coisa.
        const bool isPostProcessDomain = (m_Graph->Domain == MaterialDomain::PostProcess);

        if ((isLightDomain || isParticleDomain || isPostProcessDomain) && m_Asset
            && !m_Asset->GetFilePath().empty())
        {
            auto domainResult =
                isLightDomain ? MaterialCompiler::CompileLightFunction(m_Graph.get()) :
                isParticleDomain ? MaterialCompiler::CompileParticleFunction(m_Graph.get()) :
                MaterialCompiler::CompilePostProcess(m_Graph.get());

            const auto cookedDomain =
                isLightDomain ? CookedMaterialDomain::LightFunction :
                isParticleDomain ? CookedMaterialDomain::Particle :
                CookedMaterialDomain::PostProcess;

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

        // ── MATFUNC_V1 ───────────────────────────────────────────────────────
        //
        // Erro de GRAFO nao passa pelo log do driver. Um asset de funcao que
        // sumiu, um ciclo, um pino que a assinatura nao tem mais: em todos
        // esses casos o GLSL gerado compila LIMPO, so que com zero no lugar do
        // valor. Sem despejar isso aqui, o unico sintoma seria o material ficar
        // visualmente errado sem uma linha de aviso em lugar nenhum — que e a
        // forma mais cara de defeito que existe para depurar.
        {
            const std::string& fnErrors = MaterialCompiler::LastFunctionErrors();
            if (!fnErrors.empty())
            {
                std::stringstream ss(fnErrors);
                std::string line;
                while (std::getline(ss, line))
                    if (!line.empty()) LogWarning(line);
            }
        }

        std::shared_ptr<Shader> compiledShader;
        try { compiledShader = Shader::Create(result.VertexShader, result.FragmentShader); }
        catch (const std::exception& e)
        {
            LogError(std::string("Shader creation failed: ") + e.what());
            // SHADER_SOURCE_ECHO_V1 — a linha citada, do fonte que o driver leu.
            LogShaderSource(result.FragmentShader, e.what(), "forward");
            return;
        }
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
                // SHADER_SOURCE_ECHO_V1 — o Surface gera DOIS shaders; sem o
                // rotulo, uma falha so no G-Buffer parecia falha do forward.
                LogShaderSource(result.GeometryFragShader, e.what(), "G-Buffer");
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
        m_Material->TwoSided = result.TwoSided;   // TWO_SIDED_V1
        m_Material->UsesSceneHeight = result.UsesSceneHeight;   // SCENE_HEIGHT_V6

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


    // ── MATFUNC_V2 ───────────────────────────────────────────────────────────
    //
    // Monta o material envelope da funcao aberta e poe em m_Material, que e o
    // que a janela de preview desenha. Em modo funcao m_Material e nulo por
    // padrao — e quando ele deixa de ser, a esfera aparece sozinha, porque o
    // DrawPreviewWindow passou a ser chamado por "existe material", e nao por
    // "nao e funcao".
    //
    // Falhar aqui e normal e nao e erro: funcao sem Function Output, ou com a
    // saida solta, nao tem o que desenhar. Nesse caso m_Material fica nulo, a
    // janela de preview some, e quem explica o motivo sao os avisos que a
    // validacao ja escreveu no Shader Log.
    void MaterialEditorWindow::RefreshFunctionPreview()
    {
        if (!IsFunctionMode()) return;

        const auto& path = m_FunctionAsset->GetFilePath();
        if (path.empty()) { m_Material = nullptr; return; }

        const AssetRecord* rec = AssetDatabase::Get().GetByPath(path);
        if (!rec) { m_Material = nullptr; return; }

        m_Material = AssetSpawnDefaults::ResolveMaterialFunctionPreview(rec->UUID);

        // A miniatura no Asset Browser tem que acompanhar: e o mesmo envelope,
        // e sem invalidar ela ficaria congelada na versao de antes da edicao.
        if (m_ThumbnailRenderer)
            m_ThumbnailRenderer->Invalidate(rec->UUID);
    }

    void MaterialEditorWindow::SaveGraph()
    {
        if (!m_Graph) return;

        // ── MATFUNC_V1 ───────────────────────────────────────────────────────
        //
        // Um `.axematfunc` e UM arquivo so: cabecalho (nome, descricao,
        // assinatura) e o grafo aninhado dentro dele. Nao ha `.axegraph` irmao
        // como no material, e nao ha `.axeshader` — funcao nao cozinha nada.
        //
        // A assinatura e refeita dentro do Save, a partir dos nodes Function
        // Input/Output do grafo que esta na tela.
        if (IsFunctionMode())
        {
            auto path = m_FunctionAsset->GetFilePath();
            if (path.empty())
            {
                LogError("[MATFUNC_V1] esta funcao nao tem caminho em disco.");
                return;
            }

            if (m_FunctionAsset->Save(path, *m_Graph))
            {
                LogInfo("[MATFUNC_V1] funcao salva em '" + path.string() + "'.");
                AXE_EDITOR_INFO("MaterialEditorWindow: funcao salva em '{}'", path.string());
            }
            return;
        }

        if (!m_Asset) return;

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
        // MATFUNC_V1 — em modo funcao o grafo ja veio junto com o asset
        // (OpenMaterialFunction tomou a posse dele). Nao ha `.axegraph` irmao
        // para ler, e cair no caminho de baixo apagaria o que esta na tela.
        if (IsFunctionMode()) return;

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
            m_Material->TwoSided = result.TwoSided;   // TWO_SIDED_V1
            m_Material->UsesSceneHeight = result.UsesSceneHeight;   // SCENE_HEIGHT_V6
        }

        AXE_CORE_INFO("MaterialEditorWindow: grafo carregado.");
    }


} // namespace axe