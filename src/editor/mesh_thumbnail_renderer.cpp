#include "mesh_thumbnail_renderer.hpp"
#include "axe/mesh/mesh_factory.hpp"
#include "editor/axe_editor/import/mesh_loader.hpp"
#include "axe/mesh/primitive_uuid.hpp"
#include "axe/scene/components.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/asset/asset.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/animation/skeletal_mesh_asset.hpp"
#include "axe/animation/skinned_mesh.hpp"
#include "axe/animation/pose.hpp"
#include "axe/animation/skeleton.hpp"
#include "axe/animation/animation_clip.hpp"
#include "axe/animation/animation_sampler.hpp"
// Miniatura do clipe assado: le as curvas direto do binario cozido.
#include "axe/animation/clip_cooked.hpp"
#include "editor/axe_editor/import/skeletal_mesh_loader.hpp"
#include "axe/animation/rig/control_rig_asset.hpp"
#include "axe/material/material_asset.hpp"
#include "editor/axe_editor/material/material_compiler.hpp"
#include "editor/axe_editor/node_graph/material_graph.hpp"
#include "axe/graphics/shader.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include "editor/axe_editor/script/script_asset.hpp"
// ScriptAsset tem "std::shared_ptr<ScriptGraph> m_Graph = std::make_shared<...>"
// como inicializador de membro, e script_asset.hpp so declara ScriptGraph
// adiante. Quem CONSTROI um ScriptAsset instancia esse make_shared e precisa do
// tipo completo — daqui vinha o "uses undefined class 'axe::ScriptGraph'"
// apontando para <xmemory>, que e onde o template acaba sendo expandido.
#include "editor/axe_editor/script/script_graph.hpp"
#include "axe/material/material.hpp"
#include "axe/log/log.hpp"
#include <algorithm>
#include <vector>

namespace axe
{
    namespace
    {
        // ── SC23: malha estatica a partir de uma SkinnedMesh ──────────────────
        //
        // SkeletalMeshAsset::GetMesh() devolve SkinnedMesh, que NAO deriva de
        // Mesh — sao pipelines diferentes: a estatica desenha direto, a skinada
        // passa por um compute de skinning alimentado por uma
        // SkinnedMeshInstance que o AnimationWorld cria.
        //
        // Montar essa maquinaria toda para uma imagem de 128px seria caro e
        // fragil (uma instancia sem clipe tocando pode nem produzir pose). Os
        // vertices na CPU ja estao em BIND POSE, que e exatamente o que a
        // miniatura deve mostrar — entao converte-se uma vez, no momento de
        // resolver, e o resto do renderer so conhece Mesh.
        //
        // O custo e uma copia por asset, feita uma unica vez e guardada no
        // cache junto com o framebuffer.
        std::shared_ptr<Mesh> MakeStaticFromSkinned(const std::shared_ptr<SkinnedMesh>& skinned)
        {
            if (!skinned) return nullptr;

            const auto& sv = skinned->GetVertices();
            const auto& si = skinned->GetIndices();
            if (sv.empty() || si.empty()) return nullptr;

            std::vector<Vertex> verts;
            verts.reserve(sv.size());
            for (const auto& v : sv)
            {
                Vertex out;
                out.Position = v.Position;
                out.Normal = v.Normal;
                out.TexCoord = v.TexCoord;
                out.Tangent = v.Tangent;
                out.Bitangent = v.Bitangent;
                verts.push_back(out);
            }

            return std::make_shared<Mesh>(verts, si);
        }

        // ── SC25: malha estatica NA POSE de um clipe ─────────────────────────
        //
        // Eu tinha argumentado contra amostrar um frame: "um frame arbitrario
        // costuma pegar o corpo numa pose ilegivel". Estava certo sobre o
        // frame, e errado sobre a conclusao — porque a alternativa que escolhi
        // faz TODOS os clipes ficarem em T-pose, ou seja, identicos. Um icone
        // que nao distingue nao esta cumprindo a funcao dele; entre uma pose
        // as vezes esquisita e vinte icones iguais, a pose ganha.
        //
        // 35% da duracao, e nao o inicio: quase todo clipe comeca perto da
        // pose neutra (e por isso o inicio seria a T-pose de novo) e termina
        // voltando para ela, quando e ciclico. Um terco adiante e onde um
        // passo esta aberto, um pulo esta no alto e um golpe esta estendido.
        //
        // Tudo na CPU: SamplePose e BuildSkinningMatrices vivem na axe.dll e
        // nao tocam OpenGL. O custo e uma passada pelos vertices, uma vez por
        // asset, dentro do orcamento de um thumbnail por frame que ja existia.
        std::shared_ptr<Mesh> MakePosedFromClip(const std::shared_ptr<SkinnedMesh>& skinned,
            const std::shared_ptr<Skeleton>& skeleton,
            const std::shared_ptr<AnimationClip>& clip)
        {
            if (!skinned || !skeleton || !clip) return nullptr;

            const auto& sv = skinned->GetVertices();
            const auto& si = skinned->GetIndices();
            if (sv.empty() || si.empty()) return nullptr;

            Pose pose;
            AnimationSampler::SamplePose(*skeleton, *clip, clip->GetDuration() * 0.35f, pose);

            std::vector<glm::mat4> skin;
            AnimationSampler::BuildSkinningMatrices(*skeleton, pose, skin);
            if (skin.empty()) return nullptr;

            std::vector<Vertex> verts;
            verts.reserve(sv.size());

            for (const auto& v : sv)
            {
                glm::mat4 m(0.0f);
                float total = 0.0f;

                for (int i = 0; i < 4; i++)
                {
                    const int b = v.BoneIDs[i];
                    const float w = v.Weights[i];
                    // BoneIDs nasce -1 para slot vazio, de proposito (ver
                    // SkinnedVertex): tratar -1 como bone 0 arrastaria o
                    // vertice pelo quadril em silencio.
                    if (b < 0 || w <= 0.0f || b >= (int)skin.size()) continue;
                    m += skin[b] * w;
                    total += w;
                }

                Vertex out;
                if (total > 0.0001f)
                {
                    out.Position = glm::vec3(m * glm::vec4(v.Position, 1.0f));
                    out.Normal = glm::normalize(glm::mat3(m) * v.Normal);
                }
                else
                {
                    // Vertice sem peso nenhum fica onde estava, e nao na
                    // origem — um unico vertice colapsado esticaria um
                    // triangulo pela tela inteira.
                    out.Position = v.Position;
                    out.Normal = v.Normal;
                }
                out.TexCoord = v.TexCoord;
                out.Tangent = v.Tangent;
                out.Bitangent = v.Bitangent;
                verts.push_back(out);
            }

            return std::make_shared<Mesh>(verts, si);
        }

        // Malha de um esqueleto, posada pelo primeiro clipe se houver.
        std::shared_ptr<Mesh> MeshFromSkeletalAsset(const std::shared_ptr<SkeletalMeshAsset>& a)
        {
            if (!a) return nullptr;
            const auto& clips = a->GetClips();
            if (!clips.empty())
                if (auto posed = MakePosedFromClip(a->GetMesh(), a->GetSkeleton(), clips[0]))
                    return posed;
            return MeshFromSkeletalAsset(a);
        }
    }

    void MeshThumbnailRenderer::Initialize()
    {
        m_Renderer = std::make_unique<ViewportRenderer>();
        m_Renderer->Initialize();
        m_Renderer->GetSceneRenderer()->SetDeferredSupported(false);
        m_Renderer->SetPickingEnabled(false);

        m_Scene = std::make_unique<Scene>();
        auto& reg = m_Scene->GetRegistry();

        // Luz de tres quartos: a direcao (-1,-1,-1) deixa uma face clara, uma
        // media e uma escura, e e o que faz uma silhueta ser LIDA como volume
        // numa imagem de 128px. Luz frontal chapa tudo e a miniatura vira uma
        // mancha de cor.
        auto light = m_Scene->CreateEntity("Light");
        auto& lc = reg.emplace<LightComponent>(light);
        lc.Data = std::make_shared<DirectionalLight>();
        lc.Data->Direction = glm::vec3(-1.0f, -1.0f, -1.0f);
        lc.Data->Color = glm::vec3(1.0f);
        lc.Data->Intensity = 3.0f;
        lc.Data->AmbientStrength = 0.35f;

        m_Entity = m_Scene->CreateEntity("Thumb");
        reg.emplace<MeshComponent>(m_Entity);

        // Cinza neutro e sem metal: o thumbnail e sobre a FORMA. Um material
        // colorido ou espelhado faria assets diferentes parecerem o mesmo.
        m_Material = std::make_shared<Material>(nullptr, "ThumbDefault");
        m_Material->UsePBR = true;
        m_Material->Metallic = 0.0f;
        m_Material->Roughness = 0.6f;
        m_Material->Color = glm::vec4(0.72f, 0.74f, 0.78f, 1.0f);
        reg.emplace<MaterialComponent>(m_Entity, m_Material);

        m_Renderer->SetScene(m_Scene.get());
        m_Environment = std::make_unique<SceneEnvironment>();

        // ── SC25: angulo de tres quartos ─────────────────────────────────────
        //
        // A camera nascia com yaw e pitch zerados, ou seja, de frente e na
        // altura do centro. Uma pistola vista assim e uma linha; um plano
        // desaparece; um cubo e um quadrado. Foi o que voce viu.
        //
        // Girar ~30 graus na horizontal e ~20 para baixo mostra tres faces de
        // qualquer objeto — e a mesma razao de todo catalogo de produto
        // fotografar em tres quartos em vez de de frente. Feito UMA vez aqui:
        // o SetView do enquadramento so mexe em foco e distancia, entao o
        // angulo sobrevive a cada thumbnail.
        // Rotate() aplica um RotationSpeed de 0.8 sobre o delta (e feito para
        // pixels de mouse, nao para angulos), entao o valor pedido e dividido
        // por ele. Sem isso o giro sai 20% menor que o pretendido — pouco o
        // bastante para passar despercebido e errado o bastante para o
        // enquadramento nao ser o mesmo em todo asset.
        constexpr float kRotSpeed = 0.8f;
        if (m_Renderer->m_Camera)
            m_Renderer->m_Camera->Rotate(glm::vec2(
                glm::radians(35.0f) / kRotSpeed,
                glm::radians(22.0f) / kRotSpeed));

        m_Ready = true;
    }

    void MeshThumbnailRenderer::Register(const std::string& uuid,
        const std::filesystem::path& filePath,
        int assetType)
    {
        if (uuid.empty()) return;
        if (m_Cache.count(uuid)) return;

        Entry e;
        e.FilePath = filePath;
        e.AssetType = assetType;

        FramebufferSpecification spec;
        spec.Width = k_ThumbnailSize;
        spec.Height = k_ThumbnailSize;
        e.Framebuffer = Framebuffer::Create(spec);

        m_Cache.emplace(uuid, std::move(e));
    }

    uint32_t MeshThumbnailRenderer::GetThumbnail(const std::string& uuid)
    {
        auto it = m_Cache.find(uuid);
        if (it == m_Cache.end()) return 0;
        // Dirty ou falho devolve 0 para o chamador cair no icone generico.
        // Devolver a textura antes do primeiro render mostraria um quadrado
        // preto, que parece asset corrompido.
        if (it->second.Dirty || it->second.Failed) return 0;
        return it->second.Framebuffer ? it->second.Framebuffer->GetColorAttachmentRendererID() : 0;
    }

    void MeshThumbnailRenderer::Invalidate(const std::string& uuid)
    {
        auto it = m_Cache.find(uuid);
        if (it == m_Cache.end()) return;
        it->second.Dirty = true;
        it->second.Failed = false;
        it->second.Resolved = false;
        it->second.MeshData.reset();
    }

    std::shared_ptr<Mesh> MeshThumbnailRenderer::ResolveMesh(
        const std::filesystem::path& filePath,
        const std::string& uuid,
        int assetType)
    {
        // ── Primitiva ────────────────────────────────────────────────────────
        // Vem antes de tudo: primitiva nao tem arquivo no disco, e qualquer
        // tentativa de abrir o caminho dela falharia.
        if (MeshFactory::IsPrimitive(uuid))
            return MeshFactory::CreateByUUID(uuid);

        const std::string ext = filePath.extension().string();

        // ── Esqueleto (.axeskel) ─────────────────────────────────────────────
        if (ext == ".axeskel")
        {
            if (auto asset = SkeletalMeshAsset::LoadFromFile(filePath); asset && asset->Resolve())
                return MeshFromSkeletalAsset(asset);
            return nullptr;
        }

        // ── Clipe de animacao (.axeanim) ─────────────────────────────────────
        //
        // O clipe nao tem malha propria; a que o representa e a do esqueleto
        // que ele anima. Procura-se um .axeskel de mesmo nome-base na mesma
        // pasta — e a convencao que o importador ja usa ("Y Bot.axeskel" +
        // "Y Bot.axeanim"). Nao achando, o asset fica com o icone generico:
        // adivinhar QUAL esqueleto entre varios seria pior que nao mostrar.
        if (ext == ".axeanim")
        {
            auto skelPath = filePath;
            skelPath.replace_extension(".axeskel");
            if (std::filesystem::exists(skelPath))
            {
                if (auto asset = SkeletalMeshAsset::LoadFromFile(skelPath); asset && asset->Resolve())
                    return MeshFromSkeletalAsset(asset);
            }
            return nullptr;
        }

        // ── Clipe ASSADO (.axeclipbin) ───────────────────────────────────────
        //
        // Aqui a pergunta "de qual esqueleto e este clipe?" tem resposta EXATA,
        // e nao uma deducao — ao contrario do FBX de animacao mais abaixo, que
        // depende de o projeto ter um unico `.axeskel`.
        //
        // O motivo: o bake REGISTRA o `.axeclipbin` como AnimEntry no `.axeskel`
        // do personagem. Entao basta perguntar a cada esqueleto "este arquivo e
        // teu?" — que e literalmente o que FindAnimationEntryBySource responde,
        // e o mesmo caminho que o duplo clique ja usa para abrir o Animation
        // Editor no clipe certo.
        //
        // (A saida definitiva para o caso do FBX e esta mesma: registrar o dono.
        // Aqui ele ja esta registrado, entao a heuristica nao precisa existir.)
        if (ext == ".axeclipbin")
        {
            for (const auto& [u, rec] : AssetDatabase::Get().GetAll())
            {
                if (rec.FilePath.extension() != ".axeskel") continue;

                auto a = SkeletalMeshAsset::LoadFromFile(rec.FilePath);
                if (!a) continue;

                if (a->FindAnimationEntryBySource(filePath) < 0) continue;
                if (!a->Resolve()) continue;

                // Relido do arquivo, e nao pescado de `a->GetClips()` por
                // indice: a lista de clipes do asset mistura os embutidos no
                // personagem com os de cada arquivo importado, e casar por
                // posicao ali e o tipo de suposicao que quebra no dia em que
                // alguem importa mais uma animacao.
                auto clips = ClipCooked::Read(filePath, *a->GetSkeleton());

                if (!clips.empty())
                    if (auto posed = MakePosedFromClip(a->GetMesh(), a->GetSkeleton(), clips[0]))
                        return posed;

                // Sem curvas legiveis: a silhueta do dono ainda identifica de
                // quem e o clipe, que e a pergunta que a miniatura responde.
                return MakeStaticFromSkinned(a->GetMesh());
            }

            return nullptr;
        }

        // ── Script (.axescript) ──────────────────────────────────────────────
        //
        // Mostra o corpo que o script define. Um BP_Weapon vira a pistola, um
        // BP_Player vira o personagem — que e exatamente a informacao que
        // distingue um script do outro na grade.
        if (ext == ".axescript")
        {
            ScriptAsset script;
            if (!script.Load(filePath)) return nullptr;

            // Rotacao raiz do script, guardada para o RenderThumbnail aplicar.
            m_PendingRotation = glm::vec3(script.RootRotX, script.RootRotY, script.RootRotZ);

            // ── SC32: material autorado no script ────────────────────────────
            //
            // Se o script tem um componente Material, a miniatura renderiza
            // com ELE — o cinza neutro vira fallback, nao regra. A pistola
            // que voce ve metalica no preview nao pode aparecer cinza na
            // grade: a miniatura existe para reconhecer o asset, e a cor e
            // parte do reconhecimento.
            //
            // Compilacao identica a do preview do Script Editor (o material e
            // um grafo; o shader nasce do MaterialCompiler). try/catch pelo
            // mesmo motivo de la: um .axegraph corrompido nao pode derrubar o
            // editor por causa de um icone — cai no material padrao.
            for (const auto& def : script.GetComponents())
            {
                if (def.Type != "Material" || def.AssetUUID.empty()) continue;

                if (const AssetRecord* r = AssetDatabase::Get().GetByUUID(def.AssetUUID))
                {
                    if (auto matAsset = MaterialAsset::LoadFromFile(r->FilePath))
                    {
                        auto graphPath = r->FilePath;
                        graphPath.replace_extension(".axegraph");
                        if (std::filesystem::exists(graphPath))
                        {
                            try
                            {
                                std::ifstream gf(graphPath);
                                auto gj = nlohmann::json::parse(gf);
                                auto matGraph = std::make_unique<MaterialGraph>();
                                matGraph->Deserialize(gj);
                                auto result = MaterialCompiler::Compile(matGraph.get());
                                if (result.Success)
                                {
                                    auto shader = Shader::Create(result.VertexShader, result.FragmentShader);
                                    if (shader) matAsset->GetMaterial()->SetShader(shader);
                                }
                            }
                            catch (...) {}
                        }
                        m_PendingMaterial = matAsset->GetMaterial();
                    }
                }
                break;
            }

            for (const auto& def : script.GetComponents())
            {
                if (def.AssetUUID.empty()) continue;

                if (def.Type == "SkeletalMesh")
                {
                    if (const AssetRecord* r = AssetDatabase::Get().GetByUUID(def.AssetUUID))
                        if (auto a = SkeletalMeshAsset::LoadFromFile(r->FilePath); a && a->Resolve())
                            return MeshFromSkeletalAsset(a);
                }
                else if (def.Type == "Mesh")
                {
                    if (auto m = MeshFactory::ResolveByUUID(def.AssetUUID)) return m;
                }
            }
            return nullptr;   // script sem corpo — icone de classe continua valendo
        }

        // ── Control Rig (.axerig) ────────────────────────────────────────────
        //
        // SC32 — o rig referencia o esqueleto por UUID (GetSkeletonUUID); a
        // miniatura e o personagem desse esqueleto em BIND POSE — de
        // proposito, e nao um "frame do rig": rig e autorado em bind pose e e
        // assim que o proprio editor de rig abre o preview. Avaliar o grafo do
        // rig aqui exigiria montar o runtime de rig inteiro para uma imagem de
        // 128px, e o resultado seria a mesma silhueta.
        if (ext == ".axerig")
        {
            if (auto rigAsset = ControlRigAsset::LoadFromFile(filePath))
                if (const AssetRecord* r = AssetDatabase::Get().GetByUUID(rigAsset->GetSkeletonUUID()))
                    if (auto a = SkeletalMeshAsset::LoadFromFile(r->FilePath); a && a->Resolve())
                        return MakeStaticFromSkinned(a->GetMesh());
            return nullptr;
        }

        // ── Malha importada (.obj/.fbx/...) ──────────────────────────────────
        if (assetType == (int)AssetType::Mesh && std::filesystem::exists(filePath))
        {
            // quiet=true: isto e uma SONDAGEM. Nao achar malha aqui e um
            // resultado esperado (o arquivo pode ser so animacao) e leva ao
            // fallback logo abaixo — nao e falha que peca acao de ninguem.
            if (auto m = MeshLoader::Load(filePath.string(), true).MeshData)
                return m;

            // SC24 — FBX SEM geometria: e um clipe de animacao exportado
            // sozinho (Running.fbx, Falling Idle.fbx). Nao ha o que desenhar
            // nele, e por isso ficavam com o icone generico enquanto os
            // .axeanim ao lado ja mostravam o personagem.
            //
            // O clipe nao guarda a qual esqueleto pertence — o importador nao
            // grava isso. Entao so ha resposta quando ela e UNICA: se o
            // projeto tem exatamente um .axeskel, e ele. Com dois ou mais, o
            // icone generico volta, porque mostrar o personagem errado num
            // clipe e pior do que nao mostrar nenhum.
            //
            // A saida definitiva e o importador registrar o esqueleto no
            // .axemeta do clipe; ai isto vira uma consulta em vez de uma
            // deducao.
            const AssetRecord* onlySkel = nullptr;
            int skelCount = 0;
            for (const auto& [u, rec] : AssetDatabase::Get().GetAll())
            {
                if (rec.FilePath.extension() != ".axeskel") continue;
                onlySkel = &rec;
                if (++skelCount > 1) break;
            }

            if (skelCount == 1 && onlySkel)
                if (auto a = SkeletalMeshAsset::LoadFromFile(onlySkel->FilePath); a && a->Resolve())
                {
                    // SC32 — a pose vem do PROPRIO arquivo de animacao.
                    //
                    // Antes este caminho devolvia MeshFromSkeletalAsset, que
                    // posa com o PRIMEIRO clipe do esqueleto — o mesmo para
                    // todo arquivo. Resultado: Running, Walking, idle e
                    // Jumping ganhavam a mesma imagem, e uma miniatura que
                    // nao distingue e apenas um icone generico mais caro.
                    //
                    // LoadClips religa as curvas DESTE fbx ao esqueleto por
                    // nome de bone — o mesmo fluxo que o importador ja usa.
                    auto clips = SkeletalMeshLoader::LoadClips(
                        filePath.string(), *a->GetSkeleton());

                    if (!clips.empty())
                        if (auto posed = MakePosedFromClip(a->GetMesh(), a->GetSkeleton(), clips[0]))
                            return posed;

                    return MakeStaticFromSkinned(a->GetMesh());
                }

            return nullptr;
        }

        return nullptr;
    }

    void MeshThumbnailRenderer::FrameCameraOn(const std::shared_ptr<Mesh>& mesh)
    {
        if (!mesh || !m_Renderer || !m_Renderer->m_Camera) return;

        const auto& verts = mesh->GetVertices();
        if (verts.empty()) return;

        glm::vec3 mn = verts[0].Position, mx = verts[0].Position;
        for (const auto& v : verts)
        {
            mn = glm::min(mn, v.Position);
            mx = glm::max(mx, v.Position);
        }

        const glm::vec3 size = mx - mn;
        const glm::vec3 focal = (mn + mx) * 0.5f;
        const float extent = std::max({ size.x, size.y, size.z, 0.0001f });

        // Aqui a maior das TRES dimensoes manda, ao contrario do preview do
        // personagem: a miniatura e quadrada e nao pode cortar nada, entao o
        // enquadramento tem de caber no eixo mais largo, seja ele qual for.
        m_Renderer->m_Camera->SetView(focal, extent * 1.9f);
    }

    void MeshThumbnailRenderer::RenderThumbnail(const std::string& uuid)
    {
        auto& e = m_Cache[uuid];
        auto& reg = m_Scene->GetRegistry();

        auto& mc = reg.get<MeshComponent>(m_Entity);
        mc.Data = e.MeshData;
        mc.AssetUUID = uuid;

        if (auto* tc = reg.try_get<TransformComponent>(m_Entity))
            tc->Data.Rotation = glm::radians(e.Rotation);

        // Material do asset quando ha, cinza neutro quando nao. Por ENTRY, e
        // nao global: a entidade e uma so e reusada por todos os thumbnails.
        if (auto* mat = reg.try_get<MaterialComponent>(m_Entity))
            mat->Data = e.MaterialData ? e.MaterialData : m_Material;

        FrameCameraOn(e.MeshData);

        m_Renderer->SetScene(m_Scene.get());
        m_Renderer->RenderToFramebuffer(*e.Framebuffer,
            k_ThumbnailSize, k_ThumbnailSize, 0.0f);
    }

    void MeshThumbnailRenderer::RenderPending()
    {
        if (!m_Ready) return;

        for (auto& [uuid, e] : m_Cache)
        {
            if (!e.Dirty || e.Failed) continue;

            // A malha e carregada AQUI, e nao no Register, de proposito: o
            // Register e chamado para todo item visivel a cada frame, e ler um
            // .fbx de dentro dele travaria a navegacao na pasta. Aqui o custo
            // ja esta limitado a um asset por frame.
            if (!e.Resolved)
            {
                m_PendingRotation = glm::vec3(0.0f);
                m_PendingMaterial = nullptr;
                e.MeshData = ResolveMesh(e.FilePath, uuid, e.AssetType);
                e.Rotation = m_PendingRotation;
                e.MaterialData = m_PendingMaterial;
                e.Resolved = true;

                if (!e.MeshData)
                {
                    // Falha permanente e silenciosa: o asset simplesmente fica
                    // com o icone generico. Um warn por asset sem malha
                    // encheria o log a cada abertura de pasta com ruido que
                    // nao pede acao nenhuma.
                    e.Failed = true;
                    e.Dirty = false;
                    continue;
                }
            }

            RenderThumbnail(uuid);
            e.Dirty = false;
            break;   // um por frame — ver o comentario no header
        }
    }

} // namespace axe