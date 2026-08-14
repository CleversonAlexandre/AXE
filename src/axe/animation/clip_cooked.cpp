#include "axe/animation/clip_cooked.hpp"
#include "axe/log/log.hpp"

#include <cstring>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
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

        if (!f.good() || n > maxLen)
            return false;

        out.resize(n);

        if (n)
            f.read(out.data(), (std::streamsize)n);

        return f.good();
    }

    // Teto de chaves por canal.
    //
    // Nao e limite de arte: e a barreira contra um arquivo corrompido pedir
    // gigabytes ao resize. Um clipe de 60 s a 60 fps tem 3600 chaves por canal;
    // um milhao e tres ordens de grandeza de folga.
    constexpr std::uint32_t kMaxKeys = 1000000;

    template <typename K>
    bool ReadKeys(std::ifstream& f, std::vector<K>& out)
    {
        std::uint32_t n = 0;
        f.read(reinterpret_cast<char*>(&n), sizeof(n));

        if (!f.good() || n > kMaxKeys)
            return false;

        out.resize(n);

        if (n)
            f.read(reinterpret_cast<char*>(out.data()), (std::streamsize)(n * sizeof(K)));

        return f.good();
    }

    template <typename K>
    void WriteKeys(std::ofstream& f, const std::vector<K>& keys)
    {
        const std::uint32_t n = (std::uint32_t)keys.size();
        f.write(reinterpret_cast<const char*>(&n), sizeof(n));

        if (n)
            f.write(reinterpret_cast<const char*>(keys.data()),
                (std::streamsize)(n * sizeof(K)));
    }
}

namespace axe
{
    // ─────────────────────────────────────────────────────────────────────────
    fs::path ClipCooked::PathFor(const fs::path& source)
    {
        fs::path p = source;
        p.replace_extension(".axeclipbin");
        return p;
    }

    // ─────────────────────────────────────────────────────────────────────────
    bool ClipCooked::IsUpToDate(const fs::path& source)
    {
        std::error_code ec;

        const fs::path cooked = PathFor(source);

        if (!fs::exists(cooked, ec))
            return false;

        if (source.empty() || !fs::exists(source, ec))
            return true;

        const auto tSrc = fs::last_write_time(source, ec);
        if (ec) return false;

        const auto tCooked = fs::last_write_time(cooked, ec);
        if (ec) return false;

        return tCooked >= tSrc;
    }

    // ─────────────────────────────────────────────────────────────────────────
    bool ClipCooked::Write(const fs::path& outPath,
        const std::vector<std::shared_ptr<AnimationClip>>& clips,
        const Skeleton& skeleton)
    {
        if (clips.empty())
            return false;

        const auto& bones = skeleton.GetBones();

        std::error_code ec;
        fs::create_directories(outPath.parent_path(), ec);

        std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            AXE_CORE_ERROR("ClipCooked::Write: could not open '{}'.", outPath.string());
            return false;
        }

        AxeClipHeader h{};
        h.Magic[0] = 'A'; h.Magic[1] = 'X'; h.Magic[2] = 'C'; h.Magic[3] = 'L';
        h.Version = kAxeClipVersion;
        h.ClipCount = (std::uint32_t)clips.size();

        f.write(reinterpret_cast<const char*>(&h), sizeof(h));

        std::uint32_t droppedChannels = 0;

        for (const auto& clip : clips)
        {
            if (!clip)
                continue;

            WriteString(f, clip->GetName());

            const float dur = clip->GetDuration();
            f.write(reinterpret_cast<const char*>(&dur), sizeof(dur));

            const std::uint8_t looping = clip->IsLooping() ? 1 : 0;
            f.write(reinterpret_cast<const char*>(&looping), sizeof(looping));

            // Conta ANTES de gravar: um canal cujo BoneIndex nao existe mais no
            // esqueleto nao tem nome para gravar, e a contagem precisa bater
            // com o que realmente vai para o arquivo.
            const auto& channels = clip->GetChannels();

            std::uint32_t valid = 0;

            for (const BoneChannel& c : channels)
                if (c.BoneIndex >= 0 && c.BoneIndex < (int)bones.size())
                    ++valid;

            f.write(reinterpret_cast<const char*>(&valid), sizeof(valid));

            for (const BoneChannel& c : channels)
            {
                if (c.BoneIndex < 0 || c.BoneIndex >= (int)bones.size())
                {
                    ++droppedChannels;
                    continue;
                }

                // O NOME, e nao o indice. Ver a nota no header.
                WriteString(f, bones[(std::size_t)c.BoneIndex].Name);

                WriteKeys(f, c.PositionKeys);
                WriteKeys(f, c.RotationKeys);
                WriteKeys(f, c.ScaleKeys);
            }
        }

        if (!f.good())
        {
            AXE_CORE_ERROR("ClipCooked::Write: failed while writing '{}'.", outPath.string());
            f.close();
            fs::remove(outPath, ec);
            return false;
        }

        if (droppedChannels)
        {
            AXE_CORE_WARN("ClipCooked: '{}' dropped {} channel(s) with no valid bone.",
                outPath.filename().string(), droppedChannels);
        }

        AXE_CORE_INFO("ClipCooked: '{}' ({} clip(s)).",
            outPath.filename().string(), h.ClipCount);

        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    std::vector<std::shared_ptr<AnimationClip>> ClipCooked::Read(
        const fs::path& path, const Skeleton& targetSkeleton)
    {
        std::vector<std::shared_ptr<AnimationClip>> out;

        std::error_code ec;

        if (!fs::exists(path, ec))
            return out;

        std::ifstream f(path, std::ios::binary);
        if (!f)
            return out;

        AxeClipHeader h{};
        f.read(reinterpret_cast<char*>(&h), sizeof(h));

        if (!f.good())
            return out;

        if (std::memcmp(h.Magic, "AXCL", 4) != 0)
        {
            AXE_CORE_WARN("ClipCooked: '{}' is not an .axeclipbin.", path.filename().string());
            return out;
        }

        if (h.Version != kAxeClipVersion)
        {
            AXE_CORE_WARN("ClipCooked: '{}' is version {} (expected {}) - will be reimported.",
                path.filename().string(), h.Version, kAxeClipVersion);
            return out;
        }

        if (h.ClipCount == 0 || h.ClipCount > 4096)
            return out;

        std::uint32_t unmatched = 0;

        for (std::uint32_t i = 0; i < h.ClipCount; ++i)
        {
            auto clip = std::make_shared<AnimationClip>();

            std::string name;
            if (!ReadString(f, name))
                return {};

            clip->SetName(name);

            float dur = 0.0f;
            f.read(reinterpret_cast<char*>(&dur), sizeof(dur));
            clip->SetDuration(dur);

            std::uint8_t looping = 1;
            f.read(reinterpret_cast<char*>(&looping), sizeof(looping));
            clip->SetLooping(looping != 0);

            std::uint32_t channelCount = 0;
            f.read(reinterpret_cast<char*>(&channelCount), sizeof(channelCount));

            if (!f.good() || channelCount > (std::uint32_t)AXE_MAX_BONES * 4)
                return {};

            for (std::uint32_t c = 0; c < channelCount; ++c)
            {
                std::string boneName;
                if (!ReadString(f, boneName))
                    return {};

                BoneChannel ch;

                // RELIGAMENTO: nome -> indice no esqueleto ALVO.
                //
                // -1 quando o osso nao existe aqui. As chaves ainda precisam
                // ser LIDAS para o arquivo nao desalinhar — so nao entram no
                // clipe. AddChannel descarta BoneIndex negativo sozinho.
                ch.BoneIndex = targetSkeleton.FindBone(boneName);

                if (!ReadKeys(f, ch.PositionKeys)) return {};
                if (!ReadKeys(f, ch.RotationKeys)) return {};
                if (!ReadKeys(f, ch.ScaleKeys))    return {};

                if (ch.BoneIndex < 0)
                {
                    ++unmatched;
                    continue;
                }

                clip->AddChannel(ch);
            }

            out.push_back(clip);
        }

        if (unmatched)
        {
            AXE_CORE_WARN("ClipCooked: '{}' - {} channel(s) with no matching bone in the "
                "target skeleton (ignored).", path.filename().string(), unmatched);
        }

        return out;
    }

    // ─────────────────────────────────────────────────────────────────────────
    std::vector<std::shared_ptr<AnimationClip>> ClipCooked::TryLoadFor(
        const fs::path& source, const Skeleton& targetSkeleton)
    {
        if (!IsUpToDate(source))
            return {};

        return Read(PathFor(source), targetSkeleton);
    }

} // namespace axe