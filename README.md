# BCC3003.IC3A_CM-Atividade-1---Software-Camada-Fisica
A Camada Física do modelo ISO/OSI tem como responsabilidade primária a transmissão de bits brutos ( raw bits ) sobre um meio de comunicação de dados. Nesta atividade você deve implementar e analisar um sistema completo de comunicação digital que utiliza o meio acústico ondas sonoras no ar para transmitir informações binárias entre dispositivos.

1. Fundamentação Teórica
Modelo ISO/OSI (Visão Geral)
O modelo de referência OSI (Open Systems Interconnection) divide as redes de computadores em 7 camadas lógicas:

Física: Transmissão bruta de bits pelo meio físico.

Enlace de Dados: Detecção de erros e controle de acesso ao meio (MAC).

Rede: Roteamento de pacotes (IP).

Transporte: Entrega confiável de ponta a ponta (TCP/UDP).

Sessão: Estabelecimento e manutenção de conexões.

Apresentação: Formatação, criptografia e compressão de dados.

Aplicação: Interface com os softwares do usuário final (HTTP, FTP).

#A Camada Física e seus Conceitos

A Camada Física é responsável pela tradução de dados lógicos (0s e 1s) em sinais físicos adequados ao meio de transmissão — neste projeto, o som (ondas mecânicas propagadas no ar).

Sinais Analógicos vs. Digitais: O computador processa sinais digitais (níveis discretos), mas o som propagado no ar é analógico (contínuo no tempo e na amplitude). O DAC (Digital-to-Analog Converter) da placa de som emite o som, e o ADC (Analog-to-Digital Converter) do microfone captura o sinal, amostrando-o.

Taxa de Amostragem (Sample Rate): Define quantas vezes por segundo o sinal analógico é lido. Utilizamos 44.100 Hz, o padrão da qualidade de CD, garantindo precisão para recriar as ondas de áudio.

Modulação: É o processo de alterar uma onda portadora para embutir informação. Ao variar a amplitude, temos o ASK (Amplitude-Shift Keying); ao variar a frequência, o FSK (Frequency-Shift Keying).

Ruído: Qualquer sinal indesejado (vozes, vento, ventoinhas) que se sobrepõe ao sinal original, causando degradação e erros de leitura.

Detecção de Erros
Paridade Par (Método 1): Consiste em adicionar um bit extra ao byte de dados. Conta-se o número de bits "1" nos dados; se for ímpar, o bit de paridade é "1" (para tornar a soma total par); se for par, é "0". O recetor verifica se a contagem total de "1s" recebida é par. Se for ímpar, houve corrupção no quadro.

Soma de Bits (Método 2): Técnica mais abrangente onde o emissor conta quantos bits "1" existem no byte de dados e envia esse valor exato (em formato de 8 bits) como assinatura no final do quadro. O recetor refaz a contagem e compara com a soma recebida.

2. Engenharia e Arquitetura das Soluções
Método 1: Modulação por Amplitude 
Este método utiliza limiares de volume (Amplitude) para distinguir a presença ou ausência de sinal.

Arquitetura: O transmissor emite sons de volume alto para o Bit 1 e silêncio (ou som baixo) para o Bit 0, baseando-se em janelas de tempo fixas (sincronismo rígido).

Ajustes Práticos: O código foi calibrado com thresholds (limiares de amplitude) específicos. O recetor escuta o ambiente e, ao detetar um pico de volume que ultrapasse o limiar configurado (ex: 0.30f), assume que é o "Start Bit" e começa a amostrar o áudio em intervalos de tempo matematicamente exatos.

Vulnerabilidades: Ruídos ambientes altos no momento exato da leitura de um Bit 0 podem ser erroneamente interpretados como Bit 1, causando erros apontados pela verificação de Paridade.

Método 2: Modulação FSK Assíncrona e Contínua
Criado para garantir maior confiabilidade e imunidade a ruídos, abandonando a medição por volume.

Técnica: FSK (Frequency-Shift Keying). Utilizamos 1200 Hz para o Bit 0 e 2200 Hz para o Bit 1.

Arquitetura Tom-a-Tom (Assíncrona): O transmissor emite um "beep" de 60ms seguido de 60ms de silêncio obrigatório. Em vez de uma janela de tempo rígida (que pode causar perda de sincronismo se o sistema operativo atrasar), o recetor analisa continuamente o áudio em blocos de 20ms usando o Algoritmo de Goertzel (um filtro digital altamente eficiente para detetar frequências isoladas).

Quadro de Dados: 1 Start Bit + 8 Bits de Dados + 8 Bits de Soma = 17 bits.

Taxa de Transmissão (bps): Como cada bit exige 120ms (60ms beep + 60ms silêncio), o quadro de 17 bits demora ~2,04 segundos.

Velocidade Teórica: ~8,33 bps.

Velocidade Prática: Mantém-se nos ~8 bps com altíssima precisão. É mais lento que a rede tradicional, mas extremamente rápido e estável para transmissão acústica amadora sob forte ruído ambiente.


4. Desafios, Problemas e Soluções
Problema 1: Perda de Sincronismo (Escorregamento do Quadro)

Desafio: No desenvolvimento inicial do Método 1 e nas primeiras versões do Método 2, uma pequena latência do sistema operativo (Windows/Linux) fazia com que o recetor se atrasasse. Ao chegar no 5º ou 6º bit, o recetor estava a ler fora da janela de emissão, corrompendo o caractere.

Solução: A alteração da arquitetura no Método 2 para uma Máquina de Estados Assíncrona. Ao forçar o recetor a esperar ativamente pela frequência exata, e depois exigir ativamente um estado de silêncio (separador) antes de passar ao próximo bit, eliminou-se completamente o problema do escorregamento.

Problema 2: Falsos Positivos por Ruído Ambiente

Desafio: Qualquer batida na mesa ou voz disparava a gravação do Start Bit por atingirem o limiar de volume.

Solução: No Método 2, a transição para FSK em conjunto com a análise por blocos de 20ms com o Algoritmo de Goertzel tornou o recetor efetivamente "surdo" a barulhos genéricos. A energia de vozes ou palmas é dispersa em várias frequências; apenas os tons puros de 1200Hz ou 2200Hz possuem energia concentrada suficiente para ultrapassar a barreira matemática (TONE_THRESHOLD_HIGH).

5. Declaração do Uso de Inteligência Artificial
No desenvolvimento deste projeto, foram utilizadas ferramentas de IA Generativa (como o ChatGPT/Gemini) com os seguintes propósitos restritos e controlados:

Compreensão de DSP (Processamento Digital de Sinais): A IA foi consultada para explicar e gerar o bloco matemático do Algoritmo de Goertzel (fórmula matemática de conversão de tempo para frequência) otimizado para a linguagem C.

Estruturação de Código e Pair-Programming: Auxílio na estruturação da Máquina de Estados (State Machine) não-bloqueante no callback do microfone (API MiniAudio), evitando que a thread de áudio travasse (congelasse) durante a espera do sinal.

Revisão de Texto: Uso para revisão ortográfica e estruturação da formatação inicial deste relatório técnico.

Toda a lógica de negócios, arquitetura de separação por silêncio (60ms), escolha das regras de deteção de erro (Soma dos bits) e testes em ambiente físico real foram decisões e execuções inteiramente humanas.

6. Conclusão
O projeto ilustrou na prática a complexidade da Camada Física do modelo OSI. Transformar o ar em um meio de transmissão de dados é algo bem desafiador

Ficou evidente a razão pela qual as redes modernas utilizam cabos de cobre, fibra ótica ou Wi-Fi/Bluetooth. O meio acústico é o mais instável. No entanto, através de técnicas de processamento digital de sinais (como o FSK com Goertzel) e mecanismos de integridade (Soma de Bits), a equipa conseguiu contornar as severas limitações físicas do meio acústico, entregando uma transmissão lenta, porém incrivelmente robusta e à prova de interferências normais.
