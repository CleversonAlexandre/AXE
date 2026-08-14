#include "axe/animation/skeletal_cooked.hpp"
#include "axe/log/log.hpp"

#include <cstring>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
    // Strings de tamanho variavel: comprimento + bytes crus, sem terminador.
    //
    // Este e o unico ponto em que o formato deixa de ser POD — nome de osso
    // tem tamanho variavel, e sao eles que ligam um clipe de outro arquivo a
    // este esqueleto (Skeleton::FindBone casa por NOME). Errar aqui nao
    // corrompe a malha: desalinha o resto do arquivo, e a leitura falha logo
    // em seguida na checagem de tamanho.
    void WriteString(std::ofstream& f, const std::string& s)
    {
        const std::uint32_t n = (std::uint32_t)s.size();
        f.write(reinterpret_cast<const char*>(&n), sizeof(n));

        if (n)
            f.write(s.data(), (std::streamsize)n);
    }

    bool ReadString(std::ifstream& f, std::string& out, std::uint32_t maxLen = 4096)
    {
        std::uint32_t n = 0;
        f.read(reinterpret_cast<char*>(&n), sizeof(n));

        if (!f.good())
            return false;

        // Teto explicito: sem ele, um arquivo corrompido com n = 3 bilhoes
        // pediria 3 GB ao resize antes de qualquer chance de reportar erro.
        if (n > maxLen)
            return false;

        out.resize(n);

        if (n)
            f.read(out.data(), (std::streamsize)n);

        return f.good();
    }
}

namespace axe
{
    // ─────────────────────────────────────────────────────────────────────────
    fs::path SkeletalCooked::PathFor(const fs::path& axeskel)
    {
        fs::path p = axeskel;
        p.replace_extension(".axeskelbin");
        return p;
    }

    // ─────────────────────────────────────────────────────────────────────────
    bool SkeletalCooked::IsUpToDate(const fs::path& axeskel, const fs::path& source)
    {
        std::error_code ec;

        const fs::path cooked = PathFor(axeskel);

        if (!fs::exists(cooked, ec))
            return false;

        // Fonte ausente (projeto distribuido so com cozidos): o cozido serve.
        if (source.empty() || !fs::exists(source, ec))
            return true;

        const auto tSrc = fs::last_write_time(source, ec);
        if (ec) return false;

        const auto tCooked = fs::last_write_time(cooked, ec);
        if (ec) return false;

        return tCooked >= tSrc;
    }

    // ─────────────────────────────────────────────────────────────────────────
    bool SkeletalCooked::Write(const fs::path& outPath,
        const SkinnedMesh& mesh,
        const Skeleton& skeleton)
    {
        const auto& verts = mesh.GetVertices();
        const auto& idx = mesh.GetIndices();
        const auto& bones = skeleton.GetBones();

        if (verts.empty() || idx.empty() || bones.empty())
        {
            AXE_CORE_WARN("SkeletalCooked::Write: '{}' has no vertices, indices or bones - nothing written.",
                outPath.string());
            return false;
        }

        std::error_code ec;
        fs::create_directories(outPath.parent_path(), ec);

        std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            AXE_CORE_ERROR("SkeletalCooked::Write: could not open '{}'.", outPath.string());
            return false;
        }

        AxeSkelHeader h{};
        h.Magic[0] = 'A'; h.Magic[1] = 'X'; h.Magic[2] = 'S'; h.Magic[3] = 'K';
        h.Version = kAxeSkelVersion;
        h.VertexCount = (std::uint32_t)verts.size();
        h.IndexCount = (std::uint32_t)idx.size();
        h.VertexStride = (std::uint32_t)sizeof(SkinnedVertex);
        h.BoneCount = (std::uint32_t)bones.size();

        f.write(reinterpret_cast<const char*>(&h), sizeof(h));

        // ── Esqueleto ────────────────────────────────────────────────────────
        //
        // ANTES da malha, de proposito: o construtor de SkinnedMesh recebe o
        // esqueleto pronto, entao a leitura precisa monta-lo primeiro. Gravar
        // na ordem em que se le evita um seek.
        WriteString(f, skeleton.GetName());

        const glm::mat4 gi = skeleton.GetGlobalInverseTransform();
        f.write(reinterpret_cast<const char*>(&gi), sizeof(gi));

        const float unitScale = skeleton.GetUnitScale();
        f.write(reinterpret_cast<const char*>(&unitScale), sizeof(unitScale));

        for (const Bone& b : bones)
        {
            WriteString(f, b.Name);

            const std::int32_t parent = (std::int32_t)b.ParentIndex;
            f.write(reinterpret_cast<const char*>(&parent), sizeof(parent));
            f.write(reinterpret_cast<const char*>(&b.InverseBindPose), sizeof(glm::mat4));
            f.write(reinterpret_cast<const char*>(&b.LocalBindPose), sizeof(glm::mat4));
        }

        // ── Malha ────────────────────────────────────────────────────────────
        f.write(reinterpret_cast<const char*>(verts.data()),
            (std::streamsize)(verts.size() * sizeof(SkinnedVertex)));
        f.write(reinterpret_cast<const char*>(idx.data()),
            (std::streamsize)(idx.size() * sizeof(std::uint32_t)));

        if (!f.good())
        {
            AXE_CORE_ERROR("SkeletalCooked::Write: failed while writing '{}'.", outPath.string());
            f.close();

            // Truncado e pior que ausente: passaria pelo IsUpToDate e falharia
            // no Read a cada abertura. Apagar devolve ao fallback de FBX.
            fs::remove(outPath, ec);
            return false;
        }

        AXE_CORE_INFO("SkeletalCooked: '{}' ({} vertices, {} indices, {} bones).",
            outPath.filename().string(), h.VertexCount, h.IndexCount, h.BoneCount);

        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    SkeletalCooked::Result SkeletalCooked::Read(const fs::path& path)
    {
        Result out;

        std::error_code ec;

        if (!fs::exists(path, ec))
            return out;

        std::ifstream f(path, std::ios::binary);
        if (!f)
            return out;

        AxeSkelHeader h{};
        f.read(reinterpret_cast<char*>(&h), sizeof(h));

        if (!f.good())
            return out;

        if (std::memcmp(h.Magic, "AXSK", 4) != 0)
        {
            AXE_CORE_WARN("SkeletalCooked: '{}' is not an .axeskelbin.", path.filename().string());
            return out;
        }

        if (h.Version != kAxeSkelVersion)
        {
            AXE_CORE_WARN("SkeletalCooked: '{}' is version {} (expected {}) - will be reimported.",
                path.filename().string(), h.Version, kAxeSkelVersion);
            return out;
        }

        if (h.VertexStride != (std::uint32_t)sizeof(SkinnedVertex))
        {
            AXE_CORE_WARN("SkeletalCooked: '{}' was written with a {}-byte SkinnedVertex; "
                "this build uses {} - will be reimported.",
                path.filename().string(), h.VertexStride, (std::uint32_t)sizeof(SkinnedVertex));
            return out;
        }

        if (h.VertexCount == 0 || h.IndexCount == 0 || h.BoneCount == 0)
            return out;

        if (h.BoneCount > (std::uint32_t)AXE_MAX_BONES)
        {
            AXE_CORE_WARN("SkeletalCooked: '{}' has {} bones, above the {} limit - will be reimported.",
                path.filename().string(), h.BoneCount, AXE_MAX_BONES);
            return out;
        }

        // ── Esqueleto ────────────────────────────────────────────────────────
        auto skeleton = std::make_shared<Skeleton>();

        std::string skelName;
        if (!ReadString(f, skelName))
            return out;

        skeleton->SetName(skelName);

        glm::mat4 globalInv{ 1.0f };
        f.read(reinterpret_cast<char*>(&globalInv), sizeof(globalInv));
        skeleton->SetGlobalInverseTransform(globalInv);

        float unitScale = 1.0f;
        f.read(reinterpret_cast<char*>(&unitScale), sizeof(unitScale));
        skeleton->SetUnitScale(unitScale);

        if (!f.good())
            return out;

        for (std::uint32_t i = 0; i < h.BoneCount; ++i)
        {
            std::string name;
            if (!ReadString(f, name))
                return out;

            std::int32_t parent = -1;
            glm::mat4 inv{ 1.0f };
            glm::mat4 local{ 1.0f };

            f.read(reinterpret_cast<char*>(&parent), sizeof(parent));
            f.read(reinterpret_cast<char*>(&inv), sizeof(inv));
            f.read(reinterpret_cast<char*>(&local), sizeof(local));

            if (!f.good())
                return out;

            // INVARIANTE de ordem topologica: o pai vem ANTES do filho. Todo o
            // engine depende disso — a pose global e calculada num unico laco
            // para frente, sem recursao (ver skeleton.hpp). Um arquivo que
            // violasse isso produziria bones herdando de um pai ainda nao
            // resolvido, e o personagem se retorceria sem erro nenhum.
            if (parent >= (std::int32_t)i)
            {
                AXE_CORE_WARN("SkeletalCooked: '{}' has bone {} with parent {} (not in topological "
                    "order) - will be reimported.", path.filename().string(), i, parent);
                return out;
            }

            // AddBone reconstroi o mapa nome->indice, entao nada mais precisa
            // ser gravado para o FindBone funcionar.
            skeleton->AddBone(name, (int)parent, inv, local);
        }

        // ── Malha ────────────────────────────────────────────────────────────
        std::vector<SkinnedVertex> verts(h.VertexCount);
        std::vector<std::uint32_t> idx(h.IndexCount);

        f.read(reinterpret_cast<char*>(verts.data()),
            (std::streamsize)(verts.size() * sizeof(SkinnedVertex)));
        f.read(reinterpret_cast<char*>(idx.data()),
            (std::streamsize)(idx.size() * sizeof(std::uint32_t)));

        if (!f.good())
            return out;

        out.Skeleton = skeleton;
        out.Mesh = std::make_shared<SkinnedMesh>(verts, idx, skeleton);

        return out;
    }

} // namespace axe