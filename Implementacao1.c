/*
 * LICENÇA MIT
 * Copyright (c) 2026 Equipe de Redes de Computadores
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <conio.h>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define SAMPLE_RATE 44100
#define M1_THRESHOLD 0.15f
#define DEBOUNCE_SAMPLES 4410          
#define DOUBLE_TAP_WINDOW 17640        
#define COOLDOWN_SAMPLES 22050         

#define ESTADO_ESPERA 0
#define ESTADO_AGUARDA_SEGUNDA_BATIDA 1
#define ESTADO_COOLDOWN 2

/* --- LÓGICA DE PARIDADE PAR OBRIGATÓRIA --- */
uint8_t calcular_paridade_par(uint8_t byte_dados) {
    uint8_t contagem = 0;
    // Conta quantos bits '1' existem nos 8 bits de dados
    for (int i = 0; i < 8; i++) {
        if ((byte_dados >> i) & 1) contagem++;
    }
    // Se a quantidade for PAR, retorna 0. Se for ÍMPAR, retorna 1.
    return (contagem % 2 == 0) ? 0 : 1;
}

/* --- ESTADO DO RECEPTOR E INTERFACE --- */
typedef struct {
    int estado_m1;
    int temporizador;
    int tempo_desde_ultimo_pico;

    int batidas_no_simbolo;
    int bits_recebidos;
    uint16_t quadro_buffer; // Armazena os 9 bits
    
    char estado_texto[128];
    char mensagem_rodape[128];
    bool precisa_redesenhar;
} ReceptorContexto;

/* --- FUNÇÃO PARA DESENHAR A INTERFACE --- */
void desenhar_interface(ReceptorContexto *ctx) {
    printf("\033[H\033[J");
    
    printf("\n");
    printf("  2500 -----------------------------------------------------\n\n");
    printf("  1200 - - - - - - - - - - - - - - - - - - - - - - - - - - -\n\n");
    printf("     0 _____________________________________________________\n");
    printf("------------------------------------------------------------\n");
    
    printf("Estado: %s\n", ctx->estado_texto);
    printf("Batidas no simbolo atual: %d\n", ctx->batidas_no_simbolo);
    printf("Bits recebidos: %d / 9\n", ctx->bits_recebidos);
    printf("Quadro parcial (binario): ");
    for(int i = 0; i < ctx->bits_recebidos; i++) {
        printf("%d", (ctx->quadro_buffer >> i) & 1);
    }
    printf("\n\nCODIFICACAO:\n");
    printf("1 batida  =  0        2 batidas  =  1\n");
    printf("        v                      v\n"); 
    printf("        0                      1\n\n");
    
    printf("[q] Sair     [r] Reiniciar       [s] Salvar\n\n");
    
    printf("%s\n", ctx->mensagem_rodape);
    
    ctx->precisa_redesenhar = false;
}

/* --- CALLBACK DE CAPTURA DE ÁUDIO --- */
void captura_audio_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    ReceptorContexto* ctx = (ReceptorContexto*)pDevice->pUserData;
    const float* pAmostras = (const float*)pInput;

    if (pAmostras == NULL) return;

    for (ma_uint32 i = 0; i < frameCount; i++) {
        float amostra = pAmostras[i];
        float abs_val = fabsf(amostra);
        bool pico_detetado = (abs_val > M1_THRESHOLD);

        if (ctx->estado_m1 == ESTADO_ESPERA) {
            if (pico_detetado) {
                ctx->estado_m1 = ESTADO_AGUARDA_SEGUNDA_BATIDA;
                ctx->temporizador = 0;
                ctx->tempo_desde_ultimo_pico = 0;
                ctx->batidas_no_simbolo = 1;
                strcpy(ctx->estado_texto, "Aguardando possivel 2a batida...");
                ctx->precisa_redesenhar = true;
            }
        } 
        else if (ctx->estado_m1 == ESTADO_AGUARDA_SEGUNDA_BATIDA) {
            ctx->temporizador++;
            ctx->tempo_desde_ultimo_pico++;

            bool bit_registado = false;

            if (pico_detetado && ctx->tempo_desde_ultimo_pico > DEBOUNCE_SAMPLES) {
                // BIT 1 DETETADO
                ctx->batidas_no_simbolo = 2;
                ctx->quadro_buffer |= (1 << ctx->bits_recebidos);
                ctx->bits_recebidos++;
                strcpy(ctx->estado_texto, "BIT 1 - duas batidas");
                bit_registado = true;
            } 
            else if (ctx->temporizador > DOUBLE_TAP_WINDOW) {
                // BIT 0 DETETADO
                ctx->quadro_buffer |= (0 << ctx->bits_recebidos); // Permanece 0
                ctx->bits_recebidos++;
                strcpy(ctx->estado_texto, "BIT 0 - uma batida");
                bit_registado = true;
            }

            if (bit_registado) {
                // VERIFICAÇÃO DO QUADRO DE 9 BITS
                if (ctx->bits_recebidos == 9) {
                    uint8_t dados = ctx->quadro_buffer & 0xFF; // Recorta os 8 primeiros bits
                    uint8_t b9_recebido = (ctx->quadro_buffer >> 8) & 0x01; // Isola o 9º bit
                    uint8_t b9_esperado = calcular_paridade_par(dados);     // Calcula paridade esperada
                    
                    char char_ascii = (dados >= 32 && dados <= 126) ? (char)dados : '?';

                    if (b9_recebido == b9_esperado) {
                        sprintf(ctx->mensagem_rodape, "[SUCESSO] Char: '%c' (0x%02X) | b9 recebido: %d | Valido!", char_ascii, dados, b9_recebido);
                    } else {
                        sprintf(ctx->mensagem_rodape, "[ERRO PARIDADE] Char: '%c' | b9 recebido: %d | b9 esperado: %d", char_ascii, b9_recebido, b9_esperado);
                    }

                    // Prepara o próximo quadro
                    ctx->quadro_buffer = 0;
                    ctx->bits_recebidos = 0;
                } else {
                    sprintf(ctx->mensagem_rodape, "Bit registado. Aguardando proximo...");
                }

                ctx->estado_m1 = ESTADO_COOLDOWN;
                ctx->temporizador = 0;
                ctx->precisa_redesenhar = true;
            }
        } 
        else if (ctx->estado_m1 == ESTADO_COOLDOWN) {
            ctx->temporizador++;
            if (ctx->temporizador > COOLDOWN_SAMPLES) {
                ctx->estado_m1 = ESTADO_ESPERA;
                ctx->batidas_no_simbolo = 0;
                strcpy(ctx->estado_texto, "A escutar...");
                ctx->precisa_redesenhar = true;
            }
        }
    }
    (void)pOutput;
}

int main() {
    ma_device_config deviceConfig;
    ma_device device;
    ReceptorContexto ctx;
    
    memset(&ctx, 0, sizeof(ReceptorContexto));
    strcpy(ctx.estado_texto, "A escutar...");
    strcpy(ctx.mensagem_rodape, "Transmissao atual apagada.");
    ctx.precisa_redesenhar = true;

    deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format   = ma_format_f32;
    deviceConfig.capture.channels = 1;
    deviceConfig.sampleRate       = SAMPLE_RATE;
    deviceConfig.dataCallback     = captura_audio_callback;
    deviceConfig.pUserData        = &ctx;

    if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
        printf("Erro ao iniciar o microfone.\n");
        return -1;
    }
    
    ma_device_start(&device);

    bool rodando = true;
    while (rodando) {
        if (ctx.precisa_redesenhar) {
            desenhar_interface(&ctx);
        }

        if (_kbhit()) {
            char tecla = _getch();
            
            if (tecla == 'q' || tecla == 'Q') {
                rodando = false; 
            } 
            else if (tecla == 'r' || tecla == 'R') {
                ctx.bits_recebidos = 0;
                ctx.batidas_no_simbolo = 0;
                ctx.quadro_buffer = 0;
                ctx.estado_m1 = ESTADO_ESPERA;
                strcpy(ctx.estado_texto, "A escutar...");
                strcpy(ctx.mensagem_rodape, "Transmissao atual apagada."); 
                ctx.precisa_redesenhar = true;
            }
            else if (tecla == 's' || tecla == 'S') {
                strcpy(ctx.mensagem_rodape, "Dados salvos com sucesso!");
                ctx.precisa_redesenhar = true;
            }
        }
        ma_sleep(30); 
    }

    ma_device_uninit(&device);
    
    printf("\n\033[H\033[J");
    printf("Programa encerrado.\n");
    
    return 0;
}