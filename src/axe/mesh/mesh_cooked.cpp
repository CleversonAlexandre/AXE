#include "axe/mesh/mesh_cooked.hpp"
#include "axe/log/log.hpp"

#include <cstring>
#include <fstream>

namespace fs = std::filesystem;

namespace axe
{
    // ─────────────────────────────────────────────────────────────────────────
    fs::path MeshCooked::PathFor(const fs::path& source)
    {
        fs::path p = source;
        p.replace_extension(".axemesh");
        return p;
    }

    // ─────────────────────────────────────────────────────────────────────────
    bool MeshCooked::IsUpToDate(const fs::path& source)
    {
        std::error_code ec;

        const fs::path cooked = PathFor(source);

        if (!fs::exists(cooked, ec))
            return false;

        // Fonte sumiu (projeto so com cozidos, ou FBX movido): o cozido serve.
        // Recusa-lo aqui deixaria o asset inacessivel sem ganho nenhum.
        if (!fs::exists(source, ec))
            return true;

        const auto tSrc = fs::last_write_time(source, ec);
        if (ec) return false;

        const auto tCooked = fs::last_write_time(cooked, ec);
        if (ec) return false;

        return tCooked >= tSrc;
    }

    // ─────────────────────────────────────────────────────────────────────────
    bool MeshCooked::Write(const fs::path& outPath, const Mesh& mesh)
    {
        const auto& verts = mesh.GetVertices();
        const auto& idx = mesh.GetIndices();

        // Mesh que veio do Skin Cache tem os dados SO na GPU — GetVertices()
        // devolve vazio de proposito (ver mesh.hpp). Gravar isso produziria um
        // .axemesh valido e vazio, e o modelo sumiria da tela sem erro nenhum.
        if (verts.empty() || idx.empty())
        {
            AXE_CORE_WARN("MeshCooked::Write: '{}' has no CPU-side data - nothing written.",
                outPath.string());
            return false;
        }

        std::error_code ec;
        fs::create_directories(outPath.parent_path(), ec);

        std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            AXE_CORE_ERROR("MeshCooked::Write: could not open '{}'.", outPath.string());
            return false;
        }

        AxeMeshHeader h{};
        h.Magic[0] = 'A'; h.Magic[1] = 'X'; h.Magic[2] = 'E'; h.Magic[3] = 'M';
        h.Version = kAxeMeshVersion;
        h.VertexCount = (std::uint32_t)verts.size();
        h.IndexCount = (std::uint32_t)idx.size();
        h.VertexStride = (std::uint32_t)sizeof(Vertex);

        f.write(reinterpret_cast<const char*>(&h), sizeof(h));
        f.write(reinterpret_cast<const char*>(verts.data()),
            (std::streamsize)(verts.size() * sizeof(Vertex)));
        f.write(reinterpret_cast<const char*>(idx.data()),
            (std::streamsize)(idx.size() * sizeof(std::uint32_t)));

        if (!f.good())
        {
            AXE_CORE_ERROR("MeshCooked::Write: failed while writing '{}'.", outPath.string());
            f.close();

            // Um arquivo truncado e PIOR que arquivo nenhum: ele passaria pelo
            // teste de existencia do IsUpToDate e so falharia no Read, a cada
            // load. Apagar devolve o asset ao fallback de FBX.
            fs::remove(outPath, ec);
            return false;
        }

        AXE_CORE_INFO("MeshCooked: '{}' ({} vertices, {} indices).",
            outPath.filename().string(), h.VertexCount, h.IndexCount);

        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    std::shared_ptr<Mesh> MeshCooked::Read(const fs::path& path)
    {
        std::error_code ec;

        if (!fs::exists(path, ec))
            return nullptr;

        std::ifstream f(path, std::ios::binary);
        if (!f)
            return nullptr;

        AxeMeshHeader h{};
        f.read(reinterpret_cast<char*>(&h), sizeof(h));

        if (!f.good())
            return nullptr;

        if (std::memcmp(h.Magic, "AXEM", 4) != 0)
        {
            AXE_CORE_WARN("MeshCooked: '{}' is not an .axemesh.", path.filename().string());
            return nullptr;
        }

        if (h.Version != kAxeMeshVersion)
        {
            AXE_CORE_WARN("MeshCooked: '{}' is version {} (expected {}) - will be reimported.",
                path.filename().string(), h.Version, kAxeMeshVersion);
            return nullptr;
        }

        // Ver a nota no header: sem esta checagem, um `Vertex` que mudou de
        // tamanho produziria geometria embaralhada em vez de erro.
        if (h.VertexStride != (std::uint32_t)sizeof(Vertex))
        {
            AXE_CORE_WARN("MeshCooked: '{}' was written with a {}-byte Vertex; "
                "this build uses {} - will be reimported.",
                path.filename().string(), h.VertexStride, (std::uint32_t)sizeof(Vertex));
            return nullptr;
        }

        if (h.VertexCount == 0 || h.IndexCount == 0)
            return nullptr;

        // Confere o TAMANHO antes de alocar.
        //
        // Sem isto, um arquivo corrompido com VertexCount absurdo pediria
        // gigabytes ao resize e derrubaria o programa antes de qualquer
        // mensagem. Comparar com o tamanho real do arquivo custa uma syscall.
        const std::uintmax_t expected = sizeof(AxeMeshHeader)
            + (std::uintmax_t)h.VertexCount * sizeof(Vertex)
            + (std::uintmax_t)h.IndexCount * sizeof(std::uint32_t);

        const std::uintmax_t actual = fs::file_size(path, ec);

        if (ec || actual != expected)
        {
            AXE_CORE_WARN("MeshCooked: '{}' has {} bytes, expected {} - truncated or corrupt.",
                path.filename().string(), (std::uint64_t)actual, (std::uint64_t)expected);
            return nullptr;
        }

        std::vector<Vertex> verts(h.VertexCount);
        std::vector<std::uint32_t> idx(h.IndexCount);

        f.read(reinterpret_cast<char*>(verts.data()),
            (std::streamsize)(verts.size() * sizeof(Vertex)));
        f.read(reinterpret_cast<char*>(idx.data()),
            (std::streamsize)(idx.size() * sizeof(std::uint32_t)));

        if (!f.good())
            return nullptr;

        return std::make_shared<Mesh>(verts, idx);
    }

    // ─────────────────────────────────────────────────────────────────────────
    std::shared_ptr<Mesh> MeshCooked::TryLoadFor(const fs::path& source)
    {
        if (!IsUpToDate(source))
            return nullptr;

        return Read(PathFor(source));
    }

} // namespace axe