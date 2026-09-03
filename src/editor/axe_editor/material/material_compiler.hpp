#pragma once
#include "axe/core/types.hpp"
#include "axe/material/material_cooked.hpp"   // PKG9 — CookedMaterialDomain
#include "editor/axe_editor/node_graph/material_graph.hpp"
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <filesystem>
namespace axe
{
    class Shader;
    // -------------------------------------------------------------------------
    // Resultado da compilação — contém os shaders prontos para uso
    // -------------------------------------------------------------------------
    struct CompiledMaterial
    {
        std::string VertexShader;
        std::string FragmentShader;
        std::string GeometryFragShader;
        bool        Success = false;
        std::string ErrorMessage;

        std::shared_ptr<Texture2D> AlbedoTexture;
        std::shared_ptr<Texture2D> NormalTexture;
        std::shared_ptr<Texture2D> RoughnessTexture;
        std::shared_ptr<Texture2D> MetallicTexture;
        std::map<std::string, std::shared_ptr<Texture2D>> SamplerTextures;

        // B4 — os mesmos samplers, como UUID de asset em vez de instância de
        // textura. É o que o `.axeshader` grava: o runtime resolve UUID via
        // AssetDatabase sem precisar do grafo. AlbedoSamplerName/
        // NormalSamplerName dizem QUAL sampler alimenta cada slot fixo do
        // Material ("" = nenhum) — gravado junto para o par GLSL/slot nunca
        // divergir do que foi compilado.
        std::map<std::string, std::string> SamplerTextureUUIDs;
        std::string AlbedoSamplerName;
        std::string NormalSamplerName;

        // true se o pin "Opacity" do Material Output estiver conectado a
        // algo — sinaliza que este material precisa do forward pass de
        // transparência (ver Material::IsTransparent).
        bool IsTransparent = false;

        // Masked (alpha test): recorta o pixel via discard quando a máscara
        // de opacidade fica abaixo do cutoff. Renderiza no passe OPACO
        // (deferred), com sombra e luz normais — não é o forward translúcido.
        bool  IsMasked = false;
        float AlphaCutoff = 0.5f;
    };

    // -------------------------------------------------------------------------
    // MaterialCompiler
    //
    // Percorre o MaterialGraph em ordem topológica (DFS reverso a partir do
    // Material Output node) e gera código GLSL para cada node conectado.
    //
    // Uso:
    //   auto result = MaterialCompiler::Compile(graph);
    //   if (result.Success)
    //       auto shader = Shader::Create(result.VertexShader, result.FragmentShader);
    // -------------------------------------------------------------------------
    class MaterialCompiler
    {
    public:
        static CompiledMaterial Compile(MaterialGraph* graph);

        // B4 — grava o resultado de Compile() no `.axeshader` irmão do
        // `.axemat` (via CookedMaterial::Save, formato do runtime). É isto que
        // o jogo carrega no lugar do callback de recompile. Chamado nos dois
        // pontos que compilam grafo de superfície: CompileAndApply (botão) e o
        // MaterialRecompileCallback do EditorLayer (load de cena) — abrir o
        // projeto no editor já cozinha os materiais das cenas abertas.
        // `bakedEmissive` vem de ComputeBakedEmissive, que precisa de GL ativo.
        static bool BakeToDisk(const CompiledMaterial& result,
            const std::filesystem::path& materialFilePath,
            const glm::vec3& bakedEmissive);

        // PKG9 — cozimento dos domínios que produzem SÓ shader + samplers
        // (Light Function e Particle). Não há Material para preencher: o
        // consumidor é uma luz ou um emitter, que guardam o shader direto.
        //
        // Chamado de CompileLightFunctionFromFile / CompileParticleFunctionFromFile
        // — os mesmos pontos que o editor já usa para resolver esses materiais
        // no load de cena. Abrir a cena no editor cozinha, como no B4.
        static bool BakeShaderToDisk(const CompiledMaterial& result,
            const std::filesystem::path& materialFilePath,
            CookedMaterialDomain domain);

        // Compila um grafo no domínio Light Function: gera um shader bem
        // menor que o de superfície — sem PBR, sem G-Buffer, sem depender
        // de UV/posição de uma malha real. Só resolve o que alimenta o pin
        // Emissive do Material Output. O resultado é avaliado uma vez por
        // frame (não por pixel da tela) e o valor lido de volta alimenta
        // Color/Intensity da luz — ver LightMaterialEvaluator.
        static CompiledMaterial CompileLightFunction(MaterialGraph* graph);

        // Variante do CompileLightFunction pro EMISSIVE MÉDIO do bake de
        // GI: mesmo corpo (só o pin Emissive), mas o VS espalha UVs pelo
        // quad (em vez do UV neutro fixo) e o FS grava emissive/8 (o
        // readback é LDR). Pin desconectado → Success=false: material sem
        // emissive não pode virar emissive branco por fallback.
        static CompiledMaterial CompileEmissiveAverage(MaterialGraph* graph);

        // One-liner pros call sites: compila + avalia num FBO 8x8 e
        // devolve a média (vec3(0) se o material não emite ou falhar).
        static glm::vec3 ComputeBakedEmissive(MaterialGraph* graph);

        // Helper único — carrega o .axegraph correspondente a um .axemat,
        // compila como Light Function e já cria o Shader pronto. Usado
        // tanto pelo Inspector (ao attachar/trocar o material de uma luz)
        // quanto ao recarregar a cena — UM lugar só pra essa lógica, pra
        // não repetir o mesmo código em vários pontos (igual já tínhamos
        // identificado como problema no SceneSerializer).
        // Retorna false em caso de falha (sem material, sem grafo, erro de
        // compilação) — outShader/outSamplers não são alterados nesse caso.
        static bool CompileLightFunctionFromFile(const std::filesystem::path& materialFilePath,
            std::shared_ptr<Shader>& outShader,
            std::map<std::string, std::shared_ptr<Texture2D>>& outSamplers);

        // Compila um grafo no domínio Particle: gera um shader para billboards
        // de partícula. O vertex shader recebe os mesmos atributos do
        // ParticleRenderer (center, corner, color, size, rotation) e o fragment
        // shader é gerado a partir do grafo — resolve Color (vec3) e Opacity
        // (float) do Material Output, com acesso a v_UV, v_Color (da partícula),
        // v_Age01 (0..1 ao longo da vida) e u_Time.
        static CompiledMaterial CompileParticleFunction(MaterialGraph* graph);

        // POSTPROCESS_DOMAIN_V1 — efeito de tela inteira. Resolve o pin
        // Emissive num quad que cobre o framebuffer. Ver a nota longa na
        // implementacao.
        static CompiledMaterial CompilePostProcess(MaterialGraph* graph);

        // POSTPROCESS_DOMAIN_V1 — usado pelo callback que o EditorLayer
        // registra no SceneSerializer. Recusa (false) material que nao seja
        // deste dominio, em vez de compila-lo como se fosse.
        static bool CompilePostProcessFromFile(const std::filesystem::path& materialFilePath,
            std::shared_ptr<Shader>& outShader,
            std::map<std::string, std::shared_ptr<Texture2D>>& outSamplers);

        static bool CompileParticleFunctionFromFile(const std::filesystem::path& materialFilePath,
            std::shared_ptr<Shader>& outShader,
            std::map<std::string, std::shared_ptr<Texture2D>>& outSamplers);

    private:
        MaterialCompiler(MaterialGraph* graph);

        // PKG9 — preenche result.SamplerTextureUUIDs a partir do mapa FINAL de
        // samplers do domínio. Ver a nota longa na implementação.
        static void CollectSamplerUUIDs(const MaterialCompiler& compiler,
            MaterialGraph* graph,
            const std::map<std::string, std::shared_ptr<Texture2D>>& finalSamplers,
            CompiledMaterial& result);

        // -- Percurso do grafo --
        void VisitNode(Node* node);  // DFS — processa node e seus inputs
        void VisitPin(Pin* pin);     // navega até o node fonte de um input

        // -- Geração de código --
        std::string GenerateNodeCode(Node* node); // gera a linha GLSL do node

        // -------------------------------------------------------------------------
        // PinValue — associa uma variável GLSL ao seu tipo semântico.
        //
        // Em vez de guardar só o nome ("tex_0.rgb"), guardamos também o tipo
        // (Vec3). Isso permite que nodes matemáticos (Multiply, Add, etc.)
        // determinem o tipo do resultado sem precisar analisar strings.
        //
        // Exemplos:
        //   { "float_0",    PinType::Float }
        //   { "tex_0.rgb",  PinType::Vec3  }
        //   { "color_1",    PinType::Vec4  }
        // -------------------------------------------------------------------------
        struct PinValue
        {
            std::string variable; // nome da variável no GLSL gerado
            PinType     type;     // tipo semântico do valor
        };

        // Registra um pin de output com seu nome e tipo
        void        RegisterPin(ed::PinId pinId, const std::string& variable, PinType type);

        // Retorna o nome GLSL de um pin registrado (ex: "tex_0.rgb")
        std::string GetPinVariable(ed::PinId pinId);

        // Retorna o tipo semântico de um pin registrado (ex: PinType::Vec3)
        PinType     GetPinType(ed::PinId pinId);

        // Converte PinType para string GLSL ("float", "vec2", "vec3", "vec4")
        std::string GetGLSLType(PinType type);

        // Gera nome de variável único: "mul_0", "tex_1", "float_2", etc.
        std::string MakeVar(const std::string& prefix);

        // CUSTOM_NODE_V1 — adapta a variavel de um pin de origem ao tipo que o
        // parametro da funcao Custom declara (float -> vec3 vira vec3(x),
        // vec4 -> vec3 vira .rgb, e assim por diante).
        //
        // Existe porque um node Custom e escrito A MAO: sem adaptacao, ligar um
        // Vec3 num parametro Float nao daria erro de GRAFO, daria erro de
        // compilacao de GLSL — dentro de uma funcao gerada, sem numero de linha
        // que corresponda a nada que o usuario tenha escrito. Adaptar e a
        // diferenca entre um aviso claro e uma caca ao tesouro.
        std::string AdaptToType(const std::string& expr, PinType from, PinType to);

        // -- Navegação de links --
        Node* GetSourceNode(Pin* inputPin); // node conectado ao input
        Pin* GetSourcePin(Pin* inputPin);  // pin de output conectado ao input

        // -- Estado interno --
        MaterialGraph* m_Graph;
        std::unordered_set<int>     m_VisitedNodes;  // nodes já processados
        std::unordered_map<int, PinValue> m_PinVariables; // pin ID → {nome, tipo}
        std::unordered_map<int, std::string> m_NodeSamplers; // node ID → sampler name

        // ── SRGB_TEXTURES_V1 ─────────────────────────────────────────────────
        //
        // IDs dos nodes Texture Sample cuja textura carrega COR (e nao dado).
        // Preenchido no passo 2 do Compile a partir do INDICE DO PIN do
        // Material Output pelo qual o node foi alcancado: 0 (Base Color) e 4
        // (Emissive) sao cor; Metallic, Roughness, Normal, Opacity, AO e
        // Specular sao dado.
        //
        // Isto NAO e heuristica de nome de arquivo — e a ligacao real do
        // grafo. Uma normal map alcancada pelo pin Normal nunca vai ser
        // decodificada como cor, que e o erro que estraga mais do que conserta.
        //
        // Consumido no GenerateNodeCode do Texture Sample. Ver a nota la.
        std::unordered_set<int> m_SRGBSamplers;

        // ── POSTPROCESS_DOMAIN_V1b ───────────────────────────────────────────
        //
        // true SO durante o CompilePostProcess. Diz se o shader QUE ESTA SENDO
        // GERADO AGORA declara u_SceneColor e u_ScreenSize.
        //
        // Na primeira versao isto era `m_Graph->Domain == PostProcess`, e o
        // erro foi de ESPECIE: o dominio e propriedade do GRAFO, mas as
        // uniforms sao propriedade do SHADER. O CompileAndApply compila o grafo
        // DUAS vezes — uma no compilador do dominio e outra no de Surface (para
        // o preview e para o material da cena) — e na segunda o node Scene
        // Color emitia texture(u_SceneColor, ...) num shader que nao declara
        // essa uniform. Dai o "'u_SceneColor' : undeclared identifier".
        //
        // Flag de INSTANCIA do compilador, e nao consulta ao grafo: cada
        // compilacao sabe o que ela propria emitiu.
        bool m_PostProcessTarget = false;

        // ── CUSTOM_NODE_V1 ───────────────────────────────────────────────────
        //
        // Corpo das FUNCOES GLSL geradas pelos nodes Custom, para ser inserido
        // ANTES do `void main()` de cada shader do dominio.
        //
        // Funcao de verdade, e nao expressao inline, por uma razao pratica: e o
        // que permite ao usuario escrever `return`, variaveis temporarias,
        // `if` e `for` — que e a diferenca entre "escrever um shader" e
        // "escrever uma conta". E a mesma escolha que a Unreal faz no node
        // Custom dela.
        //
        // So entram aqui os Custom REALMENTE ALCANCADOS pelo Material Output
        // (o acumulo acontece dentro do GenerateNodeCode, que so roda para node
        // visitado). Um Custom solto no canvas com codigo pela metade nao pode
        // impedir o material inteiro de compilar.
        std::string                 m_CustomFunctions;

        std::string                 m_FragmentCode;  // código acumulado
        int                         m_VariableCounter = 0; // contador para nomes únicos
    };

} // namespace axe