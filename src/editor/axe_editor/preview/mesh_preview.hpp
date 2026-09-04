#pragma once

#include "axe/mesh/mesh.hpp"
#include "axe/material/material.hpp"
#include "axe/physics/physics_components.hpp"

#include <entt/entt.hpp>
#include <imgui.h>
#include <glm/glm.hpp>
#include <memory>

namespace axe
{
    class Scene;
    class Framebuffer;
    class SceneEnvironment;
    class ViewportRenderer;
}

namespace axe::ui
{
    // ═══════════════════════════════════════════════════════════════════════
    //  MESH_PREVIEW_V1 — o preview 3D, uma vez so
    //
    //  ── POR QUE ISTO EXISTE COMO ARQUIVO PROPRIO ───────────────────────────
    //
    //  A engine ja tem QUATRO previews 3D: material, control rig, script graph
    //  e anim graph. Todos montam a mesma coisa a mao — Framebuffer, um
    //  ViewportRenderer em modo forward, uma Scene isolada com uma entidade e
    //  uma luz, o adiamento de resize, a apresentacao via ImGui::Image, o
    //  ViewGizmo no canto e o input de camera com Alt.
    //
    //  Sao umas 180 linhas repetidas quatro vezes. O Asset Viewer precisa da
    //  mesma coisa, e fazer a QUINTA copia e o erro que a propria base de
    //  codigo ja avisa contra (ver a nota em view_gizmo.hpp sobre as listas de
    //  tipo duplicadas): N copias de uma regra que o compilador nao confere.
    //
    //  ── O QUE ESTE PASSO FAZ, E O QUE NAO FAZ ─────────────────────────────
    //
    //  FAZ: nasce como o preview do Asset Viewer, ja escrito para servir aos
    //  outros. NAO FAZ: migrar os quatro que existem. Eles funcionam, e
    //  reescrever quatro janelas estaveis para provar um ponto de arquitetura
    //  e exatamente o risco que nao vale a pena agora — a migracao pode
    //  acontecer um por vez, quando cada um for mexido por outro motivo.
    //
    //  ── SEPARACAO ─────────────────────────────────────────────────────────
    //
    //  Zero GL aqui. Framebuffer e ViewportRenderer sao as abstracoes que o
    //  editor ja usa; quem fala OpenGL e o backend, do outro lado delas.
    // ═══════════════════════════════════════════════════════════════════════
    class MeshPreview
    {
    public:
        MeshPreview();
        ~MeshPreview();

        MeshPreview(const MeshPreview&) = delete;
        MeshPreview& operator=(const MeshPreview&) = delete;

        // Monta framebuffer, cena e renderer. Idempotente: chamar duas vezes
        // nao reconstroi nada. E CARO (cria um renderer e carrega o HDRI),
        // entao quem usa chama na primeira vez que o painel realmente aparece,
        // e nao no construtor da janela — senao toda janela fechada pagaria
        // por um preview que ninguem vai olhar.
        void Initialize();
        bool IsReady() const { return m_Ready; }

        // Troca a malha exibida. Material nulo usa um padrao cinza — malha de
        // FBX costuma vir sem material util, e um preview preto nao diz nada.
        void SetMesh(const std::shared_ptr<Mesh>& mesh,
            const std::shared_ptr<Material>& material = nullptr);

        void Clear();

        // ASSET_DEFAULTS_V1 — mostra (ou tira) o volume de colisao sobre a
        // malha. Ver o collider ANTES de aceita-lo e metade do valor de
        // configurar colisao por asset: "esfera" e uma palavra, a esfera
        // engolindo a pistola inteira e uma resposta.
        void SetCollider(const ColliderComponent& collider);
        void ClearCollider();

        // Enquadra os bounds dados: a camera recua ate o objeto caber, olhando
        // para o centro dele.
        //
        // Existe porque um preview de asset e o caso em que a escala do objeto
        // e DESCONHECIDA por definicao — pode ser uma pistola de 19 cm ou um
        // predio de 40 m, e uma distancia fixa deixaria um deles fora de
        // quadro. Chamado ao abrir e ao reimportar.
        void FrameBounds(const glm::vec3& boundsMin, const glm::vec3& boundsMax);

        // Desenha a cena no framebuffer. Tem de rodar ANTES do ImGui do frame
        // — o EditorLayer::OnRender ja faz isso para os outros previews, e
        // este entra na mesma lista. Renderizar durante o ImGui desenharia com
        // o alvo errado ligado.
        void Render();

        // Apresenta a imagem e trata o input. Devolve o retangulo ocupado, que
        // o chamador pode usar para overlays proprios.
        void Draw(const ImVec2& size, bool showGizmoTools = false);

        // Grade de 1 m. E o que torna a escala JULGAVEL: "0,19 m" e um numero,
        // mas a malha ao lado de um quadrado de um metro e uma resposta.
        bool ShowGrid = true;

        // Wireframe do collider. Com convex hull ele cobre a malha inteira de
        // linhas verdes e a textura fica ilegivel — que e exatamente quando se
        // quer olhar a textura. Poder apagar e parte de poder usar.
        bool ShowColliderWire = true;

    private:
        void HandleInput(const ImVec2& boundsMin, const ImVec2& boundsMax);

        bool m_Ready = false;

        std::unique_ptr<Scene>             m_Scene;
        std::unique_ptr<ViewportRenderer>  m_Renderer;
        std::unique_ptr<SceneEnvironment>  m_Environment;
        std::shared_ptr<Framebuffer>       m_Framebuffer;
        std::shared_ptr<Material>          m_DefaultMaterial;

        entt::entity m_Entity = entt::null;

        // VIEWPORT_RESIZE_V1 — o mesmo adiamento dos outros previews: o Draw
        // so ANOTA o tamanho novo e o Render aplica antes de desenhar.
        // Redimensionar o framebuffer no meio do ImGui invalida a textura que
        // o proprio ImGui vai apresentar no mesmo frame.
        ImVec2 m_Size{ 512.0f, 512.0f };
        ImVec2 m_PendingSize{ 0.0f, 0.0f };

        bool   m_Hovered = false;
        ImVec2 m_LastMousePos{ 0.0f, 0.0f };
        bool   m_HaveLastMouse = false;
    };
}
