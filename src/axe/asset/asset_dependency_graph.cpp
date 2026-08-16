#include "axe/asset/asset_dependency_graph.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/log/log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <deque>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
    // Extensoes derivadas que acompanham um asset no build.
    //
    // Sao arquivos gerados (B2.1–B2.3) e nao aparecem no AssetDatabase — a
    // extensao deles nem esta no mapa de tipos, de proposito, para nao poluir o
    // Asset Browser. Justamente por isso o empacotador precisa saber deles
    // AQUI: ninguem mais os conhece.
    const char* kDerivedExtensions[] =
    {
        ".axemesh",     // B2.1 — malha estatica
        ".axeskelbin",  // B2.2 — malha skinada + esqueleto
        ".axeclipbin",  // B2.3 — curvas
        ".axemeta",     // UUID e tipo do asset
        ".axegraph",    // grafo do Material Editor (irmao do .axemat)
        ".axeshader",   // B4 — shader cozido do material (irmao do .axemat)
    };

    // Percorre um JSON recursivamente e entrega cada string encontrada.
    template <typename Fn>
    void ForEachString(const nlohmann::json& j, Fn&& fn)
    {
        if (j.is_string())
        {
            fn(j.get_ref<const std::string&>());
            return;
        }

        if (j.is_object())
        {
            for (auto it = j.begin(); it != j.end(); ++it)
            {
                // A CHAVE tambem e varrida.
                //
                // Nao e zelo excessivo: o bloco `clip_meta` do .axeskel usa o
                // NOME DO CLIPE como chave, e um formato futuro pode
                // perfeitamente usar um UUID assim. Varrer so os valores
                // deixaria essa classe de referencia invisivel.
                fn(it.key());
                ForEachString(it.value(), fn);
            }
            return;
        }

        if (j.is_array())
        {
            for (const auto& e : j)
                ForEachString(e, fn);
        }
    }
}

namespace axe
{
    // ─────────────────────────────────────────────────────────────────────────
    bool AssetDependencyGraph::LooksLikeUUID(const std::string& s)
    {
        // 8-4-4-4-12 = 36 caracteres. O gerador do AssetDatabase acrescenta 4
        // hex ao ultimo grupo (ver GenerateUUID), entao 40 tambem e valido —
        // e por isso o teste e por FORMA, e nao por comprimento exato.
        if (s.size() < 36 || s.size() > 40)
            return false;

        int groups = 0;
        int runLen = 0;

        for (char c : s)
        {
            if (c == '-')
            {
                if (runLen == 0) return false;   // "--" ou traco inicial
                ++groups;
                runLen = 0;
                continue;
            }

            if (!std::isxdigit((unsigned char)c))
                return false;

            ++runLen;
        }

        // Quatro tracos separando cinco grupos.
        return groups == 4 && runLen > 0;
    }

    // ─────────────────────────────────────────────────────────────────────────
    std::set<std::string> AssetDependencyGraph::DirectReferences(const std::string& uuid,
        bool* outParsed)
    {
        std::set<std::string> out;

        if (outParsed)
            *outParsed = false;

        const AssetRecord* rec = AssetDatabase::Get().GetByUUID(uuid);
        if (!rec)
            return out;

        std::error_code ec;
        if (!fs::exists(rec->FilePath, ec))
            return out;

        // Assets binarios (fbx, png, wav) nao referenciam ninguem. Tentar
        // parsea-los como JSON so gastaria tempo e encheria o log.
        //
        // ── ARQUIVOS-SATELITE ────────────────────────────────────────────────
        //
        // Alguns assets guardam parte do conteudo NUM SEGUNDO ARQUIVO, ao lado,
        // com outra extensao — e esse arquivo nao esta no AssetDatabase, porque
        // a extensao dele nao e um tipo de asset.
        //
        // O caso que motivou isto: o Material Editor grava o GRAFO em
        // `X.axegraph`, irmao do `X.axemat`. Os nos de textura do grafo
        // guardam `texture_uuid`, e o `.axemat` so tem os cinco slots fixos
        // (albedo, normal, roughness, metallic, ao). Uma textura ligada apenas
        // pelo grafo ficava INVISIVEL para o grafo de dependencias — foi
        // exatamente o que aconteceu com `bricks_roughness`, listada como nao
        // usada enquanto o material a desenhava na tela.
        //
        // Varremos os dois no mesmo passe, entao qualquer referencia em
        // qualquer um deles conta.
        std::vector<fs::path> toScan;
        toScan.push_back(rec->FilePath);

        for (const char* ext : { ".axegraph" })
        {
            fs::path sat = rec->FilePath;
            sat.replace_extension(ext);

            if (fs::exists(sat, ec))
                toScan.push_back(sat);
        }

        const fs::path baseDir = rec->FilePath.parent_path();

        for (const fs::path& file : toScan)
        {
            nlohmann::json j;

            {
                std::ifstream f(file);
                if (!f)
                    continue;

                j = nlohmann::json::parse(f, nullptr, /*allow_exceptions*/ false);
            }

            if (j.is_discarded() || j.is_null())
                continue;

            if (outParsed)
                *outParsed = true;

            ForEachString(j, [&](const std::string& s)
                {
                    if (s.empty())
                        return;

                    // ── Caminho 1: referencia por UUID ───────────────────────────
                    if (LooksLikeUUID(s))
                    {
                        if (s != uuid)   // um asset referenciando a si proprio
                            out.insert(s);
                        return;
                    }

                    // ── Caminho 2: referencia por CAMINHO ────────────────────────
                    //
                    // O .axeskel aponta para o .fbx de origem por caminho relativo,
                    // e o mesmo vale para arquivos de animacao e para o HDRI do
                    // ambiente. Sem isto, um personagem entraria no build sem a
                    // malha dele.
                    //
                    // O filtro barato vem primeiro: sem ponto, nao e nome de
                    // arquivo, e a maioria esmagadora das strings de um asset (
                    // nomes de osso, de no, de parametro) morre aqui sem tocar o
                    // disco.
                    if (s.find('.') == std::string::npos)
                        return;

                    std::error_code fec;

                    fs::path candidate = baseDir / s;

                    if (!fs::exists(candidate, fec))
                        return;

                    if (const AssetRecord* r = AssetDatabase::Get().GetByPath(candidate))
                    {
                        if (r->UUID != uuid)
                            out.insert(r->UUID);
                    }
                });

        }   // for (toScan)

        return out;
    }

    // ─────────────────────────────────────────────────────────────────────────
    AssetDependencyGraph::Result AssetDependencyGraph::Collect(
        const std::vector<std::string>& roots,
        const std::vector<std::string>& forcedUUIDs)
    {
        Result res;

        std::deque<std::string> queue;

        auto push = [&](const std::string& u)
            {
                if (u.empty()) return;
                if (res.ReachableUUIDs.count(u)) return;

                if (!AssetDatabase::Get().GetByUUID(u))
                {
                    // Referenciado e ausente. Nao entra na fila (nao ha o que
                    // percorrer) e vira ERRO no relatorio — no jogo isto seria
                    // um buraco silencioso.
                    if (std::find(res.MissingUUIDs.begin(), res.MissingUUIDs.end(), u)
                        == res.MissingUUIDs.end())
                        res.MissingUUIDs.push_back(u);
                    return;
                }

                res.ReachableUUIDs.insert(u);
                queue.push_back(u);
            };

        for (const auto& r : roots)        push(r);
        for (const auto& f : forcedUUIDs)  push(f);

        // Diagnostico das raizes, ANTES da travessia. Ver RootInfo no header.
        //
        // Deduplicado: a cena inicial chega por DOIS caminhos (Project::
        // StartScene e a varredura de cenas registradas). O `push` acima ja
        // deduplica no conjunto, mas listar a mesma raiz duas vezes na UI faria
        // a contagem mentir — e reparsearia o arquivo a toa.
        std::set<std::string> seenRoots;

        for (const auto& r : roots)
        {
            if (!seenRoots.insert(r).second)
                continue;

            Result::RootInfo info;
            info.UUID = r;

            if (const AssetRecord* rec = AssetDatabase::Get().GetByUUID(r))
                info.Name = rec->Name;

            bool parsed = false;
            info.DirectRefs = DirectReferences(r, &parsed).size();
            info.Parsed = parsed;

            res.Roots.push_back(info);

            if (parsed && info.DirectRefs == 0)
            {
                AXE_CORE_WARN("AssetDependencyGraph: root '{}' parsed but references "
                    "nothing. If this is the scene open in the editor, it likely has "
                    "unsaved changes - the graph reads the file on disk.",
                    info.Name.empty() ? r : info.Name);
            }
        }

        // Travessia em largura. O `ReachableUUIDs` faz as vezes de conjunto de
        // visitados, entao um ciclo de referencias (material A -> B -> A, que e
        // possivel de montar) termina em vez de girar para sempre.
        while (!queue.empty())
        {
            const std::string cur = queue.front();
            queue.pop_front();

            for (const auto& dep : DirectReferences(cur))
                push(dep);
        }

        // ── Arquivos a copiar ────────────────────────────────────────────────
        std::error_code ec;

        for (const auto& u : res.ReachableUUIDs)
        {
            const AssetRecord* rec = AssetDatabase::Get().GetByUUID(u);
            if (!rec) continue;

            if (fs::exists(rec->FilePath, ec))
                res.Files.push_back(rec->FilePath);

            // Derivados: `Char.axeskel` leva `Char.axeskelbin`; `prop.fbx` leva
            // `prop.axemesh`. Sem isto o build sairia com os assets e sem os
            // dados cozidos — e o runtime, ja sem assimp, nao teria como ler o
            // fonte.
            for (const char* ext : kDerivedExtensions)
            {
                fs::path d = rec->FilePath;

                // O `.axemeta` ANEXA a extensao ("a.fbx.axemeta"); os cozidos
                // SUBSTITUEM ("a.axemesh"). Ver AssetDatabase::GetMetaPath.
                if (std::string(ext) == ".axemeta")
                    d = fs::path(rec->FilePath.string() + ext);
                else
                    d.replace_extension(ext);

                if (fs::exists(d, ec))
                    res.Files.push_back(d);
            }
        }

        // ── Nao alcancados ───────────────────────────────────────────────────
        for (const auto& [uuid, rec] : AssetDatabase::Get().GetAll())
        {
            (void)rec;

            if (!res.ReachableUUIDs.count(uuid))
                res.UnreachableUUIDs.push_back(uuid);
        }

        AXE_CORE_INFO("AssetDependencyGraph: {} reachable, {} unused, "
            "{} broken reference(s), {} file(s) to copy.",
            res.ReachableUUIDs.size(), res.UnreachableUUIDs.size(),
            res.MissingUUIDs.size(), res.Files.size());

        return res;
    }

} // namespace axe