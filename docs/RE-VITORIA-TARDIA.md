# Vitória tardia sem HUD remoto

Análise estática em 18/10/2026. Nenhum jogo foi iniciado por esta análise. Endereços do Xbox, somados à base de memória do runner. **A receita original abaixo era PALPITE. A implementação e a validação parcial posterior estão registradas na atualização ao final.**

## Resultado

Não encontrei uma escrita única que dê uma vitória de carreira completa depois de `ReportRaceResult` executado. A opção menor é **adiar o primeiro ReportRaceResult**, manter o remoto protegido contra callbacks sem HUD e usar uma segunda chegada nativa exclusivamente do jogador local para liberar o resultado. O protocolo precisa transmitir resultado terminal de derrota também; transmitir somente vitória causa espera sem fim quando os dois perdem.

Não alterar contadores de carreira, nem reabrir as guardas `race+0x0D/+0x0E` depois do pagamento. A vitória normal executa hookmen, sidequests, geração de novas corridas e estatísticas; não se reduz a dinheiro e número de vitórias.

## Evidência CONFIRMADA

```asm
001E0744 mov al, [edi+0x9C]       ; entry(i)+4
001E074C jnz 0x001E0C22          ; não repetir chegada de uma entry já concluída
001E0752 mov byte ptr [edi+0x9C], 0x01
001E0759 inc [esi+0x1250]
001E0BFE call 0x002693C0         ; humanos
001E0C03 mov ebx, [esi+0x1250]
001E0C0B cmp ebx, eax
001E0C0D setz cl
001E0C10 add ecx, 0x06          ; 6 enquanto falta um humano; 7 quando todos concluíram
001E0C1E call [edx+0x04]

001C22FE jmp [edx+0x18]         ; RequestState chama EnterState imediatamente
001C3BCD mov ecx, esi
001C3BCF call 0x001C2B00        ; entrada em estado 7
001C2B34 call 0x001CFC50        ; dinheiro
001C2B44 mov cl, [eax+esi*1+0xF5] ; entry local+0x5D
001C2B4F setz dl
001C2B55 call 0x001D1810        ; ReportRaceResult(0=vitória, 1=derrota)
```

Assim, trocar apenas o estado da máquina para 7 pula justamente as funções necessárias. Não há fila de evento demonstrada em `SM+0x48`: `0x1C3B70` o preenche dentro de EnterState, executa os efeitos e só então copia para `SM+4` em `0x1C3C0B..0x1C3C0E`.

Guardas permanentes:

```asm
001CFC56 mov al, [edi+0x0E]
001CFC5B jnz 0x001CFD02         ; prêmio já processado
001CFCDD mov esi, [0x00557F00]  ; estatística de dinheiro, depois da soma
001CFCFD mov byte ptr [edi+0x0E], 0x01
001CFD28 mov al, [ebp+0x0D]
001CFD2D jnz 0x001D0123         ; estatísticas de resultado já processadas
001D182C mov al, [edi+0x0D]
001D1843 call 0x00277EE0        ; crédito de carreira, condicionado à guarda/resultado
001D184B call 0x001CFD10        ; estatísticas
001D1862 mov byte ptr [edi+0x0D], 0x01
```

Na primeira chegada em estado 6, essas funções ainda não foram chamadas pelo fluxo de chegada. Já na entrada em 7, carreira `0x277EE0` chama `0x277530` para vitória (`0x277FD2..0x277FE6`), geração `0x2893C0`, tarefas `0x2919E0` e atualiza percentuais. `0x277530` consulta o registro local e faz progresso de hookman/sidequest antes de remover o registro. Escrever só `careerData+0xA78` e `+0xACC` não replica isso.

## Receita PALPITE para implementar e testar

Primeiro habilitar somente **dois humanos, rede nativa desligada, sessão nativa -1, vtable 0x46B2D8 e corrida sem timer de desistência (`race+0x6F4==0`)**. Começar pelo circuito de tipo 4 já usado no teste. Não aplicar a eliminatórias, time-up, perseguições ou vtables diferentes por extensão automática.

1. Criar estado do mod por `{serial, ponteiro race, identificação city/raceIdx}`. Remoto continua `HUD=-1`, `entry.finished=1`, `DNF=1`, voltas 0 e CPs fechados. **Não somar a conclusão remota a `race+0x1250` ainda.** Não permitir que o código antigo some esse contador durante a espera.
2. A chegada real local conta 1/2 e entra em estado 6. Capturar uma única vez resultado local **antes de aplicar vitória compartilhada**: `entry+4`, `+5D`, `+6`; tempo final `+10`, duração da última volta `+24`, melhor volta `+28`. Exigir `race+0D==0 && race+0E==0`. Enviar `Finished(serial, won, dnf, ...)` inclusive quando `won=0`, repetindo até confirmação ou no estado periódico confiável.
3. Resolver `sharedWon = localWon || remoteWon`. Se local já ganhou, pode liberar imediatamente após publicar seu resultado terminal. Se perdeu, aguardar resultado terminal remoto; ambos enviam derrota **antes** de aguardar, evitando deadlock. Se remoto desconectar ou expirar prazo declarado, liberar com o resultado local. Depois de resolver uma vez, ignorar pacotes tardios desse serial para crédito; não prometer vitória retroativa após esse timeout. Não segurar a espera usando `setup.humans`: manter os dois setups com 2 humanos.
4. Para liberar a corrida após a primeira chegada local, exigir estado6, ponteiro e serial ainda iguais, contador1, localfinished1, localDNF0, timerdesligado e guardas0. Interpretar o contador existente1 como remoto contado: **não incrementá-lo agora**. Rearmar apenas local: `finished=0`, `laps=1`, `win=sharedWon`, `nextCP=N-1`, `CPcount=N-1`, `CPcountSecondary=N-1`. Manter remoto finished1. O segundo evento local vai incrementar1→2, jamais3. Marcar a fase rearmada no mod antes de escrever para não repetir em outro frame.
5. Exigir `1<=N<=64`, tabela de checkpoints mapeada, local HUD/câmera válidos. Os dois bitmaps `entry+4C/+50` (CPs confirmados) e `+54/+58` (CPs da outra fase) devem ter todos os CPs anteriores visitados e **o bit do último CP apagado**. `+34=N-1`, `+48/+49=N-1`. Fora da rede nativa, `0x1DFE00` testa os dois bitmaps, detecta o cruzamento e chama `0x1DFB50→vtable+70=0x1DFC00`. Quando a contagem atingeN, `0x1DFCBC→vtable+130=0x1DF8B0` consome a última volta e chama a chegada local.
6. Mover o carro local ao centro do último CP, usando a mesma operação `car::Place` já exercitada no teste (`cp+FC` x/y/z, y+0.5). O teste geométrico `0x1CF8B0` mede distância horizontal/altura; não exige velocidade ou uma direção. Manter a colocação até observar finished1/estado7, com prazo curto e log de falha. Durante a reativação, o piloto/câmera já podem estar no modo pós-chegada; a repetição e o comportamento visual precisam de validação ao vivo.
7. **Evitar falsa melhor volta:** na primeira chegada `0x1DF90A` já substituiu `entry+1C` pelo instante de chegada. Se rearmar sem corrigir isso, a segunda chegada pode virar uma “melhor volta” de milissegundos. Antes do replay, escrever `entry+1C = originalFinishTime - originalLastLapDuration`, conservar `entry+28` e não zerar melhores tempos. O segundo total incluirá a espera da rede; isso piora o tempo, sem fabricar um recorde menor. Preservar tempos exatos exigiria estudo adicional; não mexer no relógio global `SM+0C` como atalho.
8. Na segunda chegada, `0x1DF944` recalcula o vencedor. Um `entry+5D=1` preset sobrevive pelo retorno antecipado `0x1DF0D8..0x1DF0E4`, desde que `entry+7!=0`. A callback é só local, com HUD válido; o remoto permanece na guarda finished. `race+1250` vai2, EnterState7 executa crédito/pagamento/estatísticas pela primeira vez. O prêmio do vencedor pode continuar sendo enviado depois desse pagamento e complementado pelo delta já implementado.

### Efeitos repetidos que não são o crédito permanente

**CONFIRMADO:** `0x1E06E0` antes de EnterState7 atualiza bitmask, configura o carro pelo caminho `0x2C8580→0x120EE0`, reseta estado de habilidade em `0x2BF840`, toca sons e exibe banner de chegada/HUD. Essas operações ocorrerão duas vezes para o local; não estão protegidas por `race+0D/+0E`. Não vi uma segunda chamada ao crédito de carreira ou às estatísticas permanentes dentro desses trechos.

**PALPITE:** a repetição do estado de carro/HUD é tolerável. Deve ser conferida com o banner ativo, carro em pós-chegada e câmera real. Não se pode afirmar que o replay é seguro apenas por testar bytes em RAM simulada.

**CONFIRMADO:** `0x1DF8B0` também atualiza tempos de volta e tempo total; por isso o ajuste de `+1C` acima é necessário. Nunca testar esse replay em save real antes de conferir registros/estatísticas.

### Por que não disparar o timer local

`0x1CC710` escreve `entry+6=1` e **zera `entry+5D` em 0x1CC720 antes da chegada**. O EnterState7 lê esse zero dentro da mesma chamada. Portanto o timer local é apropriado para derrota/desistência, mas não resolve a vitória compartilhada; não há oportunidade de escrever win1 entre as duas instruções pelo hook de input.

## Testes concretos

Em memória simulada: contador inicial0 apesar de remoto finished1; primeiro finishlocal leva1/estado6; ausência de pacote não libera; Finished(won0) também sai para rede; peerwin0 resolve derrota, peerwin1 resolvevitória; rearme mantém contador1 e remoto protegido; repetição/reordenação/serial antigo não rearmam de novo; bitmap final apagado; `+1C` não deixa volta de poucosms; desconexão/timeout e novo mundo limpam estado; vtable/timer/guardas divergentes impedem replay. As callbacks nativas podem ser representadas no harness para verificar a máquina do mod, mas isso não prova sua execução real.

Teste ao vivo descartável: ambos perdem, parceiro vence antes/depois da chegada local, 200–2000ms de atraso, perda e duplicação de Finished, desconexão durante espera, resultado após timeout e mudança de mundo. Conferir estado6→7, dinheiro e NumberOfWonRaces exatamente uma vez, registro removido uma vez, geração de novas corridas, melhores voltas não fictícias, tempo total, autosave/reabrir, HUD sem tela dividida. Não incluir esta receita no pacote como validada antes disso.

## Higiene do Start.record na carreira do parceiro

**CONFIRMADO:** em `record[0x4C]`, `+0x00` é vtable e `+0x20` é a referência bruta de tarefa/nome da tarefa, local à cópia. O construtor `0x27D0A0` põe `+0x20=0` (`0x27D0F9`) e `+0x1E=0` (`0x27D0F5`). O save `0x27D802..0x27D828` carrega `record+20` e o passa ao serializador de string; zero vira string vazia. Não transportar esse endereço do host. `+1E` é índice do tipo de tarefa em tabela ligada à carreira; o save resolve em `0x27D7D8..0x27D7E4`. Sem remapeamento por nome, publicar **+20=0 e +1E=0** no novo registro injetado.

**CONFIRMADO:** `+1C` é índice s16 de nome de corrida na tabela estática da cidade, não ponteiro: `0x27D780..0x27D7A6`. Pode ser preservado se for -1 ou estiver dentro da contagem da mesma base de dados; fora disso, usarFFFF. Os campos de dificuldade/prêmio são floats, os campos finais são oito configs de3bytes; não vi outro ponteiro bruto nesse layout além de+00/+20.

Há também **referências locais codificadas em bytes**, que são tão perigosas quanto um ponteiro transplantado:

```asm
0027D1B4 mov cl, 0xFE
0027D1B9 sub cl, bl
0027D1BC mov [eax+edx*1+0x35], cl ; config[i].byte1 = FE-slotLocal
0027D258 mov al, [esi-0x02]      ; config[i].byte0
0027D25D jl 0x0027D27E
0027D25F movsx eax, byte ptr [esi-0x01]
0027D263 cmp eax, ebx           ; ebx=-1
0027D265 jnl 0x0027D276
0027D267 mov ecx, 0xFFFFFFFE
0027D26C sub ecx, eax
0027D271 call 0x0027E520        ; libera o slot de nome LOCAL
```

Para cada `config = record+34+i*3`, se `s8(config[0])>=0 && s8(config[1])<=-2`, o segundo byte representa `k=-2-s8(config[1])` no pool da carreira `data+B1A+k*23`, com12strings de0x23bytes. Copiar o byte do host pode usar o nome de outra corrida da carreira local e depois liberar o slot dela quando a corrida injetada for removida.

Patch mínimo para o protocolo atual: invalidar **cada tripleto desse caso para FF FF FF**, sem liberar nenhum slot local; se algum foi invalidado, `record+27=0`, para o criador local regenerar as configs (`0x2774CE..0x27751B`). Alternativa que preserva os mesmos adversários: transportar strings de nomes, reservar slots livres locais e reescreverFE-k. Isso exige expansão deStart e gerenciamento de recursos; não manter bytes com significado de propriedade do pool do host.

Validar também `NumOpponents+26<=8`, city/raceIdx contra a base de dados local e floats finitos antes de publicar. Reusar um registro já existente no parceiro mantém seus ponteiros/pool locais: não substituir seus bytes pelo payload do host. A regeneração das configs pode alterar adversários em relação ao host; isso precisa ser resolvido por manifesto/nomes para a futura sincronização da IA, mas evita corromper nomes/saves agora.

## Implementação e validação posterior (18/10/2026)

Implementado em race.cpp com guardas restritas a46B2D8/dois humanos; ativação padrão, opt-out MC3COOP_FINISH_GATE=0. Ao vivo, parceiro primeiro, resultado atrasado2s, host com perda induzida por arrested+5: espera6→replaylocal→7; contador1→2; vitória e+800 em ambos uma vez, melhor volta preservada. Derrota dos dois também passou. Ver TESTE-ISOLADO-2026-10-18.md para evidências e limites.

A comparação nativa de posição em1CF440 usa os DOIS humanos, inclusive arrested+5, DNF+6, finished+4, tempo+10, laps+18, métrica+3C e score+44. Copiar apenas AI+14 não basta quando local5º/remoto1º diferem. Protocolo4 amplia Finished para20bytes, sem ponteiros: serial, won/dnf/arrested/reserved, finish_time/progress/score. O hook importa apenas os campos de comparação do remoto já protegido e mantém finished/CP/counter/HUD. Após o Report local, restaura o tempo original somente para classificação, sem mexer em melhor volta, relógio global, prêmio ou guardas. As IAs [2,3,4] ficaram iguais mesmo com humanos5º/1º.

Timeout15s é uma decisão definitiva de crédito local. Metadata terminal tardia ainda pode atualizar classificação, mas não liga partner_won, não concede prêmio, não rearma e não reabre Report. Testado em RAM simulada; ensaio ao vivo com20s interrompido por avisos de firewall. Persistência/reabertura do save do gate, perda natural em2ºlugar e outros modos continuam pendentes. A rotina de chegada/HUD local pode executar duas vezes; estatísticas de tempo total podem incluir a espera. Não anunciar o caminho como universalmente validado.
