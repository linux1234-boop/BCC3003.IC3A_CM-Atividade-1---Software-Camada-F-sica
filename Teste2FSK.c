/*
 * MÓDULO MÉTODO 2: FSK (Frequência) + Goertzel + Assíncrono (Som/Silêncio)
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
#define PI 3.14159265358979323846f

/* --- PARÂMETROS FSK --- */
#define FREQ_0 1200.0f
#define FREQ_1 2200.0f

#define TONE_DURATION_MS 60
#define SILENCE_DURATION_MS 60
#define TONE_SAMPLES ((SAMPLE_RATE * TONE_DURATION_MS) / 1000)
#define SILENCE_SAMPLES ((SAMPLE_RATE * SILENCE_DURATION_MS) / 1000)

/* Parametros do Recetor / Goertzel */
#define TAMANHO_BLOCO_MS 20
#define TAMANHO_BLOCO_SAMPLES ((SAMPLE_RATE * TAMANHO_BLOCO_MS) / 1000)

#define TONE_THRESHOLD_HIGH 2.0f
#define TONE_THRESHOLD_LOW  0.5f

#define ESTADO_ESPERA_TOM 0
#define ESTADO_ESPERA_SILENCIO 1

/* --- MATEMÁTICA: ALGORITMO DE GOERTZEL --- */
float goertzel_mag(const float* amostras, int num_amostras, float freq_alvo) {
    int k = (int)(0.5 + ((num_amostras * freq_alvo) / SAMPLE_RATE));
    float omega = (2.0f * PI * k) / num_amostras;
    float seno = sinf(omega);
    float cosseno = cosf(omega);
    float coeff = 2.0f * cosseno;
    float q0 = 0, q1 = 0, q2 = 0;

    for (int i = 0; i < num_amostras; i++) {
        q0 = coeff * q1 - q2 + amostras[i];
        q2 = q1;
        q1 = q0;
    }
    float magnitude = sqrtf(q1 * q1 + q2 * q2 - q1 * q2 * coeff);
    return magnitude;
}

/* --- DETEÇÃO DE ERROS: SOMA DOS BITS --- */
uint8_t calcular_soma_bits(uint8_t byte_dados) {
    uint8_t soma = 0;
    for (int i = 0; i < 8; i++) {
        if ((byte_dados >> i) & 1) soma++;
    }
    return soma;
}

/* =========================================================================
   EMISSOR (TX) - Geração de Som
   ========================================================================= */
typedef struct {
    float* buffer;
    int total_samples;
    int cursor;
} EmissorContexto;

void emissor_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    EmissorContexto* ctx = (EmissorContexto*)pDevice->pUserData;
    float* pOut = (float*)pOutput;
    for (ma_uint32 i = 0; i < frameCount; i++) {
        if (ctx->cursor < ctx->total_samples) {
            pOut[i] = ctx->buffer[ctx->cursor++];
        } else {
            pOut[i] = 0.0f;
        }
    }
    (void)pInput;
}

void transmitir_caractere_fsk(char c) {
    uint8_t dados = (uint8_t)c;
    uint8_t soma = calcular_soma_bits(dados);
    
    // Constrói o quadro: 1 (Start) + 8 (Dados) + 8 (Soma) = 17 bits no total
    uint32_t quadro = 0;
    quadro |= 1;                     // Start bit (Sempre 1)
    quadro |= (dados << 1);          // Coloca os dados
    quadro |= (soma << 9);           // Coloca a assinatura (Soma) no final

    int total_bits = 17;
    int amostras_por_bit = TONE_SAMPLES + SILENCE_SAMPLES;
    int total_amostras = total_bits * amostras_por_bit;
    
    float* buffer_tx = (float*)malloc(total_amostras * sizeof(float));
    int idx = 0;

    // Gera a onda sonora bit a bit
    for (int b = 0; b < total_bits; b++) {
        int bit = (quadro >> b) & 1;
        float freq = (bit == 1) ? FREQ_1 : FREQ_0;
        
        // 1. Gera o Som (60ms)
        for (int i = 0; i < TONE_SAMPLES; i++) {
            float tempo = (float)i / SAMPLE_RATE;
            buffer_tx[idx++] = 0.5f * sinf(2.0f * PI * freq * tempo);
        }
        // 2. Gera o Silêncio separador (60ms)
        for (int i = 0; i < SILENCE_SAMPLES; i++) {
            buffer_tx[idx++] = 0.0f;
        }
    }

    EmissorContexto ctx;
    ctx.buffer = buffer_tx;
    ctx.total_samples = total_amostras;
    ctx.cursor = 0;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 1;
    config.sampleRate = SAMPLE_RATE;
    config.dataCallback = emissor_callback;
    config.pUserData = &ctx;

    ma_device device;
    if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
        printf("Erro ao iniciar alto-falante.\n");
        free(buffer_tx);
        return;
    }

    ma_device_start(&device);
    printf("\nA Transmitir: Letra '%c' (Dados: %d, Soma: %d)\n", c, dados, soma);
    
    // Aguarda o som acabar de tocar
    while (ctx.cursor < ctx.total_samples) {
        ma_sleep(10);
    }
    ma_sleep(100); // pequena pausa de segurança
    
    ma_device_uninit(&device);
    free(buffer_tx);
}


/* =========================================================================
   RECETOR (RX) - Captura de Som e Máquina de Estados
   ========================================================================= */
typedef struct {
    int estado;
    int bits_recebidos;
    uint32_t quadro_buffer;
    
    float bloco_analise[TAMANHO_BLOCO_SAMPLES];
    int index_bloco;
    
    char interface_estado[128];
    char interface_rodape[128];
    bool precisa_redesenhar;
} ReceptorFSK;

void desenhar_interface(ReceptorFSK *ctx) {
    printf("\033[H\033[J");
    printf("============================================================\n");
    printf("   CAMADA FISICA - METODO 2 (FSK + Goertzel + Assincrono)\n");
    printf("============================================================\n\n");
    
    printf("Estado Atual: %s\n", ctx->interface_estado);
    printf("Progresso do Quadro: %d / 17 bits\n\n", ctx->bits_recebidos);
    
    printf("Bits recebidos no ar: ");
    for(int i = 0; i < ctx->bits_recebidos; i++) {
        printf("%d", (ctx->quadro_buffer >> i) & 1);
    }
    printf("\n\n");
    printf("Codificacao:\n");
    printf("Bit 0 = 1200 Hz (60ms) -> Silencio (60ms)\n");
    printf("Bit 1 = 2200 Hz (60ms) -> Silencio (60ms)\n");
    printf("Frame = [1 Start Bit] + [8 Bits Dados] + [8 Bits Soma]\n\n");
    printf("[q] Sair do Receptor     [r] Reiniciar\n\n");
    printf("-> %s\n", ctx->interface_rodape);
    
    ctx->precisa_redesenhar = false;
}

void captura_fsk_continuo_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    ReceptorFSK* ctx = (ReceptorFSK*)pDevice->pUserData;
    const float* pAmostras = (const float*)pInput;
    if (pAmostras == NULL) return;

    for (ma_uint32 i = 0; i < frameCount; i++) {
        ctx->bloco_analise[ctx->index_bloco++] = pAmostras[i];
        
        // Quando enche um bloco de 20ms, analisa com Goertzel
        if (ctx->index_bloco >= TAMANHO_BLOCO_SAMPLES) {
            float mag0 = goertzel_mag(ctx->bloco_analise, TAMANHO_BLOCO_SAMPLES, FREQ_0);
            float mag1 = goertzel_mag(ctx->bloco_analise, TAMANHO_BLOCO_SAMPLES, FREQ_1);
            
            // ESTADO 0: À procura de som (Tom)
            if (ctx->estado == ESTADO_ESPERA_TOM) {
                if (mag0 > TONE_THRESHOLD_HIGH || mag1 > TONE_THRESHOLD_HIGH) {
                    int bit_lido = (mag1 > mag0) ? 1 : 0;
                    
                    ctx->quadro_buffer |= (bit_lido << ctx->bits_recebidos);
                    ctx->bits_recebidos++;
                    ctx->estado = ESTADO_ESPERA_SILENCIO; // Avança para Estado 1
                    
                    sprintf(ctx->interface_estado, "Som detetado (Bit %d)! A aguardar silencio...", bit_lido);
                    ctx->precisa_redesenhar = true;
                    
                    // Se recebemos os 17 bits completos, vamos validar
                    if (ctx->bits_recebidos == 17) {
                        uint8_t start_bit = ctx->quadro_buffer & 0x01;
                        uint8_t dados = (ctx->quadro_buffer >> 1) & 0xFF;
                        uint8_t soma_recebida = (ctx->quadro_buffer >> 9) & 0xFF;
                        
                        uint8_t soma_calculada = calcular_soma_bits(dados);
                        char char_ascii = (dados >= 32 && dados <= 126) ? (char)dados : '?';

                        if (start_bit == 1 && soma_recebida == soma_calculada) {
                            sprintf(ctx->interface_rodape, "\033[0;32m[SUCESSO]\033[0m Char: '%c' | Soma Rx: %d == Calc: %d", char_ascii, soma_recebida, soma_calculada);
                        } else {
                            sprintf(ctx->interface_rodape, "\033[0;31m[FALHA]\033[0m Char: '%c' | Soma Rx: %d != Calc: %d", char_ascii, soma_recebida, soma_calculada);
                        }
                        
                        ctx->quadro_buffer = 0;
                        ctx->bits_recebidos = 0;
                        ctx->estado = ESTADO_ESPERA_TOM;
                        strcpy(ctx->interface_estado, "A aguardar START BIT...");
                    }
                }
            } 
            // ESTADO 1: À procura de Silêncio
            else if (ctx->estado == ESTADO_ESPERA_SILENCIO) {
                if (mag0 < TONE_THRESHOLD_LOW && mag1 < TONE_THRESHOLD_LOW) {
                    ctx->estado = ESTADO_ESPERA_TOM; // Regressa ao Estado 0
                    strcpy(ctx->interface_estado, "Silencio validado! Pronto para proximo som...");
                    ctx->precisa_redesenhar = true;
                }
            }
            
            ctx->index_bloco = 0; // Prepara para a próxima análise de 20ms
        }
    }
    (void)pOutput;
}


/* =========================================================================
   MENU PRINCIPAL
   ========================================================================= */
int main() {
    int opcao = -1;
    while (opcao != 0) {
        printf("\n\033[H\033[J");
        printf("============================================================\n");
        printf("             SISTEMA DE COMUNICACAO - METODO 2              \n");
        printf("               (FSK + Goertzel + Assincrono)                \n");
        printf("============================================================\n");
        printf("1. [EMISSOR] Transmitir Caractere via Som\n");
        printf("2. [RECEPTOR] Escutar Microfone em Tempo Real\n");
        printf("0. Sair\n");
        printf("Escolha: ");
        
        if (scanf("%d", &opcao) != 1) break;
        getchar(); 

        if (opcao == 1) {
            printf("\nDigite um unico caractere para transmitir: ");
            char c = getchar();
            transmitir_caractere_fsk(c);
            printf("Prima ENTER para voltar ao menu.");
            getchar(); getchar();
        } 
        else if (opcao == 2) {
            ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
            ma_device device;
            ReceptorFSK ctx;
            memset(&ctx, 0, sizeof(ReceptorFSK));
            
            ctx.estado = ESTADO_ESPERA_TOM;
            strcpy(ctx.interface_estado, "A aguardar START BIT...");
            strcpy(ctx.interface_rodape, "Microfone ligado. Pronto para receber.");
            ctx.precisa_redesenhar = true;

            deviceConfig.capture.format = ma_format_f32;
            deviceConfig.capture.channels = 1;
            deviceConfig.sampleRate = SAMPLE_RATE;
            deviceConfig.dataCallback = captura_fsk_continuo_callback;
            deviceConfig.pUserData = &ctx;

            if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
                printf("Erro: O microfone não está disponível!\n");
                continue;
            }
            ma_device_start(&device);

            bool rodando = true;
            while (rodando) {
                if (ctx.precisa_redesenhar) desenhar_interface(&ctx);
                
                if (_kbhit()) {
                    char tecla = _getch();
                    if (tecla == 'q' || tecla == 'Q') rodando = false;
                    else if (tecla == 'r' || tecla == 'R') {
                        ctx.bits_recebidos = 0;
                        ctx.quadro_buffer = 0;
                        ctx.estado = ESTADO_ESPERA_TOM;
                        ctx.index_bloco = 0;
                        strcpy(ctx.interface_estado, "A aguardar START BIT...");
                        strcpy(ctx.interface_rodape, "Reset manual feito. Pronto para receber."); 
                        ctx.precisa_redesenhar = true;
                    }
                }
                ma_sleep(30);
            }
            ma_device_uninit(&device);
        }
    }
    return 0;
}
