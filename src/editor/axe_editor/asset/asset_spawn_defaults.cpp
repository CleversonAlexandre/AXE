#include "asset_spawn_defaults.hpp"

#include "axe/asset/asset_database.hpp"
#include "axe/material/material_asset.hpp"
#include "editor/axe_editor/node_graph/material_graph.hpp"
#include "axe/graphics/shader.hpp"
#include "editor/axe_editor/material/material_compiler.hpp"
#include "editor/axe_editor/material/material_function.hpp"   // MATFUNC_V2
#include "axe/scene/components.hpp"
#include "axe/scene/scene_serializer.hpp"
#include "axe/log/log.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <unordered_set>
#include <functional>
#include <filesystem>
#include <fstream>

namespace axe
{
    // ── MATFUNC_V2 ───────────────────────────────────────────────────────────
    //
    // Ver a nota longa no header. Em resumo: monta um material envelope em
    // memoria com a funcao ligada no Base Color, e compila esse grafo.
    std::shared_ptr<Material> AssetSpawnDefaults::ResolveMaterialFunctionPreview(
        const std::string& functionUUID)
    {
        if (functionUUID.empty()) return nullptr;

        const AssetRecord* record = AssetDatabase::Get().GetByUUID(functionUUID);
        if (!record || record->Type != AssetType::MaterialFunction) return nullptr;

        std::error_code ec;
        if (!std::filesystem::exists(record->FilePath, ec)) return nullptr;

        // So o cabecalho: a assinatura basta para montar os pinos da chamada,
        // e o grafo da funcao sera lido pelo inlining, no compilador.
        std::string fnName;
        std::vector<MaterialFunctionParam> ins, outs;
        if (!MaterialFunction::ReadSignature(record->FilePath, fnName, ins, outs))
            return nullptr;

        if (outs.empty())
        {
            AXE_EDITOR_WARN("[MATFUNC_V2] a funcao '{}' nao tem Function Output — "
                "nao ha o que mostrar na miniatura.", record->Name);
            return nullptr;
        }

        MaterialGraph wrapper;

        Node* outputNode = wrapper.AddMaterialOutputNode();
        Node* callNode = wrapper.AddMaterialFunctionNode();
        if (!outputNode || !callNode) return nullptr;

        callNode->StringValue = functionUUID;
        wrapper.RebuildFunctionCallPins(callNode, ins, outs);

        if (callNode->Outputs.empty() || outputNode->Inputs.empty()) return nullptr;

        // Pino 0 do Material Output e o Base Color — os indices dos pinos do
        // Material Output sao posicionais, como o material_graph documenta.
        // Base Color, e nao Emissive, porque a esfera do preview e iluminada:
        // o mesmo valor aparece com sombreamento, que e como a funcao vai ser
        // vista no material de verdade.
        wrapper.AddLink(callNode->Outputs[0].ID, outputNode->Inputs[0].ID);
        wrapper.BuildNodes();

        auto result = MaterialCompiler::Compile(&wrapper);
        if (!result.Success)
        {
            AXE_EDITOR_WARN("[MATFUNC_V2] a funcao '{}' nao compilou: {}",
                record->Name, result.ErrorMessage);
            return nullptr;
        }

        auto shader = Shader::Create(result.VertexShader, result.FragmentShader);
        if (!shader)
        {
            AXE_EDITOR_WARN("[MATFUNC_V2] o shader de preview da funcao '{}' nao "
                "linkou.", record->Name);
            return nullptr;
        }

        auto material = std::make_shared<Material>();
        material->SetShader(shader);
        material->SamplerTextures = result.SamplerTextures;
        material->IsTransparent = false;   // a esfera do preview e sempre opaca
        return material;
    }

    std::shared_ptr<Material> AssetSpawnDefaults::ResolveMaterial(
        const std::string& materialUUID)
    {
        if (materialUUID.empty()) return nullptr;

        const AssetRecord* record = AssetDatabase::Get().GetByUUID(materialUUID);
        if (!record || record->Type != AssetType::Material) return nullptr;

        auto matAsset = MaterialAsset::LoadFromFile(record->FilePath);
        if (!matAsset) return nullptr;

        auto material = matAsset->GetMaterial();
        if (!material) return nullptr;

        // O shader do material vive no grafo, nao no `.axemat`. Sem este
        // callback o material carrega com os parametros certos e o shader do
        // padrao — que aparece como "o material esta errado", nao como um erro.
        if (SceneSerializer::GetMaterialRecompileCallback())
            SceneSerializer::GetMaterialRecompileCallback()(materialUUID, material.get());

        // A textura do primeiro Texture Sample CONECTADO vem do `.axegraph`
        // irmao. Nao e redundancia com o `.axemat`: o `.axemat` guarda os
        // parametros, e o grafo e quem sabe qual textura esta ligada em que
        // pino. Um sem o outro da material sem albedo.
        std::filesystem::path graphPath = record->FilePath;
        graphPath.replace_extension(".axegraph");

        std::error_code ec;
        if (!std::filesystem::exists(graphPath, ec))
            return material;

        std::ifstream file(graphPath);
        try
        {
            nlohmann::json j = nlohmann::json::parse(file);
            MaterialGraph graph;
            graph.Deserialize(j);

            // ═══════════════════════════════════════════════════════════════
            //  BP_MATERIAL_V2b — compilar AQUI quando o callback nao resolveu
            //
            //  O callback de recompilacao e o caminho normal: ele vem do
            //  SceneSerializer e reaproveita o cache de shader do editor.
            //  Mas ele so existe depois do OnAttach, e a thumbnail de material
            //  e pedida muito cedo — no primeiro desenho do Asset Browser.
            //
            //  Sem esta rede, unificar as copias teria QUEBRADO justamente o
            //  caso que este arquivo veio consertar: o codigo antigo da
            //  thumbnail compilava o grafo por conta propria e por isso
            //  funcionava cedo. Compilar aqui so quando falta shader mantem o
            //  caminho barato barato e o caso cedo funcionando.
            if (!material->GetShader())
            {
                auto result = MaterialCompiler::Compile(&graph);

                if (result.Success)
                {
                    if (auto shader = Shader::Create(result.VertexShader,
                        result.FragmentShader))
                        material->SetShader(shader);

                    if (!result.GeometryFragShader.empty())
                    {
                        if (auto geo = Shader::Create(result.VertexShader,
                            result.GeometryFragShader))
                            material->SetGeometryShader(geo);
                    }
                }
                else
                {
                    // Antes isto era um `if (result.Success)` sem else: grafo
                    // que nao compila dava esfera branca eterna e nenhuma
                    // pista. O erro do compilador e a pista.
                    AXE_EDITOR_WARN("ResolveMaterial: o grafo de '{}' nao "
                        "compilou — o material fica sem shader.", record->Name);
                }
            }

            // ═══════════════════════════════════════════════════════════════
            //  BP_MATERIAL_V2 — achar a textura ANDANDO DE TRAS PARA FRENTE
            //
            //  ── O QUE ESTAVA ERRADO AQUI ──────────────────────────────────
            //
            //  A versao anterior pegava "o primeiro no Texture Sample com
            //  slot 0". Isso presume que a ordem em que os nos aparecem na
            //  lista corresponde a ordem dos slots do material — e nao
            //  corresponde a nada: a lista esta na ordem em que os nos foram
            //  CRIADOS. Um grafo onde o autor fez a normal antes da cor
            //  entregava a normal como albedo, ou nao entregava nada.
            //
            //  ── A VERSAO CERTA JA EXISTIA ────────────────────────────────
            //
            //  O renderizador de thumbnail de material fazia diferente e
            //  melhor: parte do no "Material Output", segue cada entrada dele
            //  para tras pelos links e devolve o Texture Sample que ALIMENTA
            //  aquela entrada. E a pergunta certa — "o que esta ligado na cor
            //  base?" — em vez de um palpite pela ordem da lista.
            //
            //  Era a quarta copia da rotina de material, e a unica correta.
            //  Agora ela e a unica que existe, e as outras tres chamam daqui.
            // ═══════════════════════════════════════════════════════════════
            std::function<Node* (ed::PinId)> findTextureSample =
                [&](ed::PinId startPin) -> Node*
                {
                    for (auto& n : graph.GetNodes())
                    {
                        for (auto& outPin : n->Outputs)
                        {
                            if (outPin.ID != startPin) continue;
                            if (n->Name == "Texture Sample") return n.get();

                            // Nao e Texture Sample: continua subindo por cada
                            // entrada dele. E o que faz a busca atravessar
                            // Multiply, Lerp e o que mais houver no caminho.
                            for (auto& inPin : n->Inputs)
                                for (auto& lnk : graph.GetLinks())
                                {
                                    if (lnk.EndPin != inPin.ID) continue;
                                    if (Node* found = findTextureSample(lnk.StartPin))
                                        return found;
                                }
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
                            // Slot 0 = a primeira ENTRADA do Material Output
                            // que chega a uma textura, que e a cor base.
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
        catch (const std::exception& e)
        {
            // Grafo corrompido nao invalida o material: ele ja esta carregado
            // com os parametros do `.axemat`, so fica sem a textura.
            //
            // O `catch (...) {}` mudo que morava aqui e nas outras copias e o
            // motivo de "algumas thumbnails simplesmente nao aparecem": toda
            // falha era engolida, e o resultado era uma esfera branca sem
            // nenhuma pista de por que.
            AXE_EDITOR_WARN("ResolveMaterial: '{}' nao pode ser lido ({}); o "
                "material '{}' carrega sem textura.",
                graphPath.filename().string(), e.what(), record->Name);
        }

        return material;
    }

    bool AssetSpawnDefaults::BuildCollider(const Mesh& mesh,
        const AssetImportSettings& imp, ColliderComponent& out)
    {
        if (imp.CollisionShape < 0) return false;

        const auto& verts = mesh.GetVertices();
        if (verts.empty())
        {
            // Malha cozida carrega direto para a GPU e nao tem vertices em CPU
            // (ver mesh.hpp). Nao da para medir o que nao esta aqui — e chutar
            // um cubo de 1 m seria pior que nao criar nada.
            return false;
        }

        glm::vec3 mn = verts[0].Position;
        glm::vec3 mx = mn;
        for (const auto& v : verts)
        {
            mn = glm::min(mn, v.Position);
            mx = glm::max(mx, v.Position);
        }

        const glm::vec3 center = (mn + mx) * 0.5f;
        const glm::vec3 half = glm::max((mx - mn) * 0.5f, glm::vec3(0.001f));

        out = ColliderComponent{};
        out.Shape = static_cast<ColliderShape>(
            std::clamp(imp.CollisionShape, 0, 4));
        out.IsTrigger = imp.CollisionIsTrigger;

        // ═══════════════════════════════════════════════════════════════════
        //  COLLIDER_OFFSET_V2 — offset SO para as formas primitivas
        //
        //  A caixa, a esfera e a capsula sao geradas na origem do collider, e
        //  sem o offset ficariam centradas na origem do OBJETO — deslocadas da
        //  malha sempre que o pivo nao estiver no meio dela.
        //
        //  Malha exata e convex hull sao o oposto exato: a forma E a propria
        //  malha, e os vertices dela ja carregam a posicao. O
        //  ColliderDebugRenderer desenha esses dois multiplicando os vertices
        //  pela matriz que JA inclui o Offset — somar o centro ali deslocava o
        //  wireframe inteiro pelo valor do centro, que e exatamente o
        //  descolamento visto na pistola (centro 0.062, 0.003, -0.111).
        //
        //  O mesmo vale para a fisica: um shape de malha e construido a partir
        //  dos vertices, e um offset por cima moveria a colisao para longe do
        //  objeto que ela deveria representar.
        // ═══════════════════════════════════════════════════════════════════
        const bool shapeFollowsMesh = (out.Shape == ColliderShape::Mesh
            || out.Shape == ColliderShape::ConvexHull);

        out.Offset = shapeFollowsMesh ? glm::vec3(0.0f) : center;

        const float pad = imp.CollisionPadding;

        switch (out.Shape)
        {
        case ColliderShape::Box:
            out.HalfExtent = half + glm::vec3(pad);
            break;

        case ColliderShape::Sphere:
            // A esfera precisa CONTER a caixa, entao o raio e a diagonal, e
            // nao a maior aresta. Usar a aresta deixaria os cantos do objeto
            // para fora do collider.
            out.Radius = glm::length(half) + pad;
            break;

        case ColliderShape::Capsule:
            // Capsula em pe: altura no Y, raio pelo maior semi-eixo do plano
            // XZ. E a orientacao de personagem e de quase todo prop vertical.
            out.CapsuleRadius = std::max(half.x, half.z) + pad;
            out.Height = std::max(half.y * 2.0f + pad * 2.0f,
                out.CapsuleRadius * 2.0f);
            break;

        case ColliderShape::Mesh:
        case ColliderShape::ConvexHull:
            // A forma vem da propria malha; a caixa serve so de fallback para
            // quem desenhar o debug antes de a fisica cozinhar o shape.
            out.HalfExtent = half + glm::vec3(pad);
            break;
        }

        return true;
    }

    void AssetSpawnDefaults::Apply(entt::registry& registry, entt::entity entity,
        const std::string& assetUUID)
    {
        if (assetUUID.empty() || !registry.valid(entity)) return;

        const AssetRecord* record = AssetDatabase::Get().GetByUUID(assetUUID);
        if (!record) return;

        const AssetImportSettings& imp = record->Import;
        if (imp.IsDefault()) return;   // caminho comum: nada configurado

        // ── Material padrao ────────────────────────────────────────────────
        //
        // SOBRESCREVE o material que veio do arquivo, de proposito: quem
        // configurou um material padrao no asset esta dizendo justamente que o
        // do FBX nao serve. Vazio nao mexe em nada.
        if (!imp.DefaultMaterialUUID.empty())
        {
            if (auto material = ResolveMaterial(imp.DefaultMaterialUUID))
            {
                auto& mc = registry.get_or_emplace<MaterialComponent>(entity);
                mc.Data = material;
                mc.MaterialAssetUUID = imp.DefaultMaterialUUID;
            }
            else
            {
                // Avisar importa: o material padrao esta gravado no meta, e um
                // asset que sumiu daria um objeto com material errado e nenhuma
                // pista de que havia um padrao configurado.
                AXE_EDITOR_WARN("AssetSpawnDefaults: o material padrao de '{}' "
                    "(uuid {}) nao pode ser carregado.", record->Name,
                    imp.DefaultMaterialUUID);
            }
        }

        // ── Collider ───────────────────────────────────────────────────────
        //
        // Nunca substitui um collider ja presente. Nos caminhos de prefab e de
        // anexo a entidade pode chegar aqui com um collider proprio, e o
        // padrao do asset nao tem autoridade para apagar uma escolha explicita.
        if (imp.CollisionShape >= 0 && !registry.all_of<ColliderComponent>(entity))
        {
            const auto* meshComp = registry.try_get<MeshComponent>(entity);
            if (meshComp && meshComp->Data)
            {
                ColliderComponent collider;
                if (BuildCollider(*meshComp->Data, imp, collider))
                    registry.emplace<ColliderComponent>(entity, collider);
            }
        }
    }
}