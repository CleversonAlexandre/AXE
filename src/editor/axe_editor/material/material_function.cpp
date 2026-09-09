#include "material_function.hpp"
#include "axe/log/log.hpp"

#include <algorithm>
#include <fstream>

namespace axe
{
    namespace
    {
        nlohmann::json ParamsToJson(const std::vector<MaterialFunctionParam>& params)
        {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& p : params)
                arr.push_back({ {"name", p.Name}, {"type", PinTypeToString(p.Type)} });
            return arr;
        }

        void ParamsFromJson(const nlohmann::json& arr,
            std::vector<MaterialFunctionParam>& out)
        {
            out.clear();
            if (!arr.is_array()) return;

            for (const auto& e : arr)
            {
                MaterialFunctionParam p;
                p.Name = e.value("name", std::string("In"));
                p.Type = PinTypeFromString(e.value("type", std::string("Float")));
                out.push_back(p);
            }
        }
    }

    MaterialFunction::MaterialFunction()
        : m_Graph(std::make_unique<MaterialGraph>())
    {}

    std::shared_ptr<MaterialFunction> MaterialFunction::Create(const std::string& name)
    {
        auto fn = std::make_shared<MaterialFunction>();
        fn->m_Name = name;

        // Uma funcao nasce util: uma entrada e uma saida ja no canvas, ligadas
        // uma na outra. Um `.axematfunc` vazio abriria numa tela em branco em
        // que nao ha nenhuma pista de que os nodes Function Input/Output sao o
        // que faz dele uma funcao.
        Node* in = fn->m_Graph->AddFunctionInputNode();
        Node* out = fn->m_Graph->AddFunctionOutputNode();

        fn->m_Graph->UpdateNodePosition(in->ID.Get(), ImVec2(-260.0f, 0.0f));
        fn->m_Graph->UpdateNodePosition(out->ID.Get(), ImVec2(160.0f, 0.0f));

        if (!in->Outputs.empty() && !out->Inputs.empty())
            fn->m_Graph->AddLink(in->Outputs[0].ID, out->Inputs[0].ID);

        // Sem isto os pinos ficam com ParentNode nulo e CanCreateLink recusa
        // qualquer ligacao nova. O Deserialize chama BuildNodes no fim, entao
        // um grafo VINDO DE ARQUIVO ja nasce ligado — o caminho de criacao do
        // zero e o unico que precisa chamar na mao. A mesma pegadinha ja
        // documentada no node Custom.
        fn->m_Graph->BuildNodes();

        fn->RebuildSignatureFromGraph(*fn->m_Graph);
        return fn;
    }

    void MaterialFunction::RebuildSignatureFromGraph(MaterialGraph& graph)
    {
        m_Inputs.clear();
        m_Outputs.clear();

        // Coleta com a posicao junto, para poder ordenar depois. Ordenar os
        // NODES do grafo no lugar seria pior: a ordem do vetor de nodes e
        // usada como identidade em outros lugares (o remapeamento de pins do
        // Deserialize casa por POSICAO no vetor).
        struct Entry { Node* node; ImVec2 pos; };
        std::vector<Entry> ins, outs;

        for (auto& node : graph.GetNodes())
        {
            if (node->Name == "Function Input")
                ins.push_back({ node.get(), graph.GetNodePosition(node->ID.Get()) });
            else if (node->Name == "Function Output")
                outs.push_back({ node.get(), graph.GetNodePosition(node->ID.Get()) });
        }

        auto byPosition = [](const Entry& a, const Entry& b)
            {
                if (a.pos.y != b.pos.y) return a.pos.y < b.pos.y;
                if (a.pos.x != b.pos.x) return a.pos.x < b.pos.x;
                return a.node->ID.Get() < b.node->ID.Get();
            };

        std::sort(ins.begin(), ins.end(), byPosition);
        std::sort(outs.begin(), outs.end(), byPosition);

        for (const auto& e : ins)
            m_Inputs.push_back({ e.node->StringValue, e.node->CustomOutputType });
        for (const auto& e : outs)
            m_Outputs.push_back({ e.node->StringValue, e.node->CustomOutputType });
    }

    bool MaterialFunction::Save(const std::filesystem::path& filepath, MaterialGraph& graph)
    {
        // A assinatura e SEMPRE derivada aqui, e nunca editada em separado.
        // Enquanto se edita, os nodes sao a fonte da verdade; o cabecalho e
        // um cache para o node de chamada nao ter que abrir o grafo.
        RebuildSignatureFromGraph(graph);

        nlohmann::json j;
        j["version"] = 1;
        j["name"] = m_Name;
        j["description"] = m_Description;
        j["inputs"] = ParamsToJson(m_Inputs);
        j["outputs"] = ParamsToJson(m_Outputs);
        j["graph"] = graph.Serialize();

        std::error_code ec;
        std::filesystem::create_directories(filepath.parent_path(), ec);

        std::ofstream file(filepath);
        if (!file.is_open())
        {
            AXE_CORE_ERROR("[MATFUNC_V1] nao consegui gravar '{}'.", filepath.string());
            return false;
        }

        file << j.dump(4);
        m_FilePath = filepath;
        return true;
    }

    bool MaterialFunction::Load(const std::filesystem::path& filepath, int idSeed)
    {
        std::ifstream file(filepath);
        if (!file.is_open())
        {
            AXE_CORE_ERROR("[MATFUNC_V1] nao consegui abrir '{}'.", filepath.string());
            return false;
        }

        nlohmann::json j;
        try { file >> j; }
        catch (const std::exception& e)
        {
            AXE_CORE_ERROR("[MATFUNC_V1] '{}' nao e um JSON valido: {}",
                filepath.string(), e.what());
            return false;
        }

        m_Name = j.value("name", filepath.stem().string());
        m_Description = j.value("description", std::string());

        if (j.contains("inputs"))  ParamsFromJson(j["inputs"], m_Inputs);
        if (j.contains("outputs")) ParamsFromJson(j["outputs"], m_Outputs);

        m_Graph = std::make_unique<MaterialGraph>();

        // A semente vai ANTES do Deserialize: e ele que chama GetNextID() para
        // cada node e cada pino reconstruido. Semear depois nao adiantaria
        // nada — os IDs ja teriam saido a partir de 1.
        if (idSeed > 0) m_Graph->SeedNextID(idSeed);

        if (j.contains("graph"))
            m_Graph->Deserialize(j["graph"]);

        m_FilePath = filepath;
        return true;
    }

    std::shared_ptr<MaterialFunction> MaterialFunction::LoadFromFile(
        const std::filesystem::path& filepath, int idSeed)
    {
        auto fn = std::make_shared<MaterialFunction>();
        if (!fn->Load(filepath, idSeed)) return nullptr;
        return fn;
    }

    bool MaterialFunction::ReadSignature(const std::filesystem::path& filepath,
        std::string& outName,
        std::vector<MaterialFunctionParam>& outInputs,
        std::vector<MaterialFunctionParam>& outOutputs)
    {
        std::ifstream file(filepath);
        if (!file.is_open()) return false;

        nlohmann::json j;
        try { file >> j; }
        catch (const std::exception&) { return false; }

        outName = j.value("name", filepath.stem().string());
        ParamsFromJson(j.contains("inputs") ? j["inputs"] : nlohmann::json::array(), outInputs);
        ParamsFromJson(j.contains("outputs") ? j["outputs"] : nlohmann::json::array(), outOutputs);
        return true;
    }

} // namespace axe