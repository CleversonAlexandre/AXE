#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include <memory>
#include <string>
#include <map>

namespace axe
{
    class Texture2D;
    class Shader;

    struct AXE_API DirectionalLight
    {
        glm::vec3 Direction{ -0.3f, -1.0f, -0.3f };
        glm::vec3 Color{ 1.0f,  1.0f,  1.0f };

        float Intensity = 1.0f;
        float SpecularStrength = 0.5f;
        float Shininess = 32.0f;

        // ── SKY_LIGHT_V1 — CAMPOS LEGADOS, NÃO EDITAR MAIS AQUI ──────────────
        //
        // Estes três descreviam a LUZ DE AMBIENTE, e viviam dentro da luz
        // direcional apenas por acidente histórico. Isso é o que fazia o sol
        // parecer o dono de tudo: apagar a luz direcional levava junto o
        // controle do ambiente, que não tem nada a ver com o sol.
        //
        // Hoje eles pertencem ao SkyLight (logo abaixo), que é uma entidade
        // própria — o mesmo desenho da Unreal, onde Directional Light e Sky
        // Light são atores separados.
        //
        // Continuam AQUI, e continuam sendo serializados, de propósito:
        //   - cena salva antes desta mudança guarda os valores neste bloco, e
        //     é daqui que a migração no SceneSerializer semeia o SkyLight novo;
        //   - se por algum caminho a cena chegar ao render sem SkyLight, o
        //     lighting pass cai de volta nestes valores em vez de escurecer.
        // Removê-los quebraria toda cena existente EM SILÊNCIO.
        float AmbientStrength = 0.15f;
        float IBLIntensity = 1.0f;
        float AmbientShadowFactor = 1.0f;

        // Shadow mapping
        bool  CastShadows = true;
        float ShadowDistance = 50.0f; // tamanho do frustum ortográfico

        // ── SHADOW_BIAS_METERS_V1 — AGORA EM METROS DE MUNDO ─────────────────
        //
        // Antes era uma fração adimensional do intervalo de profundidade da
        // cascade, e esse intervalo varia de ~27 m (cascade 0, Shadow Distance
        // 50) a ~1474 m (cascade 3, Shadow Distance 200). O mesmo 0.005 valia,
        // portanto, 14 cm num canto da cena e 1,8 m no outro — e mudar o
        // Shadow Distance multiplicava tudo de uma vez. Era impossível achar um
        // valor certo, e era por isso que mexer na distância obrigava a
        // re-ajustar o bias.
        //
        // Hoje o grosso do bias é calculado sozinho no shader, a partir do
        // tamanho REAL do texel de cada cascade e da inclinação da luz. Este
        // campo é só o PISO CONSTANTE, em metros, para a precisão do próprio
        // depth buffer — e ele significa a mesma coisa nas 4 cascades e em
        // qualquer Shadow Distance.
        //
        // Regra prática: 0 funciona na maioria das cenas. Subir só se aparecer
        // acne (listras) em superfície muito rasante; passar de ~10 cm começa a
        // descolar a sombra do pé do objeto.
        //
        // Cenas salvas antes desta mudança carregam 0.005, que agora são 5 mm —
        // valor inofensivo, porque a parcela automática faz o trabalho.
        float ShadowBias = 0.02f; // metros

        // ── PCSS_V1 — TAMANHO ANGULAR DA FONTE, EM GRAUS ─────────────────────
        //
        // O sol real tem 0.53 grau de diametro visto da Terra. NAO e trivia: e
        // exatamente esse numero que faz a sombra dele ser NITIDA no pe do
        // objeto e ir borrando com a distancia. Uma fonte de tamanho ZERO
        // produziria sombra de borda infinitamente dura em qualquer distancia —
        // que nao existe na natureza e e o que o olho acusa como "de jogo".
        //
        // Aqui e um controle de ARTE, nao so de fisica: subir simula ceu
        // nublado (fonte enorme, sombra difusa); 0 desliga o PCSS e volta ao
        // PCF de raio fixo, que e mais barato.
        float SunAngularDegrees = 0.53f;

        // ═══════════════════════════════════════════════════════════════════
        //  CONTACT_SHADOW_V1 — comprimento do raio, em METROS. 0 desliga.
        //
        //  Complementa o shadow map na escala em que ele nao alcanca: contato
        //  menor que um texel do mapa (o pe no chao, o vinco da caixa na
        //  parede) simplesmente nao existe la, e o objeto parece flutuar.
        //
        //  E propriedade da LUZ pelo mesmo motivo que na Unreal: e a sombra
        //  DESTE sol, e o comprimento util depende da escala da cena. 10-30 cm
        //  cobre contato humano; passar disso comeca a inventar sombra longa
        //  em espaco de tela, que some quando o bloqueador sai do quadro.
        float ContactShadowLength = 0.15f; // metros

        // Teto da penumbra em TEXEIS, por cascade. Sem ele, um objeto muito
        // alto sobre um chao distante pediria um raio de dezenas de texeis e o
        // custo explodiria — o PCF e O(raio^2) em amostras uteis.
        float MaxPenumbraTexels = 16.0f;

        // Cookie — textura projetada num plano perpendicular à direção da
        // luz (paralela/infinita, como o sol, então a projeção é por
        // tiling, não por cone). CookieScale é o tamanho de um "tile" da
        // textura, em unidades de mundo.
        std::shared_ptr<Texture2D> CookieTexture;
        std::string CookieTextureUUID;
        float CookieScale = 5.0f;

        // Light Material — ver comentário equivalente em PointLight.
        std::string LightMaterialUUID;
        std::shared_ptr<Shader> LightMaterialShader;
        std::map<std::string, std::shared_ptr<Texture2D>> LightMaterialSamplers;

        // ── SKY_OWNS_SKY_V1 — CAMPOS LEGADOS, NÃO EDITAR MAIS AQUI ───────────
        //
        // Todo o bloco abaixo descreve o CÉU, não o sol, e vivia aqui pelo
        // mesmo acidente histórico dos campos de ambiente. O sintoma disso era
        // concreto: apagando a Luz Direcional, `ProceduralSky` sumia junto com
        // ela, o céu procedural se desligava sem ninguém pedir e a cena caía em
        // silêncio no HDRI do Environment — que ilumina sem sol nenhum. Ou
        // seja, o céu ficava sem dono exatamente quando mais precisava de um.
        //
        // Hoje pertencem ao SkyLight. O que a luz direcional ainda dá ao céu é
        // só o que é dela de fato: DIREÇÃO, COR e INTENSIDADE do sol.
        //
        // Continuam aqui e continuam sendo serializados pelo mesmo motivo dos
        // campos de ambiente: é daqui que a migração no SceneSerializer semeia
        // o SkyLight, e removê-los quebraria toda cena existente em silêncio.
        bool  ProceduralSky = false;

        bool  TimeOfDayEnabled = false;
        float Hour = 12.0f;  // 0=meia-noite, 12=meio-dia, 24=meia-noite
        float DaySpeed = 1.0f;   // 1=tempo real, 60=1min/seg
        float SunLatitude = -23.0f;

        float Turbidity = 2.5f;
        float CloudCoverage = 0.4f;
        float CloudSpeed = 0.015f;
        glm::vec3 CloudColor{ 1.f, 1.f, 1.f };
        glm::vec3 NightColor{ 0.005f, 0.005f, 0.02f };

        // Resultado da última avaliação do Light Material — TRANSITÓRIO,
        // recalculado do zero a cada frame (nunca serializado, nunca
        // multiplicado em si mesmo). Necessário porque, diferente da Point
        // Light, esta struct é referenciada por ponteiro direto pelo
        // SceneCollector (não copiada por frame) — multiplicar Color
        // direto aqui composto a cada frame seria um bug (a cor iria pra
        // preto ou explodiria com o tempo). O Lighting Pass multiplica
        // Color por este valor na hora de fazer upload do uniform.
        glm::vec3 LightMaterialResult{ 1.0f, 1.0f, 1.0f };
    };

    // ═══════════════════════════════════════════════════════════════════════
    //  SKY_LIGHT_V1 — A LUZ DE AMBIENTE GANHA DONO PRÓPRIO
    //
    //  ── O QUE ESTAVA ERRADO ────────────────────────────────────────────────
    //
    //  O ambiente da cena não tinha entidade. Intensidade do IBL, ambiente
    //  chapado e o quanto a sombra bloqueia o ambiente moravam DENTRO do
    //  DirectionalLight. Consequência prática: não existia como raciocinar
    //  sobre "quanta luz o céu dá" separado de "quanta luz o sol dá", e apagar
    //  o sol levava junto o dono do ambiente. Era metade do motivo do sol
    //  parecer cosmético — a outra metade era o céu não obedecer ao sol, que o
    //  SKYLIGHT_CHAIN_V1 resolveu.
    //
    //  ── O DESENHO ──────────────────────────────────────────────────────────
    //
    //  Mesma separação da Unreal: Directional Light é o sol (luz direta,
    //  sombra), Sky Light é o ambiente (o céu iluminando de todos os lados).
    //  São dois atores, e cada um pode existir sem o outro:
    //
    //    sol sem sky light  -> luz direta e sombra dura, sem preenchimento
    //    sky light sem sol  -> luz difusa de céu nublado, sem sombra projetada
    //    nenhum dos dois    -> escuro, que é o esperado
    //
    //  A CADEIA continua intacta e é o ponto: o Sky Light não inventa luz, ele
    //  ESCALA o cubemap capturado do céu — e esse céu obedece ao sol desde o
    //  SKYLIGHT_CHAIN_V1. Então baixar o sol continua escurecendo o ambiente
    //  mesmo com o Sky Light em 1.0, exatamente como na vida real: o céu é
    //  brilhante porque o sol o ilumina, não por conta própria.
    //
    //  ── ONDE ESTA STRUCT MORA, E POR QUÊ AQUI ──────────────────────────────
    //
    //  Ela é conceitualmente irmã do DirectionalLight, e fica no mesmo header
    //  de propósito: arquivo NOVO obrigaria a regerar o projeto pelo premake
    //  (o `files` usa glob, e glob é resolvido na hora de gerar). Mesmo motivo
    //  que levou o ShadingModelID para dentro de material_cooked.hpp.
    // ═══════════════════════════════════════════════════════════════════════
    struct AXE_API SkyLight
    {
        // Desligar NÃO é o mesmo que Intensity 0: com Enabled=false o ambiente
        // some inteiro (inclusive o chapado), e é assim que se testa "quanto
        // desta imagem é o sol". Com Intensity 0 o chapado ainda sobrevive.
        bool  Enabled = true;

        // Multiplicador do IBL difuso + especular vindo do céu.
        // (era DirectionalLight::IBLIntensity)
        float Intensity = 1.0f;

        // Tinta do ambiente. Existe porque céu é a fonte mais fácil de
        // desequilibrar a cor de uma cena inteira, e porque num jogo estilizado
        // o ambiente é escolha de arte, não medição — aqui dá para puxar o
        // preenchimento para o frio ou para o quente sem tocar no sol.
        glm::vec3 Color{ 1.0f, 1.0f, 1.0f };

        // 0 = ambiente respeitado pelas sombras (interior escuro)
        // 1 = ambiente livre, ilumina tudo (céu aberto, padrão)
        // (era DirectionalLight::AmbientShadowFactor)
        float ShadowFactor = 1.0f;

        // Ambiente CHAPADO, somado por cima do IBL e independente do céu.
        // Default 0 de propósito para cena nova: desde o SKY_IBL_V1 quem
        // preenche é o céu, e constante chapada é justamente o que faz a
        // imagem parecer sem profundidade. Cena antiga herda o 0.15 que estava
        // no DirectionalLight, então nada muda de aparência sozinho.
        // (era DirectionalLight::AmbientStrength)
        float ConstantAmbient = 0.0f;

        // ═══════════════════════════════════════════════════════════════════
        //  SKY_OWNS_SKY_V1 — O CÉU PASSA A SER PROPRIEDADE DO SKY LIGHT
        //
        //  Antes tudo isto morava no DirectionalLight, e a consequência era
        //  que o céu deixava de existir junto com o sol: apagar a luz levava
        //  embora o `ProceduralSky`, o céu procedural se desligava sozinho e a
        //  cena caía no HDRI do Environment — iluminada, sem sol, sem ninguém
        //  ter pedido.
        //
        //  A divisão certa, que é a da Unreal:
        //    Sky Light        — QUAL é o céu (procedural? que turbidez? que
        //                       nuvens?) e quanta luz ele dá.
        //    Directional Light— ONDE está o sol, que cor tem e quão forte é.
        //
        //  Com isso, uma cena sem Luz Direcional continua tendo céu — só que
        //  um céu SEM SOL, que é noite. É o resultado correto, e é o que
        //  faltava para "apaguei o sol" significar escuro de verdade.
        // ═══════════════════════════════════════════════════════════════════
        // ═══════════════════════════════════════════════════════════════════
        //  SKY_SUN_GATE_V1 — SEM SOL NÃO HÁ DIA
        //
        //  Eu tinha defendido que um HDRI é "fonte de luz própria" e por isso
        //  continuava iluminando sem Luz Direcional. Isso está errado no que
        //  importa: um HDRI de céu diurno é uma FOTOGRAFIA de luz do sol
        //  espalhada pela atmosfera. Apagado o sol, aquela luz não existe mais
        //  — a foto continua existindo, a luz não.
        //
        //  Com isto ligado, TODO o ambiente (IBL e chapado) escala com a
        //  presença do sol, seja a fonte procedural ou HDRI. Apagar a Luz
        //  Direcional escurece a cena, ponto.
        //
        //  A curva satura rápido (ver o lighting pass): sol acima de ~0.5 de
        //  intensidade já dá fator 1, então nenhuma cena iluminada muda de
        //  aparência. Só morde quando o sol está de fato apagado ou quase.
        //
        //  Desligar é para os casos em que a luz do céu REALMENTE não vem do
        //  sol: HDRI de estúdio, interior iluminado só por point lights, cena
        //  noturna com luar. Aí o Sky Light passa a ser autônomo.
        // ═══════════════════════════════════════════════════════════════════
        bool  SunDependent = true;

        bool  ProceduralSky = false;

        float Turbidity = 2.5f;      // 1=limpo, 10=poluído/nublado
        float CloudCoverage = 0.4f;
        float CloudSpeed = 0.015f;
        glm::vec3 CloudColor{ 1.f, 1.f, 1.f };

        // ═══════════════════════════════════════════════════════════════════
        //  CLOUDS_SOFTEN_SUN_V1 — NUVEM MUDA O SOL, NAO SO O CEU
        //
        //  Ate aqui a cobertura de nuvens era so desenho: mudava o ceu na tela
        //  e nao tocava na luz. Fisicamente e o contrario — nuvem e o que mais
        //  muda a luz do dia:
        //
        //    1. ESPALHA a fonte. O sol tem 0.53 grau; num ceu encoberto quem
        //       ilumina e a nuvem inteira, uma fonte de dezenas de graus. E por
        //       isso que em dia nublado a sombra e larga e macia e em dia limpo
        //       e uma linha dura. O PCSS ja sabe fazer isso — so nunca ficou
        //       sabendo das nuvens.
        //
        //    2. TIRA luz DIRETA. Parte vira difusa e volta pelo ceu, que o
        //       Sky Light ja captura.
        //
        //  O SunAngularDegrees da luz NAO e sobrescrito: o valor que o usuario
        //  digitou continua sendo o de CEU LIMPO, e a nuvem so o alarga na hora
        //  de renderizar. Assim, tirar a nuvem devolve exatamente o que ele
        //  tinha, sem ninguem ter reescrito o campo pelas costas.
        // ═══════════════════════════════════════════════════════════════════
        bool  CloudsSoftenSun = true;

        // Cor do céu quando não há sol. É o PISO da atmosfera: não escala com
        // o sol (senão o céu noturno sumiria junto) e é o que sobra quando a
        // Luz Direcional é apagada. Preto puro aqui = escuridão total.
        glm::vec3 NightColor{ 0.005f, 0.005f, 0.02f };

        // ── Ciclo dia/noite ─────────────────────────────────────────────────
        //
        // Mora aqui e não na luz porque é uma propriedade do MUNDO, não da
        // lâmpada: é o relógio da cena. Quando ligado e existe uma Luz
        // Direcional, ele ESCREVE direção, cor e intensidade nela — o sol
        // continua sendo quem ilumina, só não é mais quem decide a hora.
        bool  TimeOfDayEnabled = false;
        float Hour = 12.0f;      // 0=meia-noite, 12=meio-dia
        float DaySpeed = 1.0f;   // 1=tempo real, 60=1min/seg
        float SunLatitude = -23.0f;
    };
}