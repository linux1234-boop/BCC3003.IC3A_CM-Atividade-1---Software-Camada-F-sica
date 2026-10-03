/*
 * LICENÇA MIT
 * MÓDULO MÉTODO 2: BATIDAS (Silêncio + Batida + Silêncio) + Soma dos Bits (17 Bits)
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

/* --- PARÂMETROS DO RECETOR --- */
#define AUDIO_THRESHOLD 0.30f           // Sensibilidade ajustada para estabilidade
#define SILENCIO_PREVIO_SAMPLES 8820    // 200ms de silêncio obrigatório antes
#define JANELA_BATIDAS_SAMPLES 17640    // 400ms para dar a 1ª e 2ª batidas
#define DEBOUNCE_SAMPLES 4410           // 100ms entre batidas para não contar em duplicado
#define SILENCIO_POSTERIOR_SAMPLES 8820 // 200ms de silêncio obrigatório depois

/* --- PARÂMETROS DO EMISSOR --- */
#define FREQ_BEEP 1500.0f
#define BEEP_DURACAO_SAMPLES 2205       // 50ms (simula o impacto de uma batida)

#define ESTADO_SILENCIO_PREVIO 0
#define ESTADO_ESPERA_BATIDA 1
#define ESTADO_JANELA_BATIDAS 2
#define ESTADO_SILENCIO_POSTERIOR 3

/* --- DETEÇÃO DE ERROS: SOMA DOS BITS --- */
uint8_t calcular_soma_bits(uint8_t byte_dados) {
    uint8_t soma = 0;
    for (int i = 0; i < 8; i++) {
        if ((byte_dados >> i) & 1) soma++;
    }
    return soma;
}

/* --- ESTADO DO RECETOR --- */
typedef struct {
    int estado;
    int temporizador;
    int tempo_desde_ultimo_pico;

    int batidas_no_simbolo;
    int bits_recebidos;
    uint32_t quadro_buffer; 
    
    char interface_estado[128];
    char interface_rodape[128];
    bool precisa_redesenhar;
} ReceptorBatidas;

/* --- INTERFACE DO TERMINAL --- */
void desenhar_interface(ReceptorBatidas *ctx) {
    printf("\033[H\033[J");
    printf("============================================================\n");
    printf("   CAMADA FISICA - METODO 2 (Batidas Exatas + Soma de Bits)\n");
    printf("============================================================\n\n");
    
    printf("Estado Atual: %s\n", ctx->interface_estado);
    printf("Progresso do Quadro (17 bits): %d / 17\n", ctx->bits_recebidos);
    printf("Batidas registadas no bit atual: %d\n\n", ctx->batidas_no_simbolo);
    
    printf("Bits desmodulados: ");
    for(int i = 0; i < ctx->bits_recebidos; i++) {
        printf("%d", (ctx->quadro_buffer >> i) & 1);
    }
    printf("\n\n");
    printf("Codificacao:\n");
    printf("Bit 0 = Silencio + 1 Batida  + Silencio\n");
    printf("Bit 1 = Silencio + 2 Batidas + Silencio\n");
    printf("Frame = [1 Start Bit] + [8 Bits Dados] + [8 Bits Soma]\n\n");
    printf("[q] Sair do Receptor     [r] Reiniciar\n\n");
    printf("-> %s\n", ctx->interface_rodape);
    
    ctx->precisa_redesenhar = false;
}

/* --- CALLBACK DO MICROFONE --- */
void captura_batidas_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    ReceptorBatidas* ctx = (ReceptorBatidas*)pDevice->pUserData;
    const float* pAmostras = (const float*)pInput;
    if (pAmostras == NULL) return;

    for (ma_uint32 i = 0; i < frameCount; i++) {
        float amostra = pAmostras[i];
        bool pico_detetado = (fabsf(amostra) > AUDIO_THRESHOLD);

        // ESTADO 0: Exige silêncio
        if (ctx->estado == ESTADO_SILENCIO_PREVIO) {
            if (pico_detetado) {
                ctx->temporizador = 0; 
            } else {
                ctx->temporizador++;
                if (ctx->temporizador > SILENCIO_PREVIO_SAMPLES) {
                    ctx->estado = ESTADO_ESPERA_BATIDA;
                    strcpy(ctx->interface_estado, "Silencio validado. Aguardar batida...");
                    ctx->precisa_redesenhar = true;
                }
            }
        } 
        // ESTADO 1: Aguarda a 1ª batida
        else if (ctx->estado == ESTADO_ESPERA_BATIDA) {
            if (pico_detetado) {
                ctx->estado = ESTADO_JANELA_BATIDAS;
                ctx->batidas_no_simbolo = 1;
                ctx->temporizador = 0;
                ctx->tempo_desde_ultimo_pico = 0;
                strcpy(ctx->interface_estado, "1a Batida! Aguardar 2a ou fecho da janela...");
                ctx->precisa_redesenhar = true;
            }
        } 
        // ESTADO 2: Janela aberta (400ms)
        else if (ctx->estado == ESTADO_JANELA_BATIDAS) {
            ctx->temporizador++;
            ctx->tempo_desde_ultimo_pico++;

            if (pico_detetado && ctx->tempo_desde_ultimo_pico > DEBOUNCE_SAMPLES) {
                ctx->batidas_no_simbolo++;
                ctx->tempo_desde_ultimo_pico = 0;
                sprintf(ctx->interface_estado, "%d Batidas registadas!", ctx->batidas_no_simbolo);
                ctx->precisa_redesenhar = true;
            }

            if (ctx->temporizador > JANELA_BATIDAS_SAMPLES) {
                ctx->estado = ESTADO_SILENCIO_POSTERIOR;
                ctx->temporizador = 0;
                strcpy(ctx->interface_estado, "Janela fechada. A validar silencio posterior...");
                ctx->precisa_redesenhar = true;
            }
        } 
        // ESTADO 3: Valida silêncio após batidas
        else if (ctx->estado == ESTADO_SILENCIO_POSTERIOR) {
            if (pico_detetado) {
                ctx->estado = ESTADO_SILENCIO_PREVIO;
                ctx->temporizador = 0;
                strcpy(ctx->interface_rodape, "\033[0;31m[ERRO]\033[0m Ruido no silencio posterior. Bit anulado.");
                ctx->precisa_redesenhar = true;
            } else {
                ctx->temporizador++;
                if (ctx->temporizador > SILENCIO_POSTERIOR_SAMPLES) {
                    bool bit_valido = false;
                    
                    if (ctx->batidas_no_simbolo == 1) {
                        ctx->quadro_buffer |= (0 << ctx->bits_recebidos);
                        bit_valido = true;
                    } else if (ctx->batidas_no_simbolo == 2) {
                        ctx->quadro_buffer |= (1 << ctx->bits_recebidos);
                        bit_valido = true;
                    }

                    if (bit_valido) {
                        ctx->bits_recebidos++;
                        sprintf(ctx->interface_rodape, "Bit %d validado com sucesso.", (ctx->batidas_no_simbolo == 2) ? 1 : 0);
                        
                        // FINAL DO QUADRO DE 17 BITS
                        if (ctx->bits_recebidos == 17) {
                            uint8_t start_bit = ctx->quadro_buffer & 0x01;
                            uint8_t dados = (ctx->quadro_buffer >> 1) & 0xFF;
                            uint8_t soma_recebida = (ctx->quadro_buffer >> 9) & 0xFF;
                            
                            uint8_t soma_calculada = calcular_soma_bits(dados);
                            char char_ascii = (dados >= 32 && dados <= 126) ? (char)dados : '?';

                            if (start_bit == 1 && soma_recebida == soma_calculada) {
                                sprintf(ctx->interface_rodape, "\033[0;32m[SUCESSO]\033[0m Char: '%c' | Soma Rx: %d == Calc: %d", char_ascii, soma_recebida, soma_calculada);
                            } else {
                                sprintf(ctx->interface_rodape, "\033[0;31m[FALHA]\033[0m Char: '%c' | Start: %d | Soma Rx: %d != Calc: %d", char_ascii, start_bit, soma_recebida, soma_calculada);
                            }
                            ctx->quadro_buffer = 0;
                            ctx->bits_recebidos = 0;
                        }
                    }

                    ctx->estado = ESTADO_SILENCIO_PREVIO;
                    ctx->temporizador = 0;
                    ctx->precisa_redesenhar = true;
                }
            }
        }
    }
    (void)pOutput;
}

/* --- MÓDULO EMISSOR (GERA AS BATIDAS AUTOMATICAMENTE) --- */
void transmitir_caractere_batidas(char caractere) {
    uint8_t dados = (uint8_t)caractere;
    uint8_t soma = calcular_soma_bits(dados);
    uint32_t quadro = 1 | (dados << 1) | (soma << 9);
    
    // O Emissor dá 250ms de silêncio para garantir que o Recetor passa dos 200ms
    int tx_silencio_previo = 11025; // 250ms
    int tx_janela = 17640;          // 400ms
    int tx_silencio_post = 11025;   // 250ms
    int samples_por_bit = tx_silencio_previo + tx_janela + tx_silencio_post;
    
    size_t total_samples = 17 * samples_por_bit;
    float* pcm_buffer = (float*)calloc(total_samples, sizeof(float));
    
    float phase = 0.0f;
    int idx = 0;
    
    for (int b = 0; b < 17; b++) {
        int bit = (quadro >> b) & 1;
        
        // 1. Silêncio Prévio
        for (int s = 0; s < tx_silencio_previo; s++) {
            pcm_buffer[idx++] = 0.0f;
        }
        
        // 2. Janela de Batidas
        for (int s = 0; s < tx_janela; s++) {
            bool tocar_beep = false;
            
            if (s < BEEP_DURACAO_SAMPLES) tocar_beep = true; // 1a batida no inicio da janela
            if (bit == 1 && s >= 6615 && s < (6615 + BEEP_DURACAO_SAMPLES)) tocar_beep = true; // 2a batida aos 150ms
            
            if (tocar_beep) {
                pcm_buffer[idx++] = sinf(phase) * 0.8f; 
                phase += 2.0f * PI * FREQ_BEEP / SAMPLE_RATE;
                if (phase > 2.0f * PI) phase -= 2.0f * PI;
            } else {
                pcm_buffer[idx++] = 0.0f; 
            }
        }
        
        // 3. Silêncio Posterior
        for (int s = 0; s < tx_silencio_post; s++) {
            pcm_buffer[idx++] = 0.0f;
        }
    }

    ma_engine engine;
    ma_engine_init(NULL, &engine);
    
    ma_audio_buffer_config bufConfig = ma_audio_buffer_config_init(ma_format_f32, 1, total_samples, pcm_buffer, NULL);
    bufConfig.sampleRate = SAMPLE_RATE;
    ma_audio_buffer audioBuffer;
    ma_audio_buffer_init(&bufConfig, &audioBuffer);
    
    ma_sound sound;
    ma_sound_init_from_data_source(&engine, &audioBuffer, 0, NULL, &sound);
    ma_sound_start(&sound);

    printf("A transmitir caractere '%c' por Batidas... Aguarde 15 seg.\n", caractere);
    while (ma_sound_is_playing(&sound)) ma_sleep(100);

    ma_sound_uninit(&sound);
    ma_audio_buffer_uninit(&audioBuffer);
    ma_engine_uninit(&engine);
    free(pcm_buffer);
}

/* --- MENU PRINCIPAL --- */
int main() {
    int opcao = -1;
    while (opcao != 0) {
        printf("\n\033[H\033[J");
        printf("============================================================\n");
        printf("             SISTEMA DE COMUNICACAO - METODO 2              \n");
        printf("============================================================\n");
        printf("1. [EMISSOR] Transmitir Caractere via Batidas (Auto)\n");
        printf("2. [RECEPTOR] Escutar Microfone em Tempo Real\n");
        printf("0. Sair\n");
        printf("Escolha: ");
        
        if (scanf("%d", &opcao) != 1) break;
        getchar(); 

        if (opcao == 1) {
            printf("\nDigite um unico caractere para transmitir: ");
            char c = getchar();
            transmitir_caractere_batidas(c);
            printf("Transmissao concluida! Prima ENTER para voltar.");
            getchar(); getchar();
        } 
        else if (opcao == 2) {
            ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
            ma_device device;
            ReceptorBatidas ctx;
            memset(&ctx, 0, sizeof(ReceptorBatidas));
            
            ctx.estado = ESTADO_SILENCIO_PREVIO;
            strcpy(ctx.interface_estado, "A verificar silencio inicial...");
            strcpy(ctx.interface_rodape, "Pronto para receber dados.");
            ctx.precisa_redesenhar = true;

            deviceConfig.capture.format = ma_format_f32;
            deviceConfig.capture.channels = 1;
            deviceConfig.sampleRate = SAMPLE_RATE;
            deviceConfig.dataCallback = captura_batidas_callback;
            deviceConfig.pUserData = &ctx;

            ma_device_init(NULL, &deviceConfig, &device);
            ma_device_start(&device);

            bool rodando = true;
            while (rodando) {
                if (ctx.precisa_redesenhar) desenhar_interface(&ctx);
                if (_kbhit()) {
                    char tecla = _getch();
                    if (tecla == 'q' || tecla == 'Q') rodando = false;
                    else if (tecla == 'r' || tecla == 'R') {
                        ctx.bits_recebidos = 0;
                        ctx.batidas_no_simbolo = 0;
                        ctx.quadro_buffer = 0;
                        ctx.estado = ESTADO_SILENCIO_PREVIO;
                        strcpy(ctx.interface_estado, "A verificar silencio inicial...");
                        strcpy(ctx.interface_rodape, "Transmissao reiniciada."); 
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