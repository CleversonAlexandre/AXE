#pragma once
#include "axe/core/types.hpp"
#include "editor/axe_editor/node_graph/material_graph.hpp"
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <map>
namespace axe
{
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
    class AXE_API MaterialCompiler
    {
    public:
        static CompiledMaterial Compile(MaterialGraph* graph);
        
    private:
        MaterialCompiler(MaterialGraph* graph);

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

        // -- Navegação de links --
        Node* GetSourceNode(Pin* inputPin); // node conectado ao input
        Pin* GetSourcePin(Pin* inputPin);  // pin de output conectado ao input

        // -- Estado interno --
        MaterialGraph* m_Graph;
        std::unordered_set<int>     m_VisitedNodes;  // nodes já processados
        std::unordered_map<int, PinValue> m_PinVariables; // pin ID → {nome, tipo}
        std::unordered_map<int, std::string> m_NodeSamplers; // node ID → sampler name
        std::string                 m_FragmentCode;  // código acumulado
        int                         m_VariableCounter = 0; // contador para nomes únicos
    };

} // namespace axe