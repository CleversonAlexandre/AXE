// src/axe/animation/sequencer/sequencer_asset.cpp
//
// Implementacao do SequencerAsset com JSON (nlohmann).
// UUID e std::string no AXE (ver components.hpp — AssetUUID etc.).

#include "axe/animation/sequencer/sequencer_asset.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace axe {

    // ============================================================
    // Helpers de conversao enum <-> string (estaveis para o JSON)
    // ============================================================

    const char* SequencerTrackTypeToString(SequencerTrackType t) {
        switch (t) {
        case SequencerTrackType::TransformBone:    return "TransformBone";
        case SequencerTrackType::TransformControl: return "TransformControl";
        case SequencerTrackType::TransformSocket:  return "TransformSocket";
        case SequencerTrackType::AnimationClip:    return "AnimationClip";
        case SequencerTrackType::Property:         return "Property";
        case SequencerTrackType::Event:            return "Event";
        }
        return "Unknown";
    }

    SequencerTrackType SequencerTrackTypeFromString(const std::string& s) {
        if (s == "TransformBone")     return SequencerTrackType::TransformBone;
        if (s == "TransformControl")  return SequencerTrackType::TransformControl;
        if (s == "TransformSocket")  return SequencerTrackType::TransformSocket;
        if (s == "AnimationClip")    return SequencerTrackType::AnimationClip;
        if (s == "Property")         return SequencerTrackType::Property;
        if (s == "Event")            return SequencerTrackType::Event;
        return SequencerTrackType::TransformBone;  // default seguro
    }

    const char* SequencerTargetTypeToString(SequencerTargetType t) {
        switch (t) {
        case SequencerTargetType::Bone:    return "Bone";
        case SequencerTargetType::Control: return "Control";
        case SequencerTargetType::Null:    return "Null";
        case SequencerTargetType::Socket:  return "Socket";
        }
        return "Bone";
    }

    SequencerTargetType SequencerTargetTypeFromString(const std::string& s) {
        if (s == "Bone")    return SequencerTargetType::Bone;
        if (s == "Control") return SequencerTargetType::Control;
        if (s == "Null")    return SequencerTargetType::Null;
        if (s == "Socket")  return SequencerTargetType::Socket;
        return SequencerTargetType::Bone;
    }

    const char* SequencerChannelComponentToString(SequencerChannelComponent c) {
        switch (c) {
        case SequencerChannelComponent::X:      return "X";
        case SequencerChannelComponent::Y:      return "Y";
        case SequencerChannelComponent::Z:      return "Z";
        case SequencerChannelComponent::RotX:   return "RotX";
        case SequencerChannelComponent::RotY:   return "RotY";
        case SequencerChannelComponent::RotZ:   return "RotZ";
        case SequencerChannelComponent::ScaleX: return "ScaleX";
        case SequencerChannelComponent::ScaleY: return "ScaleY";
        case SequencerChannelComponent::ScaleZ: return "ScaleZ";
        }
        return "X";
    }

    SequencerChannelComponent SequencerChannelComponentFromString(const std::string& s) {
        if (s == "X")      return SequencerChannelComponent::X;
        if (s == "Y")      return SequencerChannelComponent::Y;
        if (s == "Z")      return SequencerChannelComponent::Z;
        if (s == "RotX")   return SequencerChannelComponent::RotX;
        if (s == "RotY")   return SequencerChannelComponent::RotY;
        if (s == "RotZ")   return SequencerChannelComponent::RotZ;
        if (s == "ScaleX") return SequencerChannelComponent::ScaleX;
        if (s == "ScaleY") return SequencerChannelComponent::ScaleY;
        if (s == "ScaleZ") return SequencerChannelComponent::ScaleZ;
        return SequencerChannelComponent::X;
    }

    const char* SequencerInterpToString(SequencerInterp i) {
        switch (i) {
        case SequencerInterp::Step:         return "Step";
        case SequencerInterp::Linear:       return "Linear";
        case SequencerInterp::CubicEaseIn:  return "CubicEaseIn";
        case SequencerInterp::CubicEaseOut: return "CubicEaseOut";
        case SequencerInterp::Bezier:       return "Bezier";
        case SequencerInterp::EaseInStrong:  return "EaseInStrong";
        case SequencerInterp::EaseOutStrong: return "EaseOutStrong";
        case SequencerInterp::EaseInOut:     return "EaseInOut";
        case SequencerInterp::EaseOutBack:   return "EaseOutBack";
        case SequencerInterp::EaseOutBounce: return "EaseOutBounce";
        }
        return "Linear";
    }

    SequencerInterp SequencerInterpFromString(const std::string& s) {
        if (s == "Step")         return SequencerInterp::Step;
        if (s == "Linear")       return SequencerInterp::Linear;
        if (s == "CubicEaseIn")  return SequencerInterp::CubicEaseIn;
        if (s == "CubicEaseOut") return SequencerInterp::CubicEaseOut;
        if (s == "Bezier")       return SequencerInterp::Bezier;
        if (s == "EaseInStrong")  return SequencerInterp::EaseInStrong;
        if (s == "EaseOutStrong") return SequencerInterp::EaseOutStrong;
        if (s == "EaseInOut")     return SequencerInterp::EaseInOut;
        if (s == "EaseOutBack")   return SequencerInterp::EaseOutBack;
        if (s == "EaseOutBounce") return SequencerInterp::EaseOutBounce;

        // Fallback Linear, e nao um erro: um `.axeseq` gravado por uma versao
        // mais nova abre numa mais velha com a curva errada, mas ABRE — perder
        // a sequence inteira por causa de um nome de curva seria pior.
        return SequencerInterp::Linear;
    }

    // ============================================================
    // Serializacao interna (Struct -> JSON)
    // ============================================================

    namespace {

        void SerializeKey(const SequencerKey& k, nlohmann::json& j) {
            j["frame"] = k.Frame;
            j["value"] = k.Value;
            j["interp"] = SequencerInterpToString(k.Interp);
            if (k.Interp == SequencerInterp::Bezier) {
                j["tangent_in"] = k.TangentIn;
                j["tangent_out"] = k.TangentOut;
            }
            // Gravado so quando significa alguma coisa. Um campo "overshoot: 0"
            // em cada key de um bake denso engordaria o arquivo em megabytes
            // sem dizer nada.
            if (k.Interp == SequencerInterp::EaseOutBack ||
                k.Interp == SequencerInterp::EaseOutBounce) {
                j["overshoot"] = k.Overshoot;
            }
        }

        SequencerKey DeserializeKey(const nlohmann::json& j) {
            SequencerKey k;
            k.Frame = j.value("frame", 0.0f);
            k.Value = j.value("value", 0.0f);
            k.Interp = SequencerInterpFromString(j.value("interp", "Linear"));
            if (k.Interp == SequencerInterp::Bezier) {
                k.TangentIn = j.value("tangent_in", 0.0f);
                k.TangentOut = j.value("tangent_out", 0.0f);
            }
            k.Overshoot = j.value("overshoot", 0.0f);
            return k;
        }

        void SerializeChannel(const SequencerChannel& ch, nlohmann::json& j) {
            j["component"] = SequencerChannelComponentToString(ch.Component);
            j["keys"] = nlohmann::json::array();
            for (const auto& k : ch.Keys) {
                nlohmann::json jk;
                SerializeKey(k, jk);
                j["keys"].push_back(jk);
            }
        }

        SequencerChannel DeserializeChannel(const nlohmann::json& j) {
            SequencerChannel ch;
            ch.Component = SequencerChannelComponentFromString(j.value("component", "X"));
            if (j.contains("keys")) {
                for (const auto& jk : j["keys"]) {
                    ch.Keys.push_back(DeserializeKey(jk));
                }
                ch.SortKeys();
            }
            return ch;
        }

        void SerializeSection(const SequencerSection& sec, nlohmann::json& j) {
            j["start_frame"] = sec.StartFrame;
            j["end_frame"] = sec.EndFrame;
            j["source_clip_uuid"] = sec.SourceClipUUID;  // std::string direto
            j["source_clip_name"] = sec.SourceClipName;  // ver SequencerSection
            j["clip_offset"] = sec.ClipOffset;
            j["clip_rate_scale"] = sec.ClipRateScale;
            j["channels"] = nlohmann::json::array();
            for (const auto& ch : sec.Channels) {
                nlohmann::json jc;
                SerializeChannel(ch, jc);
                j["channels"].push_back(jc);
            }
        }

        SequencerSection DeserializeSection(const nlohmann::json& j) {
            SequencerSection sec;
            sec.StartFrame = j.value("start_frame", 0);
            sec.EndFrame = j.value("end_frame", 0);
            sec.SourceClipUUID = j.value("source_clip_uuid", std::string{});
            // Default vazio: .axeseq gravado antes das tracks de clipe continua
            // carregando, so que sem clipe — que e exatamente o que ele era.
            sec.SourceClipName = j.value("source_clip_name", std::string{});
            sec.ClipOffset = j.value("clip_offset", 0);
            // Default 1.0, e clampado: um rate zero (ou negativo) vindo de um
            // arquivo editado a mao congelaria o clipe no segundo 0, sem erro.
            sec.ClipRateScale = j.value("clip_rate_scale", 1.0f);
            if (!(sec.ClipRateScale > 0.0001f)) sec.ClipRateScale = 1.0f;
            if (j.contains("channels")) {
                for (const auto& jc : j["channels"]) {
                    sec.Channels.push_back(DeserializeChannel(jc));
                }
            }
            return sec;
        }

        void SerializeTrack(const SequencerTrack& tr, nlohmann::json& j) {
            j["type"] = SequencerTrackTypeToString(tr.Type);
            j["target_name"] = tr.TargetName;
            j["target_type"] = SequencerTargetTypeToString(tr.TargetType);
            j["sections"] = nlohmann::json::array();
            for (const auto& sec : tr.Sections) {
                nlohmann::json js;
                SerializeSection(sec, js);
                j["sections"].push_back(js);
            }
            j["muted"] = tr.Muted;
            j["locked"] = tr.Locked;
            j["hold_outside"] = tr.HoldOutsideSections;
            j["attached_asset_uuid"] = tr.AttachedAssetUUID;
        }

        SequencerTrack DeserializeTrack(const nlohmann::json& j) {
            SequencerTrack tr;
            tr.Type = SequencerTrackTypeFromString(j.value("type", "TransformBone"));
            tr.TargetName = j.value("target_name", "");
            tr.TargetType = SequencerTargetTypeFromString(j.value("target_type", "Bone"));
            tr.Muted = j.value("muted", false);
            tr.Locked = j.value("locked", false);
            // Default true: `.axeseq` gravado antes do campo passa a segurar a
            // pose, que e o comportamento que ele deveria ter tido desde sempre.
            tr.HoldOutsideSections = j.value("hold_outside", true);
            tr.AttachedAssetUUID = j.value("attached_asset_uuid", std::string{});
            if (j.contains("sections")) {
                for (const auto& js : j["sections"]) {
                    tr.Sections.push_back(DeserializeSection(js));
                }
            }
            return tr;
        }

        void SerializeBinding(const SequencerBinding& b, nlohmann::json& j) {
            // entity_name: o handle que atravessa save/load (ver SequencerBinding).
            j["entity_name"] = b.EntityName;
            j["entity_uuid"] = b.EntityUUID;     // reservado
            j["display_name"] = b.DisplayName;
            j["rig_asset_uuid"] = b.RigAssetUUID;
            j["rig_controls_follow_anim"] = b.RigControlsFollowAnimation;
            j["tracks"] = nlohmann::json::array();
            for (const auto& tr : b.Tracks) {
                nlohmann::json jt;
                SerializeTrack(tr, jt);
                j["tracks"].push_back(jt);
            }
        }

        SequencerBinding DeserializeBinding(const nlohmann::json& j) {
            SequencerBinding b;
            // Ausente em arquivos gravados antes deste campo existir — cai em vazio,
            // e o binding aparece como "(sem entidade)" no outliner ate ser religado.
            b.EntityName = j.value("entity_name", std::string{});
            b.EntityUUID = j.value("entity_uuid", std::string{});
            b.DisplayName = j.value("display_name", "");
            b.RigAssetUUID = j.value("rig_asset_uuid", std::string{});
            // Default true: `.axeseq` gravado antes deste campo passa a seguir a
            // animacao, que e o comportamento que ele deveria ter tido.
            b.RigControlsFollowAnimation = j.value("rig_controls_follow_anim", true);
            if (j.contains("tracks")) {
                for (const auto& jt : j["tracks"]) {
                    b.Tracks.push_back(DeserializeTrack(jt));
                }
            }
            return b;
        }

    } // namespace

    // ============================================================
    // SequencerChannel::SortKeys / FindBracketingKeys
    // ============================================================

    void SequencerChannel::SortKeys() {
        std::sort(Keys.begin(), Keys.end());
    }

    bool SequencerChannel::FindBracketingKeys(float frame,
        const SequencerKey*& outLeft,
        const SequencerKey*& outRight) const {
        if (Keys.empty()) return false;

        // Caso 1: frame antes da primeira key.
        if (frame <= Keys.front().Frame) {
            outLeft = &Keys.front();
            outRight = &Keys.front();
            return true;
        }
        // Caso 2: frame depois da ultima key.
        if (frame >= Keys.back().Frame) {
            outLeft = &Keys.back();
            outRight = &Keys.back();
            return true;
        }
        // Caso 3: achar bracket interior.
        for (size_t i = 1; i < Keys.size(); ++i) {
            if (frame <= Keys[i].Frame) {
                outLeft = &Keys[i - 1];
                outRight = &Keys[i];
                return true;
            }
        }
        return false;
    }

    // ============================================================
    // SequencerAsset::LoadFromFile / SaveToFile
    // ============================================================

    bool SequencerAsset::LoadFromFile(const std::string& path) {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            return false;
        }

        std::stringstream ss;
        ss << ifs.rdbuf();
        std::string content = ss.str();

        nlohmann::json j;
        try {
            j = nlohmann::json::parse(content);
        }
        catch (const std::exception&) {
            return false;
        }

        std::string version = j.value("version", "");
        if (version == "SEQUENCER_V1") {
            m_Fps = j.value("fps", 30);
            if (j.contains("frame_range")) {
                auto& jr = j["frame_range"];
                m_FrameRange.Start = jr.value("start", 0);
                m_FrameRange.End = jr.value("end", 90);
            }
            m_Bindings.clear();
            if (j.contains("bindings")) {
                for (const auto& jb : j["bindings"]) {
                    m_Bindings.push_back(DeserializeBinding(jb));
                }
            }
            m_Path = path;
            return true;
        }

        return false;
    }

    bool SequencerAsset::SaveToFile(const std::string& path) const {
        nlohmann::json j;
        j["version"] = "SEQUENCER_V1";
        j["fps"] = m_Fps;

        nlohmann::json jr;
        jr["start"] = m_FrameRange.Start;
        jr["end"] = m_FrameRange.End;
        j["frame_range"] = jr;

        j["bindings"] = nlohmann::json::array();
        for (const auto& b : m_Bindings) {
            nlohmann::json jb;
            SerializeBinding(b, jb);
            j["bindings"].push_back(jb);
        }

        std::ofstream ofs(path);
        if (!ofs.is_open()) {
            return false;
        }
        ofs << j.dump(2) << std::endl;
        return true;
    }

    // ============================================================
    // Mutacoes (usadas pelo editor + comandos undo/redo)
    // ============================================================

    int SequencerAsset::AddBinding(const SequencerBinding& binding) {
        m_Bindings.push_back(binding);
        return static_cast<int>(m_Bindings.size() - 1);
    }

    void SequencerAsset::RemoveBinding(int index) {
        if (index < 0 || index >= static_cast<int>(m_Bindings.size())) return;
        m_Bindings.erase(m_Bindings.begin() + index);
    }

    SequencerBinding* SequencerAsset::GetBinding(int index) {
        if (index < 0 || index >= static_cast<int>(m_Bindings.size())) return nullptr;
        return &m_Bindings[index];
    }

    const SequencerBinding* SequencerAsset::GetBinding(int index) const {
        if (index < 0 || index >= static_cast<int>(m_Bindings.size())) return nullptr;
        return &m_Bindings[index];
    }

    int SequencerAsset::AddTrack(int bindingIndex, const SequencerTrack& track) {
        auto* b = GetBinding(bindingIndex);
        if (!b) return -1;
        b->Tracks.push_back(track);
        return static_cast<int>(b->Tracks.size() - 1);
    }

    void SequencerAsset::RemoveTrack(int bindingIndex, int trackIndex) {
        auto* b = GetBinding(bindingIndex);
        if (!b) return;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size())) return;
        b->Tracks.erase(b->Tracks.begin() + trackIndex);
    }

    int SequencerAsset::AddSection(int bindingIndex, int trackIndex,
        const SequencerSection& section) {
        auto* b = GetBinding(bindingIndex);
        if (!b) return -1;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size())) return -1;
        b->Tracks[trackIndex].Sections.push_back(section);
        return static_cast<int>(b->Tracks[trackIndex].Sections.size() - 1);
    }

    int SequencerAsset::AddChannel(int bindingIndex, int trackIndex, int sectionIndex,
        const SequencerChannel& channel) {
        auto* b = GetBinding(bindingIndex);
        if (!b) return -1;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size())) return -1;
        auto& tr = b->Tracks[trackIndex];
        if (sectionIndex < 0 || sectionIndex >= static_cast<int>(tr.Sections.size())) return -1;
        tr.Sections[sectionIndex].Channels.push_back(channel);
        return static_cast<int>(tr.Sections[sectionIndex].Channels.size() - 1);
    }

    SequencerChannel* SequencerAsset::FindOrCreateChannel(int bindingIndex, int trackIndex,
        int sectionIndex,
        SequencerChannelComponent component) {
        auto* b = GetBinding(bindingIndex);
        if (!b) return nullptr;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size())) return nullptr;
        auto& tr = b->Tracks[trackIndex];
        if (sectionIndex < 0 || sectionIndex >= static_cast<int>(tr.Sections.size())) return nullptr;
        auto& sec = tr.Sections[sectionIndex];
        for (auto& ch : sec.Channels) {
            if (ch.Component == component) return &ch;
        }
        SequencerChannel newCh;
        newCh.Component = component;
        sec.Channels.push_back(newCh);
        return &sec.Channels.back();
    }

    int SequencerAsset::AddKey(int bindingIndex, int trackIndex, int sectionIndex,
        int channelIndex, const SequencerKey& key) {
        auto* b = GetBinding(bindingIndex);
        if (!b) return -1;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size())) return -1;
        auto& tr = b->Tracks[trackIndex];
        if (sectionIndex < 0 || sectionIndex >= static_cast<int>(tr.Sections.size())) return -1;
        auto& sec = tr.Sections[sectionIndex];
        if (channelIndex < 0 || channelIndex >= static_cast<int>(sec.Channels.size())) return -1;
        auto& ch = sec.Channels[channelIndex];

        // Se ja existe key no mesmo frame (tolerancia 0.01), substitui.
        const float tolerance = 0.01f;
        for (size_t i = 0; i < ch.Keys.size(); ++i) {
            if (std::abs(ch.Keys[i].Frame - key.Frame) < tolerance) {
                ch.Keys[i] = key;
                return static_cast<int>(i);
            }
        }
        ch.Keys.push_back(key);
        ch.SortKeys();
        // Acha o indice da key recem-adicionada (posicao pode ter mudado no sort).
        for (size_t i = 0; i < ch.Keys.size(); ++i) {
            if (ch.Keys[i].Frame == key.Frame && ch.Keys[i].Value == key.Value) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void SequencerAsset::RemoveKey(int bindingIndex, int trackIndex, int sectionIndex,
        int channelIndex, int keyIndex) {
        auto* b = GetBinding(bindingIndex);
        if (!b) return;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(b->Tracks.size())) return;
        auto& tr = b->Tracks[trackIndex];
        if (sectionIndex < 0 || sectionIndex >= static_cast<int>(tr.Sections.size())) return;
        auto& sec = tr.Sections[sectionIndex];
        if (channelIndex < 0 || channelIndex >= static_cast<int>(sec.Channels.size())) return;
        auto& ch = sec.Channels[channelIndex];
        if (keyIndex < 0 || keyIndex >= static_cast<int>(ch.Keys.size())) return;
        ch.Keys.erase(ch.Keys.begin() + keyIndex);
    }

} // namespace axe