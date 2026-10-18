# HUD remoto sem câmera e encerramento local

Revisão estática e testes de memória simulada em 18/10/2026; sem executar o jogo. O achado de tela dividida/escritas inválidas veio do relatório do agente repassado pelo usuário. A implementação segue a recomendação de retirar o HUD e concluir a vaga remota por dados.

## Evidência conferida

- Em 0x2BD8CB, o criador de HUD testa `setup+0x128+i*0x2C` contra -1; 0x2BD8D3 pula 0x2BE8C0 quando não há número de HUD. O último grava o objeto de HUD em `player+0x58` (0x2BE8CF).
- Em 0x1E0744..0x1E074C, a chegada nativa testa `entry+4` e retorna se já concluiu. Esse caminho evita os usos perigosos de `player+0x58` mais adiante. Só a flag não basta: 0x1DF8C8..0x1DF8CD testa voltas restantes antes de processar a volta/chegada; por isso a implementação também zera `entry+0x18` e fecha os checkpoints.
- Checkpoint humano tem guarda de bitmap em 0x1DFEBA/0x1DFEC0. 0x1DF340 pode recomputar `next_cp` a partir da contagem, por isso a contagem e os bitmaps também ficam coerentes com a conclusão.
- 0x1E0759 incrementa `race+0x1250` na chegada real. O mod faz a contagem remota uma única vez, antes da chegada local, sem chamar a função. Desconexão não repete a operação.
- Classificação 0x1CF5D2..0x1CF5F0 coloca o desistente atrás de quem não desistiu. Marcar só "chegou" com tempo zero faria o placeholder remoto ganhar a classificação quando o local chegasse. `entry+6=1` evita isso. O parceiro pode aparecer como desistente no HUD local; seu prêmio/vitória real é comunicado pela rede.
- 0x1E0BFE..0x1E0C1E pede estado 7 quando todos os humanos foram contados. Como o remoto já foi contado, a chegada local pode entrar em 7 diretamente; o mod precisa observar a vitória também em 7, não apenas 5/6.
- `+0x1250` não tem essa semântica universal. Só são alteradas as classes 0x46B2D8, 0x46B4B8, 0x46ABD8 e 0x46B188. As demais exigem análise própria.

## Correção adicional no mapa do tráfego

O texto antigo em RE-TRAFEGO descrevia a densidade em função de `players-1`. O asm contradiz essa descrição:

```asm
0006CDC8 mov eax, [0x0053F2E0]
0006CDCD mov edi, [eax+0x50]  ; número de telas locais
0006CDD0 mov edx, [eax+0x08]
0006CDD3 dec edi
0006CDF5 mov edi, [esi+edi*4+0x70]
0006CDFB mov edi, [esi+edi*4+0x68]
0006CDFF cmp [esi+0x80], edi
0006CE17 call 0x0006C9B0
```

As duas instruções de seleção pertencem a ramos alternativos conforme `setup+8==2`. São entradas DWORD; modo de rede nativo usa `tm+0x78/+0x7C`. Humanos (`+0x4C`) determinam as regiões, mas telas (`+0x50`) escolhem a tabela de densidade offline. O HUD extra pode alterar qual entrada é usada; ainda não há evidência de que isso explica o pool 48/0 relatado.

Gates adicionais de criação: rede/sessão em 0x6C9F0..0x6CA09; byte `[0x4AFCCC]` em 0x6CA0F..0x6CA16; freeze `tm+0x144` em 0x6CCAF..0x6CCB7; disponibilidade de regiões em 0x2BA2A0 (contagens WORD `regions+0x20+8*i`, não só região zero). O comando de estado agora expõe esses dados para a próxima captura, sem forçar spawn.

## Limites

A contagem remota precisa ocorrer na thread do jogo e antes da chegada local. Não se deve invocar o timer de desistência para contar novamente o remoto nem reduzir os humanos enquanto a corrida está carregada. O fim da corrida local pode ocorrer antes do pacote de vitória real do parceiro; a solução de crédito retroativo para essa condição ainda não foi implementada. Testes simulados não demonstram ausência de tela branca, estabilidade de todos os modos ou funcionamento de autosave em execução.
