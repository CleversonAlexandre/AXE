#pragma once

// ═══════════════════════════════════════════════════════════════════════════
//  ICONES DO EDITOR — Font Awesome 6 Free (Solid), subsetada
//
//  Gerado a partir de assets/fonts/axe_icons.ttf. A fonte tem SO os glifos
//  listados aqui: 14 KB em vez dos 426 KB da original. Pra acrescentar um
//  icone e preciso regerar o subset — senao o define existe e o glifo sai
//  como retangulo vazio.
//
//  Os literais estao em UTF-8 escapado byte a byte, e nao como \uXXXX: o MSVC
//  interpreta \u conforme a code page do arquivo-fonte, e o resultado muda
//  conforme quem compila. Escapado, e sempre o mesmo byte.
//
//  Uso:  ImGui::Button(ICON_SAVE "  Salvar")
//        ui::IconButton(ICON_TRASH, "Apagar")
// ═══════════════════════════════════════════════════════════════════════════

#define ICON_MIN_AXE 0xe4e2
#define ICON_MAX_AXE 0xf84c

// ── Arquivo e historico ─────────────────────────────────────────
#define ICON_SAVE         "\xef\x83\x87"   // U+F0C7
#define ICON_UNDO         "\xef\x83\xa2"   // U+F0E2
#define ICON_REDO         "\xef\x80\x9e"   // U+F01E
#define ICON_FOLDER_OPEN  "\xef\x81\xbc"   // U+F07C
#define ICON_FILE         "\xef\x85\x9b"   // U+F15B
#define ICON_TRASH        "\xef\x87\xb8"   // U+F1F8
#define ICON_PLUS         "\xef\x81\xa7"   // U+F067
#define ICON_MINUS        "\xef\x81\xa8"   // U+F068
#define ICON_XMARK        "\xef\x80\x8d"   // U+F00D
#define ICON_CHECK        "\xef\x80\x8c"   // U+F00C
#define ICON_COPY         "\xef\x83\x85"   // U+F0C5
#define ICON_PASTE        "\xef\x83\xaa"   // U+F0EA
#define ICON_CUT          "\xef\x83\x84"   // U+F0C4
#define ICON_CLONE        "\xef\x89\x8d"   // U+F24D

// ── Reproducao ──────────────────────────────────────────────────
#define ICON_PLAY          "\xef\x81\x8b"   // U+F04B
#define ICON_PAUSE         "\xef\x81\x8c"   // U+F04C
#define ICON_STOP          "\xef\x81\x8d"   // U+F04D
#define ICON_FORWARD_STEP  "\xef\x81\x91"   // U+F051

// ── Transform e viewport ────────────────────────────────────────
#define ICON_ARROWS      "\xef\x82\xb2"   // U+F0B2
#define ICON_ROTATE      "\xef\x8b\xb1"   // U+F2F1
#define ICON_EXPAND      "\xef\x81\xa5"   // U+F065
#define ICON_CAMERA      "\xef\x80\xb0"   // U+F030
#define ICON_EYE         "\xef\x81\xae"   // U+F06E
#define ICON_EYE_SLASH   "\xef\x81\xb0"   // U+F070
#define ICON_BORDER_ALL  "\xef\xa1\x8c"   // U+F84C
#define ICON_MAGNET      "\xef\x81\xb6"   // U+F076

// ── Rig e esqueleto ─────────────────────────────────────────────
#define ICON_BONE               "\xef\x97\x97"   // U+F5D7
#define ICON_PERSON             "\xef\x86\x83"   // U+F183
#define ICON_PERSON_RUNNING     "\xef\x9c\x8c"   // U+F70C
#define ICON_SITEMAP            "\xef\x83\xa8"   // U+F0E8
#define ICON_CIRCLE_NODES       "\xee\x93\xa2"   // U+E4E2
#define ICON_DIAGRAM_PROJECT    "\xef\x95\x82"   // U+F542
#define ICON_ARROWS_LEFT_RIGHT  "\xef\x8c\xb7"   // U+F337

// ── Grafo e funcoes ─────────────────────────────────────────────
#define ICON_CODE         "\xef\x84\xa1"   // U+F121
#define ICON_CODE_BRANCH  "\xef\x84\xa6"   // U+F126
#define ICON_FUNCTION     "\xef\x95\x82"   // U+F542
#define ICON_CUBE         "\xef\x86\xb2"   // U+F1B2
#define ICON_LAYER_GROUP  "\xef\x97\xbd"   // U+F5FD
#define ICON_ARROW_LEFT   "\xef\x81\xa0"   // U+F060
#define ICON_ARROW_RIGHT  "\xef\x81\xa1"   // U+F061
#define ICON_ARROW_UP     "\xef\x81\xa2"   // U+F062
#define ICON_ARROW_DOWN   "\xef\x81\xa3"   // U+F063
#define ICON_ROTATE_LEFT  "\xef\x8b\xaa"   // U+F2EA

// ── Estado e aviso ──────────────────────────────────────────────
#define ICON_TRIANGLE_EXCLAMATION  "\xef\x81\xb1"   // U+F071
#define ICON_CIRCLE_INFO           "\xef\x81\x9a"   // U+F05A
#define ICON_BUG                   "\xef\x86\x88"   // U+F188
#define ICON_LOCK                  "\xef\x80\xa3"   // U+F023
#define ICON_UNLOCK                "\xef\x82\x9c"   // U+F09C
#define ICON_LINK                  "\xef\x83\x81"   // U+F0C1
#define ICON_LINK_SLASH            "\xef\x84\xa7"   // U+F127

// ── Paineis ─────────────────────────────────────────────────────
#define ICON_SLIDERS           "\xef\x87\x9e"   // U+F1DE
#define ICON_GEAR              "\xef\x80\x93"   // U+F013
#define ICON_LIST              "\xef\x80\xba"   // U+F03A
#define ICON_TABLE_CELLS       "\xef\x80\x8a"   // U+F00A
#define ICON_MAGNIFYING_GLASS  "\xef\x80\x82"   // U+F002
#define ICON_FILTER            "\xef\x82\xb0"   // U+F0B0
#define ICON_PALETTE           "\xef\x94\xbf"   // U+F53F
#define ICON_IMAGE             "\xef\x80\xbe"   // U+F03E
#define ICON_WAND              "\xef\x9c\xab"   // U+F72B
#define ICON_BOLT              "\xef\x83\xa7"   // U+F0E7
#define ICON_CLOCK             "\xef\x80\x97"   // U+F017
#define ICON_FILM              "\xef\x80\x88"   // U+F008