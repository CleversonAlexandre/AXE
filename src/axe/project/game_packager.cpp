#include "axe/project/game_packager.hpp"

#include "axe/asset/asset_database.hpp"
#include "axe/project/project_manager.hpp"
#include "axe/log/log.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace fs = std::filesystem;

namespace axe
{
    GamePackager::Result GamePackager::Package(const Options& options)
    {
        Result res;

        auto fail = [&res](std::string msg)
            {
                AXE_CORE_ERROR("Packager: {}", msg);
                res.Errors.push_back(std::move(msg));
                return res;
            };

        if (!ProjectManager::Get().HasProject())
            return fail("no project is open.");

        const Project& project = ProjectManager::Get().GetCurrent();

        if (options.OutputDir.empty())
            return fail("no output folder given.");

        std::error_code ec;

        const fs::path root = fs::weakly_canonical(project.RootPath, ec);
        const fs::path out = fs::weakly_canonical(options.OutputDir, ec);

        // ── A guarda que importa ─────────────────────────────────────────────
        //
        // Empacotar PARA DENTRO da pasta do projeto copiaria assets para
        // dentro da arvore que esta sendo varrida, e com CleanOutput ligado
        // apagaria o projeto do usuario.
        //
        // A comparacao e por prefixo de caminho canonico, entao pega tambem o
        // caso de o destino ser uma subpasta do projeto.
        {
            const std::string rootStr = root.generic_string();
            const std::string outStr = out.generic_string();

            // O caso de IGUALDADE merece mensagem propria. Ele quase nunca
            // significa "escolhi a pasta errada": significa que o projeto
            // ABERTO no editor JA E a saida de um build anterior — voce esta
            // editando a copia empacotada sem saber. Dizer so "escolha outra
            // pasta" manda o usuario mexer no campo certo pelo motivo errado.
            if (outStr == rootStr)
                return fail("the output folder IS the root of the open project - "
                    "the editor is open on a PACKAGED COPY, not on your source "
                    "project. Open the real project (File > Open Project) and "
                    "package again.");

            if (outStr.rfind(rootStr + "/", 0) == 0)
                return fail("the output folder is inside the project folder. "
                    "Pick a folder outside it.");
        }

        if (project.StartScene.empty())
            return fail("the project has no start scene set. The game would have "
                "nothing to open.");

        // ── Raizes ───────────────────────────────────────────────────────────
        //
        // SO a cena inicial e o GameMode — nao todas as cenas registradas.
        //
        // Esta e a diferenca entre o relatorio e o pacote. O Asset Report tem a
        // opcao de incluir todas as cenas porque no editor voce quer ver o
        // projeto inteiro. O JOGO abre uma cena so, e o que estiver fora do
        // alcance dela nao deve viajar junto.
        std::vector<std::string> roots;

        if (!project.ActiveGameModeUUID.empty())
            roots.push_back(project.ActiveGameModeUUID);

        const fs::path startScene = root / project.StartScene;

        if (const AssetRecord* rec = AssetDatabase::Get().GetByPath(startScene))
        {
            roots.push_back(rec->UUID);
        }
        else
        {
            return fail("the start scene '" + project.StartScene + "' is not in the "
                "asset index. Run Rescan in the Asset Report and try again.");
        }

        res.Graph = AssetDependencyGraph::Collect(roots, options.ForcedUUIDs);

        // Referencia quebrada NAO impede o pacote, mas vira aviso visivel: o
        // jogo vai rodar com um buraco onde aquele asset estaria.
        for (const auto& missing : res.Graph.MissingUUIDs)
            res.Warnings.push_back("broken reference: " + missing);

        // ── Destino ──────────────────────────────────────────────────────────
        if (options.CleanOutput && fs::exists(out, ec))
        {
            fs::remove_all(out, ec);

            if (ec)
                return fail("could not clean the output folder: " + ec.message());
        }

        fs::create_directories(out, ec);

        if (ec)
            return fail("could not create the output folder: " + ec.message());

        // ── Assets ───────────────────────────────────────────────────────────
        //
        // Cada arquivo vai para o MESMO caminho relativo que tinha na origem.
        // Ver a nota no header sobre por que a arvore e espelhada.
        for (const fs::path& src : res.Graph.Files)
        {
            const fs::path rel = fs::relative(src, root, ec);

            if (ec || rel.empty() || rel.begin()->string() == "..")
            {
                // Asset fora da raiz do projeto. Copiar preservando o caminho
                // e impossivel, e inventar um destino quebraria as referencias
                // relativas de quem aponta para ele.
                res.Warnings.push_back("outside the project, not copied: " + src.string());
                continue;
            }

            const fs::path dst = out / rel;

            fs::create_directories(dst.parent_path(), ec);
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);

            if (ec)
            {
                res.Errors.push_back("failed to copy " + rel.generic_string()
                    + ": " + ec.message());
                ec.clear();
                continue;
            }

            ++res.FilesCopied;
            res.BytesCopied += fs::file_size(dst, ec);
            ec.clear();
        }

        // ── Arquivos de configuracao da raiz (PKG4) ──────────────────────────
        //
        // Nem tudo que o jogo precisa e asset. O `InputConfig.json` mora na
        // RAIZ do projeto e guarda os bindings — sem ele o jogo abre, os
        // scripts rodam, o pawn existe e NADA responde ao teclado.
        //
        // Esta lista existe porque o grafo de dependencias, por construcao, so
        // enxerga o que esta em `Assets/` e e alcancavel por referencia. Um
        // arquivo de configuracao nao e referenciado por ninguem: ele
        // simplesmente PRECISA estar la.
        //
        // Se aparecer outro arquivo assim, e aqui que ele se registra.
        for (const char* cfg : { "InputConfig.json" })
        {
            const fs::path src = root / cfg;

            if (!fs::exists(src, ec))
            {
                res.Warnings.push_back(std::string(cfg) + " not found in the project "
                    "root - the game will start with no input bindings.");
                continue;
            }

            fs::copy_file(src, out / cfg, fs::copy_options::overwrite_existing, ec);

            if (ec)
            {
                res.Errors.push_back(std::string("failed to copy ") + cfg + ": " + ec.message());
                ec.clear();
                continue;
            }

            ++res.FilesCopied;
            res.BytesCopied += fs::file_size(out / cfg, ec);
            ec.clear();
        }

        // ── DLLs de script (PKG3) ────────────────────────────────────────────
        //
        // Vivem em `<projeto>/Binaries/Scripts/`, FORA de `Assets/`. O grafo de
        // dependencias nunca as ve — ele parte dos assets, e o `.axescript` e o
        // asset; a DLL e o produto da compilacao dele.
        //
        // Sem elas o jogo abre, renderiza e simula fisica, mas
        // `ScriptWorld::OnSceneUpdate` reclama de "entity N sem Instance" a
        // cada frame: o pawn nao e resolvido, a camera nao segue ninguem e nada
        // responde ao teclado. Foi exatamente o sintoma do primeiro build.
        //
        // A pasta inteira e copiada, sem filtrar por script alcancavel. Filtrar
        // exigiria mapear cada `.axescript` ao nome de arquivo da DLL dele
        // (`<Nome>_<8 hex do uuid>.dll`), e um erro ai produziria um jogo em que
        // UM objeto nao responde — o tipo de falha que so aparece quando o
        // jogador chega naquela parte. Sao alguns KB por script.
        {
            const fs::path scriptsSrc = root / "Binaries" / "Scripts";

            if (fs::exists(scriptsSrc, ec))
            {
                const fs::path scriptsDst = out / "Binaries" / "Scripts";
                fs::create_directories(scriptsDst, ec);

                std::size_t dlls = 0;

                for (const auto& e : fs::directory_iterator(scriptsSrc, ec))
                {
                    if (!e.is_regular_file(ec) || e.path().extension() != ".dll")
                        continue;

                    const fs::path dst = scriptsDst / e.path().filename();

                    fs::copy_file(e.path(), dst,
                        fs::copy_options::overwrite_existing, ec);

                    if (ec)
                    {
                        res.Warnings.push_back("failed to copy script dll "
                            + e.path().filename().string() + ": " + ec.message());
                        ec.clear();
                        continue;
                    }

                    ++dlls;
                    ++res.FilesCopied;
                    res.BytesCopied += fs::file_size(dst, ec);
                    ec.clear();
                }

                if (dlls == 0)
                {
                    res.Warnings.push_back("no compiled script dll found in "
                        "Binaries/Scripts - scripts will not run in the game.");
                }
            }
            else
            {
                res.Warnings.push_back("Binaries/Scripts does not exist - compile "
                    "the scripts in the editor before packaging, or nothing will "
                    "respond to input.");
            }
        }

        // ── Manifesto ────────────────────────────────────────────────────────
        //
        // O `axe_assets.json` do pacote contem SO os assets alcancaveis, com
        // caminhos relativos. E o que substitui a varredura de diretorio: o
        // jogo nao procura nada, ele le esta lista.
        //
        // Mesmo formato do indice do editor de proposito — assim o
        // `AssetDatabase::Load` funciona nos dois sem saber a diferenca, e nao
        // ha um segundo leitor para manter em sincronia.
        {
            nlohmann::json manifest = nlohmann::json::array();

            for (const auto& uuid : res.Graph.ReachableUUIDs)
            {
                const AssetRecord* rec = AssetDatabase::Get().GetByUUID(uuid);
                if (!rec) continue;

                const fs::path rel = fs::relative(rec->FilePath, root, ec);

                if (ec || rel.empty() || rel.begin()->string() == "..")
                {
                    ec.clear();
                    continue;
                }

                nlohmann::json e;
                e["uuid"] = rec->UUID;
                e["path"] = rel.generic_string();
                e["type"] = AssetTypeToString(rec->Type);
                e["name"] = rec->Name;
                e["virtual_folder"] = rec->VirtualFolder;

                if (!rec->ScriptClassType.empty())
                    e["script_class_type"] = rec->ScriptClassType;

                manifest.push_back(e);
            }

            std::ofstream f(out / "axe_assets.json");

            if (!f)
                return fail("could not write the asset manifest.");

            f << manifest.dump(4);
        }

        // ── .axeproject ──────────────────────────────────────────────────────
        //
        // Reescrito, e nao copiado: o do editor carrega caminhos e preferencias
        // da maquina de desenvolvimento. O jogo precisa de tres campos.
        {
            nlohmann::json p;
            p["name"] = project.Name;
            p["assets_path"] = "Assets";
            p["start_scene"] = project.StartScene;
            p["active_gamemode_uuid"] = project.ActiveGameModeUUID;

            std::ofstream f(out / (project.Name + ".axeproject"));

            if (!f)
                return fail("could not write the project file.");

            f << p.dump(4);
        }

        // ── Binarios ─────────────────────────────────────────────────────────
        if (options.BinariesDir.empty())
        {
            // Campo vazio: "copiar so os assets" e uma opcao legitima, e por
            // isso os dois casos abaixo sao AVISO e nao erro. Mas os dois
            // produzem um pacote que engana, cada um a seu modo — e a diferenca
            // entre eles e o `CleanOutput`, que ja apagou a pasta la em cima.
            //
            // A checagem e feita AQUI, depois do clean, olhando o estado FINAL
            // da saida. Perguntar antes daria a resposta de um mundo que nao
            // existe mais.
            std::error_code bec;

            const bool hasExe = fs::exists(out / "game.exe", bec);
            const bool hasDll = fs::exists(out / "axe.dll", bec);

            if (hasExe || hasDll)
            {
                // Assets novos sobre binarios velhos. Roda, parece completo, e
                // se comporta como se as mudancas de codigo nao existissem —
                // exatamente o sintoma do PKG6, entrando por outra porta.
                res.Warnings.push_back("binaries folder left empty, but the output already "
                    "has game.exe/axe.dll from an earlier package - they were NOT updated. "
                    "Fill the binaries folder whenever the code changed.");
            }
            else
            {
                // Nem isso: o pacote nao tem executavel nenhum. Acontece com
                // "Clean the output folder first" ligado e o campo vazio — o
                // clean apagou os binarios do pacote anterior e nada os
                // repos. O relatorio diria "104 arquivos, 60 MB" e o jogador
                // nao teria o que abrir.
                res.Warnings.push_back("binaries folder left empty and the output has no "
                    "game.exe - this package contains assets only and cannot be run. "
                    "Fill the binaries folder to produce a playable package.");
            }
        }
        else
        {
            if (!fs::exists(options.BinariesDir, ec))
            {
                res.Warnings.push_back("binaries folder not found: "
                    + options.BinariesDir.string() + " (assets were still copied)");
            }
            else
            {
                for (const auto& e : fs::directory_iterator(options.BinariesDir, ec))
                {
                    if (!e.is_regular_file(ec))
                        continue;

                    const std::string ext = e.path().extension().string();

                    // So executavel e biblioteca. A pasta de build tambem tem
                    // .pdb, .lib, .exp e .ilk — nada disso o jogador precisa, e
                    // o .pdb sozinho costuma ser maior que o resto do pacote.
                    if (ext != ".exe" && ext != ".dll")
                        continue;

                    const fs::path dst = out / e.path().filename();

                    fs::copy_file(e.path(), dst, fs::copy_options::overwrite_existing, ec);

                    if (ec)
                    {
                        res.Warnings.push_back("failed to copy binary "
                            + e.path().filename().string() + ": " + ec.message());
                        ec.clear();
                        continue;
                    }

                    ++res.FilesCopied;
                    res.BytesCopied += fs::file_size(dst, ec);
                    ec.clear();
                }

                // ── game.exe mais velho que axe.dll ──────────────────────────
                //
                // O `game` NAO e dependencia de build do `editor` na solution.
                // Compilar ou rodar o editor reconstroi `axe.dll` e
                // `editor.exe` e deixa o `game.exe` como estava — e o pacote
                // sai com um executavel velho ao lado de uma DLL nova.
                //
                // Isso nao quebra o jogo (quase tudo mora na DLL), e por isso
                // e tao dificil de ver: o pacote roda, mas sem as mudancas que
                // estavam no `main.cpp`. Aconteceu com a captura do mouse do
                // PKG5 — a camera ficou parada por dois builds enquanto
                // material e iluminacao, que vivem na DLL, ja funcionavam.
                //
                // AVISO e nao erro: um executavel mais velho pode ser
                // deliberado (empacotar um build anterior de proposito).
                {
                    const fs::path exePath = options.BinariesDir / "game.exe";
                    const fs::path dllPath = options.BinariesDir / "axe.dll";

                    std::error_code tec;

                    if (fs::exists(exePath, tec) && fs::exists(dllPath, tec))
                    {
                        const auto exeTime = fs::last_write_time(exePath, tec);
                        const auto dllTime = fs::last_write_time(dllPath, tec);

                        if (!tec && exeTime < dllTime)
                            res.Warnings.push_back(
                                "game.exe is OLDER than axe.dll - the game project was not "
                                "rebuilt. Changes made in src/game/main.cpp are NOT in this "
                                "package. Build the 'game' project (it is not a dependency "
                                "of 'editor') and package again.");
                    }
                }

                // ── resources/ ao lado do executavel (PKG5) ──────────────────
                //
                // O HDRI default (`resources/quarry_04_puresky_2k.hdr`) vive
                // com o EDITOR, nao com o projeto — e um asset da engine, nao
                // do jogo. Uma cena que nunca teve HDRI escolhido aponta para
                // ele por caminho relativo, e no pacote o arquivo nao existia:
                // sem cubemap nao ha IBL, e o resultado nao e so "sem ceu" —
                // materiais PBR perdem a fonte de luz ambiente e a cena inteira
                // sai errada.
                //
                // Copiado da pasta de binarios porque e la que ele ja esta,
                // posto pelo postbuild do editor.
                const fs::path resSrc = options.BinariesDir / "resources";

                if (fs::exists(resSrc, ec))
                {
                    for (const auto& e : fs::recursive_directory_iterator(resSrc, ec))
                    {
                        if (!e.is_regular_file(ec))
                            continue;

                        const fs::path rel = fs::relative(e.path(), resSrc, ec);

                        if (ec) { ec.clear(); continue; }

                        const fs::path dst = out / "resources" / rel;

                        fs::create_directories(dst.parent_path(), ec);
                        fs::copy_file(e.path(), dst,
                            fs::copy_options::overwrite_existing, ec);

                        if (ec) { ec.clear(); continue; }

                        ++res.FilesCopied;
                        res.BytesCopied += fs::file_size(dst, ec);
                        ec.clear();
                    }
                }
                else
                {
                    res.Warnings.push_back("no 'resources' folder next to the binaries - "
                        "a scene using the default HDRI will render without a skybox.");
                }
            }
        }

        res.Success = res.Errors.empty();

        AXE_CORE_INFO("Packager: {} - {} file(s), {:.1f} MB, {} warning(s).",
            res.Success ? "done" : "finished with errors",
            res.FilesCopied, (double)res.BytesCopied / (1024.0 * 1024.0),
            res.Warnings.size());

        return res;
    }

} // namespace axe