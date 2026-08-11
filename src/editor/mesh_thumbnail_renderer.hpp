#pragma once
#include "axe/core/types.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/renderer/viewport_renderer.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/scene_environment.hpp"
#include "axe/mesh/mesh.hpp"
#include <unordered_map>
#include <memory>
#include <string>
#include <filesystem>

namespace axe
{
    // ─────────────────────────────────────────────────────────────────────────
    //  MeshThumbnailRenderer — miniatura DO CONTEUDO, e nao do tipo do arquivo
    //
    //  O Asset Browser mostrava o mesmo cubo azul generico para toda malha,
    //  toda primitiva, toda animacao e todo script. Com uma dezena de assets
    //  ainda da para ler o nome embaixo; com cinquenta, a grade inteira e
    //  ruido e o nome vira a UNICA informacao — o icone deixa de ajudar a
    //  achar e passa a atrapalhar.
    //
    //  Textura e Material ja resolviam isto: a primeira usa a propria imagem, o
    //  segundo tem o MaterialThumbnailRenderer. Esta classe e o irmao dele para
    //  o que sobra, e segue a mesma arquitetura de proposito: cena minima
    //  propria, um framebuffer por asset, UM render por frame.
    //
    //  O limite de um por frame nao e preguica. Renderizar cinquenta
    //  miniaturas no frame em que a pasta abre daria um engasgo visivel toda
    //  vez que voce navegasse; espalhados, aparecem preenchendo a grade em
    //  meio segundo e ninguem percebe o custo.
    // ─────────────────────────────────────────────────────────────────────────
    class MeshThumbnailRenderer
    {
    public:
        void Initialize();

        // Enfileira o asset. Barato e idempotente: pode ser chamado por frame,
        // para todo item visivel, sem checagem do lado de quem chama.
        void Register(const std::string& uuid,
            const std::filesystem::path& filePath,
            int assetType);

        // TextureID do thumbnail, ou 0 enquanto nao houver. Zero significa
        // "use o icone generico" — nunca "espere".
        uint32_t GetThumbnail(const std::string& uuid);

        // Forca re-render. Chamar quando o asset muda no disco.
        void Invalidate(const std::string& uuid);

        // Chamar no OnRender, antes do ImGui.
        void RenderPending();

    private:
        // Resolve a MALHA que representa o asset. O tipo do arquivo decide onde
        // procurar; o resultado e sempre uma malha, ou nada.
        //
        //   .obj/.fbx      -> a propria malha
        //   primitiva      -> a fabrica
        //   .axeskel       -> a malha do esqueleto, em bind pose
        //   .axeanim       -> a malha do esqueleto que o clipe anima
        //   .axescript     -> a malha do componente Mesh/SkeletalMesh do script
        //
        // O clipe de animacao mostra o personagem parado, e nao um frame da
        // animacao: escolher "o frame certo" de uma animacao qualquer nao tem
        // resposta boa, e um frame arbitrario costuma pegar o corpo numa pose
        // ilegivel. A silhueta identifica de quem e o clipe, que e a pergunta
        // que o icone precisa responder.
        std::shared_ptr<Mesh> ResolveMesh(const std::filesystem::path& filePath,
            const std::string& uuid,
            int assetType);

        void RenderThumbnail(const std::string& uuid);

        // Enquadra a camera na malha. Mesmo fator 1.9 dos previews do editor —
        // miniaturas com enquadramento diferente do preview parecem outra coisa.
        void FrameCameraOn(const std::shared_ptr<Mesh>& mesh);

        struct Entry
        {
            std::shared_ptr<Framebuffer> Framebuffer;
            std::shared_ptr<Mesh>        MeshData;
            // Rotacao autorada no Script Editor, em graus. A miniatura de um
            // script deve mostrar o objeto como o autor o deixou — uma pistola
            // girada para ficar de lado no preview nao pode voltar a aparecer
            // deitada na grade.
            glm::vec3                    Rotation{ 0.0f };
            // SC32 — material autorado no script (nullptr = cinza padrao).
            std::shared_ptr<class Material> MaterialData;
            std::filesystem::path        FilePath;
            int                          AssetType = 0;
            bool  Resolved = false;   // ja tentou carregar a malha
            bool  Dirty = true;
            bool  Failed = false;   // sem malha; nao tenta de novo
        };

        std::unique_ptr<ViewportRenderer> m_Renderer;
        std::unique_ptr<Scene>            m_Scene;
        std::unique_ptr<SceneEnvironment> m_Environment;
        std::shared_ptr<class Material>   m_Material;
        entt::entity                      m_Entity = entt::null;
        bool                              m_Ready = false;
        // Carona entre ResolveMesh e RenderPending: ResolveMesh ja retorna a
        // malha, e um segundo valor de retorno so para o caso do script nao
        // justificava mudar a assinatura usada por todos os tipos.
        glm::vec3                         m_PendingRotation{ 0.0f };
        std::shared_ptr<class Material>   m_PendingMaterial;   // idem, SC32

        std::unordered_map<std::string, Entry> m_Cache;

        static constexpr uint32_t k_ThumbnailSize = 128;
    };

} // namespace axe