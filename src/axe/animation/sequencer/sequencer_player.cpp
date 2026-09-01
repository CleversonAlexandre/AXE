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
        m_LastClipSamples.clear();
        m_FiredEvents.clear();
        m_ActiveCamera.clear();

        // Ver a nota do campo: um tiquinho antes do inicio, para que uma key no
        // primeiro frame da sequence dispare.
        m_EventFrame = m_StartFrame - 0.001f;
    }

    void SequencerPlayer::OnUpdate(float deltaTime) {
        m_FiredEvents.clear();

        if (m_Mode == SequencerPlaybackMode::Playing) {
            m_CurrentFrame += deltaTime * static_cast<float>(m_Fps);

            // Loop ou clamp.
            if (m_CurrentFrame > m_EndFrame) {
                if (m_Loop) {
                    // A VOLTA E DOIS TRECHOS, NAO UM.
                    //
                    // O playhead saltou do meio da sequence de volta para o
                    // comeco. Um unico intervalo (from, to] com from > to nao
                    // contem key nenhuma — e todo evento entre o ponto de saida
                    // e o fim seria pulado silenciosamente a cada volta, o que
                    // e exatamente o fim da sequence: onde a maioria dos
                    // eventos interessantes mora.
                    CollectEvents(m_EventFrame, m_EndFrame);

                    m_CurrentFrame = m_StartFrame +
                        std::fmod(m_CurrentFrame - m_StartFrame,
                            m_EndFrame - m_StartFrame);

                    m_EventFrame = m_StartFrame - 0.001f;
                }
                else {
                    m_CurrentFrame = m_EndFrame;
                    m_Mode = SequencerPlaybackMode::Paused;
                }
            }

            CollectEvents(m_EventFrame, m_CurrentFrame);
            m_EventFrame = m_CurrentFrame;
        }
        // Scrubbing mode: m_CurrentFrame ja foi setado por Scrub(). Nada a fazer
        // aqui — e, de proposito, nenhum evento.

        Resample();
    }

    void SequencerPlayer::CollectEvents(float from, float to) {
        if (to <= from) return;

        for (int bi = 0; bi < static_cast<int>(m_Bindings.size()); ++bi) {
            const auto& b = m_Bindings[bi];

            for (const auto& tr : b.Tracks) {
                if (tr.Type != SequencerTrackType::Event) continue;
                if (tr.Muted) continue;

                // Sem exigir section ATIVA: um evento e um instante, e nao um
                // valor que precise de uma janela de tempo valida ao redor. Uma
                // key de evento fora de qualquer section ainda e uma key que o
                // playhead cruzou.
                for (const auto& sec : tr.Sections) {
                    for (const auto& ch : sec.Channels) {
                        for (const auto& k : ch.Keys) {
                            if (k.Frame <= from || k.Frame > to) continue;

                            SequencerEventSample ev;
                            ev.BindingIndex = bi;
                            ev.EventName = tr.TargetName;
                            ev.Value = k.Value;
                            ev.Frame = k.Frame;
                            m_FiredEvents.push_back(ev);
                        }
                    }
                }
            }
        }
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

        // O mesmo vale para o cursor de eventos: se o range encolheu, um cursor
        // alem do novo fim deixaria a sequence inteira "ja disparada".
        m_EventFrame = std::clamp(m_EventFrame, m_StartFrame - 0.001f, m_EndFrame);

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
        m_LastClipSamples.clear();
        m_FiredEvents.clear();
        m_ActiveCamera.clear();
        m_CurrentFrame = 0.0f;
        m_EventFrame = -1.0f;
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
            // ── BEZIER 2D, RESOLVIDO POR x ───────────────────────────────────
            //
            // As alcas tem componente de TEMPO (os pesos) e de VALOR (as
            // tangentes). `t` aqui e a posicao no trecho — o x que queremos —,
            // e nao o parametro da curva: com alcas fora de 1/3 os dois deixam
            // de coincidir.
            //
            // Com peso 1/3 dos dois lados x(u) = u exatamente (ver a nota em
            // SequencerKey), a busca converge no primeiro palpite e o resultado
            // e termo a termo o da formula antiga. Nenhuma key gravada muda.
            const float wOut = std::clamp(left.TangentOutWeight, 0.01f, 0.99f);
            const float wIn = std::clamp(right.TangentInWeight, 0.01f, 0.99f);

            // Pontos de controle em x, dentro do trecho normalizado [0,1].
            const float x1 = wOut;
            const float x2 = 1.0f - wIn;

            auto bezX = [&](float u) {
                const float m = 1.0f - u;
                return 3.0f * m * m * u * x1 + 3.0f * m * u * u * x2 + u * u * u;
                };

            // Bisseccao, e nao Newton: x e monotonico com x1 e x2 em [0,1] (a
            // mesma restricao da cubic-bezier do CSS), entao a bisseccao SEMPRE
            // converge e nunca diverge. Newton seria mais rapido e precisaria de
            // salvaguarda para derivada perto de zero nas pontas — mais codigo
            // para economizar microssegundos num laco que roda por canal.
            float lo = 0.0f, hi = 1.0f, u = t;

            for (int i = 0; i < 24; ++i) {
                const float x = bezX(u);
                if (std::abs(x - t) < 1e-5f) break;
                if (x < t) lo = u; else hi = u;
                u = 0.5f * (lo + hi);
            }

            const float v0 = left.Value;
            const float v1 = left.Value + left.TangentOut;
            const float v2 = right.Value - right.TangentIn;
            const float v3 = right.Value;

            const float m = 1.0f - u;
            return m * m * m * v0 + 3 * m * m * u * v1 + 3 * m * u * u * v2 + u * u * u * v3;
        }

                                    // ── AS CURVAS DE IMPACTO ─────────────────────────────────────────────
                                    //
                                    // Todas escrevem como `mix(a, b, f(t))`, com f(0)=0 e f(1)=1. Isso
                                    // garante que as duas pontas do trecho passam EXATAMENTE pelos valores
                                    // das keys — o que importa quando o frame 0 e o frame final tem de
                                    // devolver a pose de repouso identica. `EaseOutBack` e a excecao
                                    // deliberada NO MEIO: f passa de 1 e volta, e e justamente esse trecho
                                    // acima de 1 que produz a ultrapassagem.
        case SequencerInterp::EaseInStrong: {
            const float f = t * t * t * t;
            return left.Value + (right.Value - left.Value) * f;
        }

        case SequencerInterp::EaseOutStrong: {
            const float u = 1.0f - t;
            const float f = 1.0f - u * u * u * u;
            return left.Value + (right.Value - left.Value) * f;
        }

        case SequencerInterp::EaseInOut: {
            // Smootherstep (6t^5-15t^4+10t^3): derivada E segunda derivada
            // zeradas nas duas pontas. Encadear duas smoothstep comuns deixa um
            // "canto" de aceleracao na emenda, visivel como um tranco.
            const float f = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
            return left.Value + (right.Value - left.Value) * f;
        }

        case SequencerInterp::EaseOutBack: {
            // 1.70158 e a constante classica (~10% de ultrapassagem). O campo
            // Overshoot substitui quando o animador quer mais coice.
            const float s = (left.Overshoot > 0.0001f) ? left.Overshoot : 1.70158f;
            const float u = t - 1.0f;
            const float f = 1.0f + (s + 1.0f) * u * u * u + s * u * u;
            return left.Value + (right.Value - left.Value) * f;
        }

        case SequencerInterp::EaseOutBounce: {
            // Quique classico: quatro parabolas com alturas decrescentes.
            const float n1 = 7.5625f;
            const float d1 = 2.75f;
            float x = t;
            float f;

            if (x < 1.0f / d1) {
                f = n1 * x * x;
            }
            else if (x < 2.0f / d1) {
                x -= 1.5f / d1;
                f = n1 * x * x + 0.75f;
            }
            else if (x < 2.5f / d1) {
                x -= 2.25f / d1;
                f = n1 * x * x + 0.9375f;
            }
            else {
                x -= 2.625f / d1;
                f = n1 * x * x + 0.984375f;
            }

            // Overshoot > 0 exagera a altura dos quiques sem mexer no tempo
            // deles — mesma leitura do campo no EaseOutBack.
            if (left.Overshoot > 0.0001f)
                f = 1.0f - (1.0f - f) * left.Overshoot;

            return left.Value + (right.Value - left.Value) * f;
        }
        }
        return left.Value;  // fallback
    }

    void SequencerPlayer::Resample() {
        m_LastSamples.clear();

        // ── POR QUE ESTA LINHA EXISTE, E POR QUE FALTAVA ─────────────────────
        //
        // `m_LastClipSamples` nunca era escrito nem limpo. O editor perguntava
        // `GetLastClipSamples()` todo frame, recebia uma lista vazia, nao achava
        // clipe base nenhum e caia em `Pose::FromBindPose` — a T-pose. Soltar um
        // clipe na timeline produzia uma section roxa perfeita, com duracao
        // certa e nome resolvido, e um personagem parado de bracos abertos.
        //
        // Todo o resto da cadeia (SamplePose, PoseOverride, BuildSkinningMatrices)
        // ja estava pronto e correto: o unico elo faltando era o produtor.
        m_LastClipSamples.clear();

        // Corte de camera: estado, nao evento. Recomecar do zero todo Resample
        // e o que garante que voltar o playhead para antes do primeiro corte
        // devolve a camera padrao, em vez de deixar o ultimo corte grudado.
        m_ActiveCamera.clear();
        float bestCutFrame = -1e9f;

        const float fps = (m_Fps > 0) ? static_cast<float>(m_Fps) : 30.0f;

        for (int bi = 0; bi < static_cast<int>(m_Bindings.size()); ++bi) {
            auto& b = m_Bindings[bi];
            for (auto& tr : b.Tracks) {
                if (tr.Muted) continue;

                // Track de Event nao produz VALOR. Ver CollectEvents: ela e
                // lida por instante cruzado, em OnUpdate. Sem este `continue`
                // cada key de evento viraria um SequencerSample com TargetName
                // igual ao nome do evento — e o aplicador iria procurar um osso
                // chamado "Tiro".
                if (tr.Type == SequencerTrackType::Event) continue;

                // ── CORTE DE CAMERA ──────────────────────────────────────────
                //
                // Tambem nao produz valor: a key nao diz "quanto", diz "a
                // partir daqui". Vence a key de MAIOR frame <= playhead, entre
                // todas as tracks de corte — e por isso a comparacao e feita
                // aqui, no meio da varredura, e nao track a track.
                //
                // Empate no mesmo frame: fica a PRIMEIRA track na ordem do
                // arquivo. E arbitrario, mas e estavel; o que nao se pode ter e
                // o resultado mudando conforme a ordem de iteracao.
                if (tr.Type == SequencerTrackType::CameraCut) {
                    if (tr.TargetName.empty()) continue;

                    for (const auto& sec : tr.Sections) {
                        for (const auto& ch : sec.Channels) {
                            for (const auto& k : ch.Keys) {
                                if (k.Frame > m_CurrentFrame) continue;
                                if (k.Frame <= bestCutFrame) continue;

                                bestCutFrame = k.Frame;
                                m_ActiveCamera = tr.TargetName;
                            }
                        }
                    }
                    continue;
                }

                // ── CAMADA BASE: track de clipe ──────────────────────────────
                //
                // Tratada ANTES da busca de section comum, e nao dentro dela:
                // uma section de clipe nao TEM canais (um clipe nao se edita por
                // componente), entao o laco de canais rodaria zero vezes e a
                // track inteira sumiria da avaliacao.
                if (tr.Type == SequencerTrackType::AnimationClip) {
                    if (tr.Sections.empty()) continue;

                    // Section ativa, ou — se o playhead estiver fora de todas —
                    // a mais proxima, com o tempo grampeado na borda dela.
                    //
                    // ── POR QUE SEGURAR A POSE ───────────────────────────────
                    //
                    //   Sem isto, passar do ultimo frame da section devolvia a
                    //   pose de repouso: o personagem terminava a animacao e
                    //   ABRIA OS BRACOS em T-pose no frame seguinte. Numa
                    //   sequence de 125 frames com um clipe de 35, eram 90
                    //   frames de T-pose.
                    //
                    //   Segurar a borda e o que todo NLE de animacao faz (o
                    //   "Keep State" do Sequencer da Unreal e o default de la
                    //   tambem): fora da section a performance congela no
                    //   primeiro/ultimo frame, e o animador enxerga o buraco
                    //   como uma pausa, nao como um bug.
                    //
                    //   Quem quiser o comportamento antigo desliga
                    //   `HoldOutsideSections` na track.
                    SequencerSection* activeSec = nullptr;
                    float evalFrame = m_CurrentFrame;

                    for (auto& sec : tr.Sections) {
                        if (sec.ContainsFrame(m_CurrentFrame)) { activeSec = &sec; break; }
                    }

                    if (!activeSec && tr.HoldOutsideSections) {
                        // Section mais proxima do playhead. Com uma section so —
                        // o caso normal — isto e simplesmente ela.
                        float bestDist = -1.0f;
                        for (auto& sec : tr.Sections) {
                            const float s = static_cast<float>(sec.StartFrame);
                            const float e = static_cast<float>(sec.EndFrame);
                            const float d = (m_CurrentFrame < s) ? (s - m_CurrentFrame)
                                : (m_CurrentFrame - e);
                            if (bestDist < 0.0f || d < bestDist) {
                                bestDist = d;
                                activeSec = &sec;
                            }
                        }
                        if (activeSec) {
                            evalFrame = std::clamp(m_CurrentFrame,
                                static_cast<float>(activeSec->StartFrame),
                                static_cast<float>(activeSec->EndFrame));
                        }
                    }

                    if (!activeSec) continue;

                    const std::string& clipName = !activeSec->SourceClipName.empty()
                        ? activeSec->SourceClipName
                        : tr.TargetName;   // fallback: sections antigas sem o campo
                    if (clipName.empty()) continue;

                    // frame da timeline -> frame DENTRO do clipe -> segundos.
                    //
                    // O rate entra multiplicando porque o CreateClipTrack dividiu
                    // por ele para achar o comprimento da section: e a inversa
                    // exata do mesmo mapeamento (ver SequencerSection::ClipRateScale).
                    const float rate = (activeSec->ClipRateScale > 0.0001f)
                        ? activeSec->ClipRateScale : 1.0f;

                    const float localFrame =
                        (evalFrame - static_cast<float>(activeSec->StartFrame)) +
                        static_cast<float>(activeSec->ClipOffset);

                    SequencerClipSample cs;
                    cs.BindingIndex = bi;
                    cs.ClipName = clipName;
                    cs.TimeSeconds = (localFrame / fps) * rate;

                    // ClipOffset negativo daria tempo negativo. Clampar aqui e
                    // mais honesto que deixar o WrapTime do clipe interpretar o
                    // sinal — em clipe com loop, -0.1s viraria o FIM da animacao.
                    if (cs.TimeSeconds < 0.0f) cs.TimeSeconds = 0.0f;

                    m_LastClipSamples.push_back(cs);
                    continue;
                }

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