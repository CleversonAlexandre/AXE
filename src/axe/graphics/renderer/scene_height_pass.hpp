#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include <memory>

// =============================================================================
//  SCENE_HEIGHT_V1 — o mapa de topo da cena
//
//  ── O PROBLEMA QUE ESTE PASSE RESOLVE ────────────────────────────────────
//
//  Tudo que o material sabia sobre "o que esta atras/embaixo" vinha do
//  G-Buffer, e o G-Buffer so conhece o que a CAMERA ESTA VENDO. Medir
//  distancia com ele da um numero que muda quando a camera gira: a espuma de
//  margem virava um rastro descendo pela tela, e depois uma cunha atras de
//  cada objeto — a sombra do objeto vista do ponto de vista da camera,
//  projetada na agua.
//
//  Nao ha conta que conserte isso. A informacao que falta — o que existe fora
//  do campo de visao — nao esta em lugar nenhum na hora do fragment.
//
//  ── COMO A UNREAL RESOLVE, E COMO ISTO SE COMPARA ────────────────────────
//
//  A Unreal nao resolve com matematica: resolve com DADO PRE-COMPUTADO. Os
//  Mesh Distance Fields dela sao SDFs assados offline por malha, em textura de
//  volume, ate 8 MB por malha a 128^3, e a documentacao diz explicitamente que
//  "cannot be done at runtime". O sistema de agua dela nem usa isso — usa uma
//  render das informacoes de agua vista de cima.
//
//  Este passe e a segunda coisa: uma render ortografica DE CIMA PARA BAIXO,
//  como a shadow map faz com a luz. Mesmo molde, mesma ideia, custo muito
//  menor que um campo de distancia volumetrico — e e a ferramenta certa para
//  superficie de agua, que e um problema de duas dimensoes.
//
//  ── O QUE ELE PRODUZ ─────────────────────────────────────────────────────
//
//  Duas texturas cobrindo um quadrado do mundo centrado na camera:
//
//    HeightMap (R32F)  — o Y de mundo da geometria mais alta naquela coluna
//                        XZ, ou um valor sentinela muito negativo onde nao ha
//                        nada. Da a profundidade VERTICAL da agua, e ela
//                        independe do angulo da camera.
//
//    SeedMap (RG32F)   — o XZ de mundo da geometria MAIS PROXIMA daquele
//                        ponto, resultado de um jump flood sobre o height map.
//                        Com ele o material calcula distancia horizontal ate a
//                        margem sem depender do que a camera enxerga.
//
//  Guardar a SEMENTE em vez da distancia ja pronta e deliberado: a distancia
//  sai exata, em metros, calculada contra a posicao real do pixel, em vez de
//  ficar presa a resolucao do texel.
//
//  ── O QUE ELE NAO E ──────────────────────────────────────────────────────
//
//  Nao e receita de espuma. E fonte de DADO, da mesma familia do Scene Depth e
//  do Scene World Position: responde "o que tem embaixo" e "quao longe esta a
//  geometria". Como isso vira espuma, cor ou o que for continua sendo montado
//  no grafo pelo autor.
// =============================================================================

namespace axe
{
    class Mesh;

    // ── SCENE_HEIGHT_V6 — a FATIA, e de onde ela vem ─────────────────────────
    //
    //  O mapa mede distancia ate a linha onde a cena cruza um plano horizontal.
    //  Esse plano tem uma altura, e ela precisa ser conhecida ANTES da
    //  semeadura — senao o jump flood escolhe a semente mais proxima entre
    //  TODA a geometria, e a margem de verdade atras de um objeto irrelevante
    //  deixa de existir para sempre. Nenhum teste no material desfaz isso.
    //
    //  De onde vem a altura: da PROPRIA SUPERFICIE que le o mapa. O
    //  SceneRenderer varre a fila, acha os draw calls cujo material usa o node
    //  Scene Height, e usa o Y do transform deles. Nao ha ajuste de cena, nao
    //  ha campo de "agua" em painel nenhum, e o passe so roda se algum material
    //  realmente precisar dele.
    //
    //  E o passe continua sem saber o que e agua: ele responde "onde a cena
    //  cruza o plano Y = SliceY", que serve igual para lava, mare, neblina
    //  baixa ou campo de forca.

    class AXE_API SceneHeightPass
    {
    public:
        virtual ~SceneHeightPass() = default;

        virtual void Initialize(uint32_t resolution = 1024) = 0;

        // SCENE_HEIGHT_V6 — a altura da superficie que vai ler este mapa.
        // Vale para o Begin seguinte; chamar todo frame e barato.
        virtual void SetSlice(float y) = 0;

        virtual void Begin(const glm::mat4& topDownMatrix) = 0;
        virtual void DrawMesh(const Mesh& mesh, const glm::mat4& model) = 0;

        // O jump flood roda AQUI, depois do ultimo DrawMesh — precisa do height
        // map inteiro para saber onde estao as sementes.
        virtual void End() = 0;

        virtual uint32_t         GetHeightMapID() const = 0;
        virtual uint32_t         GetSeedMapID()   const = 0;
        virtual const glm::mat4& GetMatrix()      const = 0;
        virtual bool             IsInitialized()  const = 0;

        // Factory — igual ao ShadowMapPass::Create
        static std::shared_ptr<SceneHeightPass> Create();

        // ── Utilitario matematico, sem OpenGL ────────────────────────────────
        //
        //  Ortografica olhando para baixo, cobrindo um quadrado de lado
        //  2*extent centrado no XZ dado.
        //
        //  O centro e ENCAIXADO NA GRADE DE TEXEL de proposito. Sem isso, mover
        //  a camera um centimetro desloca o conteudo do mapa por uma fracao de
        //  texel, e a borda da espuma FERVE — o mesmo cintilar que a shadow map
        //  em cascata tem quando nao se faz este arredondamento.
        static glm::mat4 CalcTopDownMatrix(const glm::vec3& center,
            float extent,
            uint32_t resolution,
            float height = 500.0f);
    };
}