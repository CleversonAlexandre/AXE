// src/axe/animation/sequencer/sequencer_player.cpp
//
// Implementacao do SequencerPlayer. Fase 2 (MVP): deep copy + Sample real.

#include "axe/animation/sequencer/sequencer_player.hpp"
#include "axe/animation/sequencer/sequencer_asset.hpp"

#include <algorithm>
#include <cmath>

namespace axe {

    // ============================================================
    // Ciclo de vida
    // ============================================================

    void SequencerPlayer::OnStart(const SequencerAsset& asset) {
        // DEEP COPY. Bug R1 do SEQUENCER_DESIGN §12: nunca compartilhar estado
        // entre players. Cada entidade com SequencerPlayerComponent tem sua
        // propia copia das bindings/tracks/sections/channels/keys.
        m_Bindings = asset.GetBindings();   // std::vector copia recursivamente

        m_Fps = asset.GetFps();
        auto range = asset.GetFrameRange();
        m_StartFrame = static_cast<float>(range.Start);
        m_EndFrame = static_cast<float>(range.End);
        m_CurrentFrame = m_StartFrame;
        m_Mode = SequencerPlaybackMode::Paused;
        m_LastSamples.clear();
    }

    void SequencerPlayer::OnUpdate(float deltaTime) {
        if (m_Mode == SequencerPlaybackMode::Playing) {
            m_CurrentFrame += deltaTime * static_cast<float>(m_Fps);

            // Loop ou clamp.
            if (m_CurrentFrame > m_EndFrame) {
                if (m_Loop) {
                    m_CurrentFrame = m_StartFrame +
                        std::fmod(m_CurrentFrame - m_StartFrame,
                            m_EndFrame - m_StartFrame);
                }
                else {
                    m_CurrentFrame = m_EndFrame;
                    m_Mode = SequencerPlaybackMode::Paused;
                }
            }
        }
        // Scrubbing mode: m_CurrentFrame ja foi setado por Scrub(). Nada a fazer aqui.

        Resample();
    }

    void SequencerPlayer::SyncFrom(const SequencerAsset& asset) {
        // Mesma copia do OnStart, sem tocar em tempo nem em modo de playback.
        m_Bindings = asset.GetBindings();
        m_Fps = asset.GetFps();

        const auto range = asset.GetFrameRange();
        m_StartFrame = static_cast<float>(range.Start);
        m_EndFrame = static_cast<float>(range.End);

        if (m_Fps <= 0) m_Fps = 30;   // ver SetFps

        // O range pode ter encolhido debaixo do playhead.
        m_CurrentFrame = std::clamp(m_CurrentFrame, m_StartFrame, m_EndFrame);

        // Ordena as keys da COPIA, defensivamente.
        //
        // `AddKey` ordena, mas editar o campo "Frame" no painel de key (ou
        // arrastar) mexe no valor direto, sem reordenar. Uma key fora de ordem
        // quebra o FindBracketingKeys em silencio: ele decide "frame depois da
        // ultima key" olhando `Keys.back()`, e passa a devolver sempre a mesma
        // key — a curva inteira vira uma constante.
        //
        // Ordenar aqui e O(n) num vetor ja ordenado, roda uma vez por frame, e
        // imuniza a amostragem de qualquer estado torto do asset.
        for (auto& b : m_Bindings)
            for (auto& tr : b.Tracks)
                for (auto& sec : tr.Sections)
                    for (auto& ch : sec.Channels)
                        ch.SortKeys();

        Resample();
    }

    void SequencerPlayer::OnStop() {
        m_Bindings.clear();
        m_LastSamples.clear();
        m_CurrentFrame = 0.0f;
        m_Mode = SequencerPlaybackMode::Paused;
    }

    // ============================================================
    // Sample (interpola keys ativas no frame atual)
    // ============================================================

    float SequencerPlayer::Interpolate(const SequencerKey& left,
        const SequencerKey& right,
        float frame) {
        // Caso trivial: mesmo frame (clamping nas pontas).
        if (std::abs(right.Frame - left.Frame) < 0.001f) {
            return left.Value;
        }

        float t = (frame - left.Frame) / (right.Frame - left.Frame);
        t = std::clamp(t, 0.0f, 1.0f);

        switch (left.Interp) {
        case SequencerInterp::Step:
            return left.Value;

        case SequencerInterp::Linear:
            return left.Value + (right.Value - left.Value) * t;

        case SequencerInterp::CubicEaseIn:
            // t*t (slow-in)
            return left.Value + (right.Value - left.Value) * (t * t);

        case SequencerInterp::CubicEaseOut:
            // 1 - (1-t)^2 (slow-out)
            return left.Value + (right.Value - left.Value) * (1.0f - (1.0f - t) * (1.0f - t));

        case SequencerInterp::Bezier: {
            // Cubic bezier com tangents.
            float v0 = left.Value;
            float v1 = left.Value + left.TangentOut;
            float v2 = right.Value - right.TangentIn;
            float v3 = right.Value;
            float u = 1.0f - t;
            return u * u * u * v0 + 3 * u * u * t * v1 + 3 * u * t * t * v2 + t * t * t * v3;
        }
        }
        return left.Value;  // fallback
    }

    void SequencerPlayer::Resample() {
        m_LastSamples.clear();

        for (int bi = 0; bi < static_cast<int>(m_Bindings.size()); ++bi) {
            auto& b = m_Bindings[bi];
            for (auto& tr : b.Tracks) {
                if (tr.Muted) continue;

                // Acha a section ativa no frame atual.
                SequencerSection* activeSec = nullptr;
                for (auto& sec : tr.Sections) {
                    if (sec.ContainsFrame(m_CurrentFrame)) {
                        activeSec = &sec;
                        break;
                    }
                }
                if (!activeSec) continue;

                for (auto& ch : activeSec->Channels) {
                    if (ch.Keys.empty()) continue;

                    const SequencerKey* left = nullptr;
                    const SequencerKey* right = nullptr;
                    if (!ch.FindBracketingKeys(m_CurrentFrame, left, right)) continue;

                    SequencerSample s;
                    s.BindingIndex = bi;
                    s.TargetName = tr.TargetName;
                    s.TargetType = tr.TargetType;
                    s.Component = ch.Component;
                    s.Value = Interpolate(*left, *right, m_CurrentFrame);
                    m_LastSamples.push_back(s);
                }
            }
        }
    }

} // namespace axe