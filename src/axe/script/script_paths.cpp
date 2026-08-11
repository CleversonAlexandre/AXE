#include "script_paths.hpp"
#include "axe/project/project_manager.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/log/log.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <vector>

namespace axe
{
    namespace
    {
        // Oito caracteres do UUID bastam para distinguir assets dentro de UM
        // projeto, e um nome de arquivo com o UUID inteiro fica ilegível na
        // pasta. Se um dia der colisão, ela aparece como duas entradas iguais
        // no sweep — barulhenta, não silenciosa.
        std::string ShortUuid(const std::string& uuid)
        {
            return uuid.size() <= 8 ? uuid : uuid.substr(0, 8);
        }

        // O nome vem do usuário e vira nome de arquivo: qualquer coisa que o
        // Windows recuse (barra, dois-pontos, aspas) viraria uma falha de
        // gravação com mensagem incompreensível.
        std::string SanitizeForFilename(const std::string& s)
        {
            std::string out;
            out.reserve(s.size());
            for (char c : s)
            {
                const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
                out += ok ? c : '_';
            }
            return out.empty() ? std::string("Script") : out;
        }

        std::filesystem::path EnsureDir(const std::filesystem::path& p)
        {
            if (p.empty()) return p;
            std::error_code ec;
            std::filesystem::create_directories(p, ec);
            return p;
        }

        std::filesystem::path ProjectRoot()
        {
            auto& pm = ProjectManager::Get();
            if (!pm.HasProject()) return {};
            return pm.GetCurrent().RootPath;
        }

        // Raiz definida a mao por quem hospeda o jogo. Vazia = nao definida.
        std::filesystem::path g_RuntimeRoot;

        std::filesystem::path ExeDir()
        {
            char buf[MAX_PATH] = {};
            if (GetModuleFileNameA(nullptr, buf, MAX_PATH) == 0) return {};
            return std::filesystem::path(buf).parent_path();
        }

        // Extensões que o MSVC deixa para trás ao gerar uma DLL a partir de um
        // .cpp solto. O .obj cai no diretório de trabalho do cl.exe, por isso
        // ele também é procurado nas duas pastas.
        const char* kArtifactExts[] = { ".cpp", ".dll", ".lib", ".exp", ".pdb", ".obj", ".ilk" };
    }

    void ScriptPaths::SetRuntimeRoot(const std::filesystem::path& root)
    {
        g_RuntimeRoot = root;
        AXE_CORE_INFO("ScriptPaths: raiz de runtime definida como '{}'.", root.string());
    }

    std::filesystem::path ScriptPaths::Root()
    {
        // Explicita > projeto > executavel. Ver o comentario no header.
        if (!g_RuntimeRoot.empty()) return g_RuntimeRoot;
        if (auto p = ProjectRoot(); !p.empty()) return p;
        return ExeDir();
    }

    std::filesystem::path ScriptPaths::IntermediateDir(bool create)
    {
        auto root = Root();
        if (root.empty()) return {};
        auto dir = root / "Intermediate" / "Scripts";
        return create ? EnsureDir(dir) : dir;
    }

    std::filesystem::path ScriptPaths::BinariesDir(bool create)
    {
        auto root = Root();
        if (root.empty()) return {};
        auto dir = root / "Binaries" / "Scripts";
        return create ? EnsureDir(dir) : dir;
    }

    std::filesystem::path ScriptPaths::LegacyDir()
    {
        char exeBuf[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exeBuf, MAX_PATH);
        return std::filesystem::path(exeBuf).parent_path() / "temp_scripts";
    }

    std::string ScriptPaths::ArtifactStem(const std::string& scriptName,
        const std::string& uuid)
    {
        const std::string base = SanitizeForFilename(scriptName);
        if (uuid.empty()) return base;
        return base + "_" + ShortUuid(uuid);
    }

    std::filesystem::path ScriptPaths::CppPathFor(const std::string& scriptName,
        const std::string& uuid)
    {
        // create=true: este e um caminho de ESCRITA (o compilador vai gravar
        // o .cpp aqui), e so nos caminhos de escrita a pasta deve nascer.
        auto dir = IntermediateDir(true);
        if (dir.empty()) return {};
        return dir / (ArtifactStem(scriptName, uuid) + ".cpp");
    }

    std::filesystem::path ScriptPaths::DllPathFor(const std::string& scriptName,
        const std::string& uuid)
    {
        auto dir = BinariesDir(true);   // caminho de escrita — ver CppPathFor
        if (dir.empty()) return {};
        return dir / (ArtifactStem(scriptName, uuid) + ".dll");
    }

    std::filesystem::path ScriptPaths::ResolveDll(const std::string& scriptName,
        const std::string& uuid)
    {
        std::error_code ec;

        // 1 — formato atual
        auto current = DllPathFor(scriptName, uuid);
        if (!current.empty() && std::filesystem::exists(current, ec))
            return current;

        // 2 — já no projeto, mas ainda sem sufixo de UUID
        auto binDir = BinariesDir();   // leitura: nao cria pasta
        if (!binDir.empty())
        {
            auto noUuid = binDir / (SanitizeForFilename(scriptName) + ".dll");
            if (std::filesystem::exists(noUuid, ec))
                return noUuid;
        }

        // 3 — UUID desconhecido: procura por padrao "<Nome>_????????.dll"
        //
        // E o caso do RUNTIME. La o AssetDatabase pode nao estar carregado, e
        // sem ele nao ha UUID para montar o nome exato — o passo 1 falha nao
        // porque o arquivo nao existe, mas porque nao sabemos como ele se
        // chama.
        //
        // So aceita quando o resultado e UNICO. Dois candidatos significam
        // dois scripts de mesmo nome no projeto, e escolher um deles seria
        // carregar o comportamento de outro objeto: o jogo roda, nada crasha,
        // e o sintoma nao aponta para lugar nenhum. Melhor nao carregar e
        // dizer por que.
        if (!binDir.empty() && std::filesystem::exists(binDir, ec))
        {
            const std::string prefix = SanitizeForFilename(scriptName) + "_";
            std::filesystem::path found;
            int matches = 0;

            for (const auto& entry : std::filesystem::directory_iterator(binDir, ec))
            {
                if (!entry.is_regular_file(ec)) continue;
                if (entry.path().extension() != ".dll") continue;

                const std::string stem = entry.path().stem().string();
                if (stem.rfind(prefix, 0) != 0) continue;          // nao comeca com "<Nome>_"
                if (stem.size() != prefix.size() + 8) continue;    // sufixo tem 8 chars

                found = entry.path();
                matches++;
            }

            if (matches == 1)
                return found;

            if (matches > 1)
            {
                AXE_CORE_ERROR("ScriptPaths: {} DLLs candidatas para o script '{}' e nenhum "
                    "UUID para desempatar. Nenhuma sera carregada — renomeie um dos "
                    "scripts para que os nomes sejam unicos.", matches, scriptName);
                return {};
            }
        }

        // 4 — legado, na pasta bin do editor
        auto legacy = LegacyDir() / (scriptName + ".dll");
        if (std::filesystem::exists(legacy, ec))
        {
            AXE_CORE_WARN("ScriptPaths: '{}' ainda esta em temp_scripts (formato antigo). "
                "Recompile o script para mover os artefatos para a pasta do projeto.",
                scriptName);
            return legacy;
        }

        return {};
    }

    int ScriptPaths::RemoveArtifactsFor(const std::string& scriptName,
        const std::string& uuid)
    {
        const std::string stem = ArtifactStem(scriptName, uuid);
        const std::filesystem::path dirs[] = { IntermediateDir(), BinariesDir() };

        int removed = 0;
        std::error_code ec;

        for (const auto& dir : dirs)
        {
            if (dir.empty()) continue;
            for (const char* ext : kArtifactExts)
            {
                auto f = dir / (stem + ext);
                if (std::filesystem::exists(f, ec) && std::filesystem::remove(f, ec))
                    removed++;
            }
        }

        if (removed > 0)
            AXE_CORE_INFO("ScriptPaths: {} artefato(s) de '{}' removido(s).", removed, scriptName);

        return removed;
    }

    int ScriptPaths::SweepOrphans()
    {
        // ── SC29: duas travas antes de apagar qualquer coisa ─────────────────
        //
        // Esta funcao apaga arquivos comparando contra o AssetDatabase, e a
        // queda para a pasta do executavel (introduzida agora) tornou isso
        // perigoso: sem projeto aberto, a raiz vira a pasta do .exe E o
        // AssetDatabase esta vazio — ou seja, TODA DLL de script pareceria
        // orfa e seria apagada. Num jogo empacotado, isso seria o executavel
        // deletando o proprio conteudo no boot.
        //
        // Trava 1: so varre com projeto aberto. Varredura e faxina de editor,
        // e nao tem o que fazer em runtime.
        if (!ProjectManager::Get().HasProject())
            return 0;

        // Trava 2: base vazia significa "nao sei o que existe", e nao "nada
        // existe". Apagar com base nessa duvida e o tipo de decisao que so se
        // percebe depois que o trabalho ja foi embora.
        if (AssetDatabase::Get().GetAll().empty())
        {
            AXE_CORE_WARN("ScriptPaths: AssetDatabase vazio — varredura de orfaos "
                "ignorada para nao apagar artefatos por engano.");
            return 0;
        }

        const std::filesystem::path dirs[] = { IntermediateDir(), BinariesDir() };
        if (dirs[0].empty() && dirs[1].empty()) return 0;

        // Conjunto de UUIDs curtos vivos. Comparar contra o AssetDatabase é o
        // ponto todo do sufixo: sem ele, "de quem é este arquivo?" não tem
        // resposta e nada pode ser apagado com segurança.
        std::vector<std::string> alive;
        for (const auto& [uuid, rec] : AssetDatabase::Get().GetAll())
        {
            (void)rec;
            alive.push_back(ShortUuid(uuid));
        }

        int removed = 0;
        std::error_code ec;

        for (const auto& dir : dirs)
        {
            if (dir.empty() || !std::filesystem::exists(dir, ec)) continue;

            for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
            {
                if (!entry.is_regular_file(ec)) continue;

                const std::string stem = entry.path().stem().string();

                // Sufixo "_xxxxxxxx" no fim? Sem ele, o arquivo é de antes
                // desta mudança e não há como saber de quem é — fica.
                const auto us = stem.rfind('_');
                if (us == std::string::npos || stem.size() - us - 1 != 8) continue;

                const std::string tag = stem.substr(us + 1);
                if (std::find(alive.begin(), alive.end(), tag) != alive.end()) continue;

                if (std::filesystem::remove(entry.path(), ec))
                {
                    removed++;
                    AXE_CORE_INFO("ScriptPaths: orfao removido — '{}'",
                        entry.path().filename().string());
                }
            }
        }

        if (removed > 0)
            AXE_CORE_INFO("ScriptPaths: {} arquivo(s) orfao(s) limpo(s) na abertura do projeto.", removed);

        return removed;
    }

} // namespace axe