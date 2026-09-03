#pragma once
// material_cooked.hpp — o `.axeshader`: shader de material COZIDO.
//
// ── POR QUE ESTE ARQUIVO EXISTE ──────────────────────────────────────────────
//
// O shader de um material nasce do grafo (`.axegraph`) via MaterialGraph +
// MaterialCompiler — os dois em `src/editor/`. O jogo nao linka o editor, e
// por isso o SceneSerializer chamava um CALLBACK que so o EditorLayer
// registra. No game.exe o callback e nulo: o `material_asset_uuid` era lido e
// nada acontecia — o material ficava nos defaults e o personagem saia branco.
//
// E a mesma familia do B2 (assimp): o runtime dependendo de uma ferramenta de
// AUTORIA para carregar CONTEUDO. E a solucao e a mesma do `.axemesh` /
// `.axeskelbin` / `.axeclipbin`: o editor COZINHA o resultado num arquivo ao
// lado do asset, e o runtime so LE.
//
// ── O FORMATO ────────────────────────────────────────────────────────────────
//
// `X.axeshader`, irmao do `X.axemat` (mesma relacao do `X.axegraph`). JSON,
// como todo asset de texto da engine:
//
//   {
//     "version": 1,
//     "vertex":            "<GLSL>",
//     "fragment":          "<GLSL forward>",
//     "geometry_fragment": "<GLSL deferred>"  (vazio se nao houver),
//     "samplers":        { "u_AlbedoMap": "<uuid textura>", ... },
//     "albedo_sampler":    "u_AlbedoMap"  (qual sampler e o albedo),
//     "normal_sampler":    "u_Texture_1"  (idem para a normal, "" se nenhum),
//     "is_transparent":    bool,
//     "is_masked":         bool,
//     "alpha_cutoff":      float,
//     "baked_emissive":    [r, g, b]
//   }
//
// As texturas viajam como UUID e nao como caminho: quem resolve UUID -> path
// e o AssetDatabase, que o jogo ja carrega. Os nomes de sampler ("u_AlbedoMap",
// "u_Texture_N") sao os que o MaterialCompiler gerou DENTRO do GLSL gravado —
// gravar os dois juntos e o que mantem o par consistente para sempre.
//
// ── QUEM ESCREVE, QUEM LE ────────────────────────────────────────────────────
//
//   escreve — o EDITOR, nos mesmos pontos em que compila o grafo:
//             MaterialEditorWindow::CompileAndApply (botao Compile/Save) e o
//             MaterialRecompileCallback do EditorLayer (load de cena). Abrir o
//             projeto no editor uma vez ja cozinha os materiais das cenas.
//
//   le      — o RUNTIME, no SceneSerializer, QUANDO o callback e nulo (ou
//             seja: no jogo). No editor o callback continua mandando, porque
//             la o grafo pode ter mudado depois do ultimo cozimento.
//
// Save() mora aqui no runtime — e nao no editor — pela mesma razao do
// skeletal_cooked (B2.2): o formato pertence a quem o LE. O editor e apenas
// um cliente que o preenche. A dependencia aponta editor -> runtime, nunca o
// contrario.
//
// ── FRONTEIRA ────────────────────────────────────────────────────────────────
//
// Nada aqui inclui MaterialGraph, MaterialCompiler ou qualquer header de
// `src/editor/`. Se um dia um include desses aparecer, o empacotamento
// quebrou de novo — e este comentario e o aviso.

#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace axe
{
    class Material;
    class Shader;
    class Texture2D;

    // PKG9 — em qual DOMINIO este cozido foi compilado.
    //
    // Um `.axemat` tem um dominio so (o `.axegraph` guarda o campo `domain`), e
    // o GLSL gerado e completamente diferente em cada um: superficie escreve o
    // G-Buffer, light function so resolve o pin Emissive num quad minimo,
    // particle desenha um billboard. Trocar um pelo outro nao da erro de
    // carregamento — da imagem errada, que e pior.
    //
    // Por isso o dominio viaja DENTRO do arquivo e e conferido na leitura:
    // quem pede um shader de luz e recebe um de superficie ouve isso no log.
    enum class CookedMaterialDomain
    {
        Surface,
        LightFunction,
        Particle,

        // POSTPROCESS_DOMAIN_V1 — efeito de tela inteira escrito no grafo.
        //
        // Aqui a POSICAO no enum nao importa para o arquivo: o `.axeshader`
        // grava o dominio como STRING ("surface", "particle", ...) via
        // DomainName/DomainFromName, justamente para nao amarrar o formato a
        // uma ordem de enum. Quem NAO tem essa protecao e o MaterialDomain do
        // editor, serializado por indice no `.axegraph` — la, entrada nova so
        // no fim.
        //
        // Ao adicionar um dominio aqui: DomainName E DomainFromName, em
        // material_cooked.cpp. Esquecer o segundo faz o cozido carregar como
        // "surface" com um aviso no log, em vez de falhar.
        PostProcess,
    };

    // ═════════════════════════════════════════════════════════════════════════
    //  SHADING_MODEL_V1 — o modelo de sombreamento viaja no G-Buffer
    //
    //  ── O PROBLEMA ──────────────────────────────────────────────────────────
    //
    //  `MaterialShadingModel` existia no dropdown do Material Editor e era
    //  serializado no `.axegraph`, mas o MaterialCompiler NUNCA lia o campo.
    //  Escolher "Unlit" nao mudava um pixel. Era menu sem fio ligado.
    //
    //  ── POR QUE O ID MORA AQUI, E NAO NO ENUM DA UI ─────────────────────────
    //
    //  `MaterialShadingModel` (node_types.hpp, no EDITOR) tem 12 entradas, das
    //  quais 10 sao placeholders da Unreal que o motor nao implementa. Mandar
    //  aquele indice para o G-Buffer amarraria o formato de runtime a uma
    //  ordem de menu — bastaria alguem reordenar o dropdown para todo material
    //  ja gravado passar a ser sombreado de outro jeito.
    //
    //  Entao existem DOIS enums de proposito: o da UI, que pode crescer e
    //  reordenar a vontade, e este, que e o CONTRATO entre o GLSL gerado pelo
    //  editor e o lighting pass do axe.dll. O compilador traduz um no outro num
    //  ponto so.
    //
    //  ── ONDE ELE VIAJA ──────────────────────────────────────────────────────
    //
    //  No canal .b do attachment 3 do G-Buffer (o `g_PBR`), que era `vec2` num
    //  alvo RGBA8: .b e .a estavam ALOCADOS e sem uso. Nenhum attachment novo,
    //  nenhum byte a mais de banda.
    //
    //  ATENCAO: .b e .a nao eram zero, eram INDEFINIDOS — um `out vec2` nao
    //  escreve os outros canais. Por isso TODOS os escritores de g_PBR viraram
    //  vec4 na mesma rodada (o shader fixo do geometry pass e o gerado pelo
    //  compilador). Se um dia aparecer um terceiro escritor e ele esquecer o
    //  .b, aquele material vai ser sombreado por lixo de memoria.
    //
    //  Material antigo grava 0 = DefaultLit e continua identico.
    // ═════════════════════════════════════════════════════════════════════════
    enum class ShadingModelID : int
    {
        DefaultLit = 0,  // Cook-Torrance completo — o caminho de sempre
        Unlit = 1,  // sem luz nenhuma: albedo + emissive direto
        Toon = 2,  // difusa quantizada em bandas + especular de corte duro
    };

    // Fator de codificacao do ID num canal UNORM de 8 bits. O decode e
    // `int(v * 255.0 + 0.5)`, e o +0.5 nao e decoracao: sem ele, o
    // arredondamento para baixo transformaria um 2 que voltou como 1.9999 em 1.
    constexpr float kShadingModelEncodeScale = 1.0f / 255.0f;

    // Bandas do Toon viajam no .a do mesmo texel, normalizadas por 16 — o
    // limite superior util (acima de ~8 degraus o resultado ja e indistinguivel
    // de sombreamento continuo, que e justamente o que o Toon nao quer).
    constexpr float kToonStepsMax = 16.0f;

    struct CookedMaterialData
    {
        CookedMaterialDomain Domain = CookedMaterialDomain::Surface;

        std::string VertexShader;
        std::string FragmentShader;
        std::string GeometryFragShader;   // vazio = material sem passe deferred

        // nome do sampler no GLSL -> UUID da textura no AssetDatabase
        std::map<std::string, std::string> SamplerTextureUUIDs;

        // Quais samplers alimentam os slots fixos do Material. Vazio = nenhum.
        std::string AlbedoSamplerName;
        std::string NormalSamplerName;

        bool  IsTransparent = false;
        bool  IsMasked = false;
        float AlphaCutoff = 0.5f;

        glm::vec3 BakedEmissive{ 0.0f };
    };

    class AXE_API CookedMaterial
    {
    public:
        // Caminho do cozido a partir do caminho do `.axemat`.
        static std::filesystem::path PathFor(const std::filesystem::path& materialPath);

        // Grava/le o JSON. Save e chamado pelo editor; Load pelo runtime.
        static bool Save(const std::filesystem::path& path, const CookedMaterialData& data);
        static bool Load(const std::filesystem::path& path, CookedMaterialData& outData);

        // Cria os shaders via Shader::Create, resolve as texturas via
        // AssetDatabase e preenche o Material — o MESMO conjunto de campos que
        // o MaterialRecompileCallback do editor preenche, para que editor e
        // jogo mostrem o mesmo pixel.
        static bool ApplyToMaterial(const CookedMaterialData& data, Material& material);

        // Conveniencia do SceneSerializer: UUID do MaterialAsset -> acha o
        // `.axeshader` irmao -> Load + ApplyToMaterial. false = nao havia
        // cozido (o chamador loga o aviso com contexto).
        static bool LoadAndApply(const std::string& materialAssetUUID, Material& material);

        // PKG9 — o mesmo, para os dominios que produzem SO shader + samplers.
        //
        // A assinatura e de proposito identica a dos callbacks
        // `LightMaterialRecompileCallback` / `ParticleMaterialRecompileCallback`
        // do SceneSerializer: no editor o callback compila do grafo, no jogo
        // esta funcao le o cozido, e o chamador nao precisa saber em qual dos
        // dois esta. Uma luz guarda `LightMaterialShader` + `LightMaterialSamplers`;
        // um emitter guarda os dele. Nenhum dos dois tem Material para preencher.
        //
        // `expectedDomain` NAO e formalidade: e o que impede um cozido de
        // superficie de ser entregue a uma luz.
        static bool LoadShaderAndSamplers(const std::string& materialAssetUUID,
            CookedMaterialDomain expectedDomain,
            std::shared_ptr<Shader>& outShader,
            std::map<std::string, std::shared_ptr<Texture2D>>& outSamplers);

        // Nome do dominio no JSON e no log. Fora da classe seria uma segunda
        // tabela para divergir da primeira.
        static const char* DomainName(CookedMaterialDomain domain);
    };

} // namespace axe