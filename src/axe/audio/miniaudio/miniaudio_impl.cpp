// TU dedicada a implementacao do miniaudio — mesmo padrao de
// src/axe/stb_image_impl.cpp: a lib de header unico e compilada UMA vez,
// isolada, e o resto do backend so inclui as declaracoes.
//
// Separar assim nao e cosmetico: miniaudio.h com MINIAUDIO_IMPLEMENTATION
// e uma unidade de compilacao pesada. Mante-la sozinha significa que mexer
// no MiniAudioDevice recompila o device, nao a lib inteira.

// Nenhum MA_NO_* aqui de proposito: qualquer define de configuracao do
// miniaudio precisa ser IDENTICO em toda TU que inclui o header, senao as
// declaracoes divergem entre este arquivo e miniaudio_device.cpp. Se um dia
// for preciso desligar modulos (encoding, backends), o lugar certo e um
// header de config incluido pelos dois — nao um define solto aqui.

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"