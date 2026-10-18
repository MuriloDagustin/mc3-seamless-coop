# Ensaio de puppets de adversários (18/10/2026)

Complementa `RE-IA.md`. Análise estática e teste em RAM sintética; este arquivo não registra ensaio no jogo. Implementação isolada em `mod/src/ai.cpp` e `ai.h`; o pacote deve tratá-la como **experimental**.

## Suspensão parcial encontrada

**CONFIRMADO:** o construtor do carro escreve sua vtable real e inicializa a máscara:

```asm
00120952 mov [esi+0x15C], eax        ; eax=0
00120963 mov dword ptr [esi], 0x4564F0
00120A00 mov edx, [ecx+0x15C]
00120A06 xor eax, eax
00120A08 test edx, edx
00120A0A setz al
00120A0D ret
```

O DWORD em **0x45651C** (vtable0x4564F0+0x2C), lido das seções do XBE, vale **0x120A00**. Essa é a associação que faltava no mapa anterior. O XBE foi somente lido como dados, nunca executado.

```asm
001EE1EC mov eax, [ebp]             ; ebp=car
001EE1EF mov ecx, ebp
001EE1F1 call [eax+0x2C]
001EE1F4 test al, al
001EE1F6 jz 0x001EE678              ; retorna antes dos virtuais de navegação
```

**CONFIRMADO:** o estado usado para a decisão é `R32(R32(0x557EEC)+4)`, a máquina da corrida, e não o estado em0x557ED0. Nos estados2/4 há uma exceção:

```asm
001E4841 mov eax, [0x00557EEC]
001E484D mov ebp, [eax+0x04]
001E4864 cmp ebp, 0x04
001E4867 jz 0x001E486E
001E4869 cmp ebp, 0x02
001E486C jnz 0x001E488E
001E486E mov eax, [esi+0x08]
001E4877 comiss xmm0, [eax+edi*1+0x60]
001E487F jbe 0x001E489A
001E4881 mov ecx, [ecx+0x20]
001E4884 mov ecx, [ecx+0x08]
001E4887 push 0x00
001E4889 call 0x00120EE0            ; limpa car+15C
001E488E mov ecx, [esi+0x08]
001E4893 call 0x001EE1A0
001E4898 jmp 0x001E489F
001E489A call 0x001EDF00            ; outro caminho de driver, NÃO é suspensão
```

Por isso elevar `opp+0x60` não resolve: desvia para0x1EDF00, que chama drivervirtual+0x10, copia os alvos e chama0x1ED9C0. Drivers comuns podem continuar com0x1EB240 e manobras. O ensaio deixa esse campo intacto e atua **somente nos estados5/6**, fora desses ramos.

**PALPITE de integração:** `car+0x15C |= 0x10` mantém a máscara nãozero sem habilitar os bits nativos1/2/4/8 (manobras/chegada/acidente). Os consumidores examinados de vehCar testam esses quatro bits; a semântica completa de um bit reservado0x10 não está demonstrada. `0x122A80` também muda de ramo com qualquer máscara nãozero e limpa caches de manobra em0x122E36..45, mas segue atualizando física. Nenhum registro de veículo, collider, corpo, phInst ou corredor é removido pelo mod. Manter desenho/colisão corretamente durante todas as situações ainda exige ensaio no jogo.

**Limitações confirmadas:** o teste abaixo do mundo em0x1EE1C2..E2 ocorre antes do teste da máscara e pode chamar drivervirtual+0x38. O input independente0x1FC950 continua calculando direção/freios e escreve car+6C..78 em0x1FCAD9..F6. Assim o bit bloqueia o corpo principal do driver, mas **não é um desligamento universal de todos os escritores**. O mod corrige os carros a cada gancho; se o input/física vier depois, haverá simulação divergente entre correções. O experimento não deve ser anunciado como sincronização concluída sem medir drift, respawn e contato.

## Estado implementado

`SetRace(serial,city,race_idx,bool sync_progress=false)` configura a geração de rede; precisa ser chamado em cada início comum, e `Stop()` no encerramento da sessão. Ambos apenas alteram buffers C++. `Tick(partner,now)` é o único escritor guest e deve continuar sendo chamado mesmo sem sessão, para restaurar o bit. `Snapshot/Receive` seguem o estilo do tráfego. Apenas pacotes autenticados como vindos do host podem chegar a `Receive`.

O snapshot aceita as quatro vtables de corrida comum já verificadas, estado5/6 e até oito competidores no prefixo do OppMgr. A cadeia por índice permanece:

```text
mgr=[534074]; arr=[mgr+8]; opp=arr+i*43C; u16(opp+54)==i
vehicle=[opp+20]; car=[vehicle+8]; inst=[vehicle+1C]
coll=[car+68]; body=[coll+10]; colliderMatrix=[coll+14]
```

A identidade transmite geração/cidade/race_idx e hash ordenado de tipo/modalidade, CPs, total de competidores, nome local da IA (`P+5C`, somente conteúdo), nome de modelo (`[desc+64]+E3`, somente conteúdo), índice, tipo de driver e spawn. Não transmite ponteiros locais. Save/seleção divergente causa recusa de todo o lote, não associação por aparência. É uma assinatura conservadora para o ensaio, não prova matemática de identidade nem manifesto completo de rota/CarCfg; mesma contagem/mesmo modelo com outra rota ainda exige compatibilidade do mesmo arquivo/build. Falta um handshake explícito de build e configuração. HashFNV32 também admite colisões.

Um datagrama contém um adversário, menos de256bytes; todos os adversários do ciclo precisam chegar antes da publicação atômica. Ciclos antigos, serial/identidade diferentes, NaN/infinito e valores físicos fora dos limites são rejeitados. Matriz de corpo e origem visual são distintas; se a matriz do collider aliasar a do corpo, o ensaio recusa o layout para não sobrescrever duas origens diferentes. A aplicação copia origem visual/collider, orientação e posição do centro de massa, velocidades, comandos e speedcache. Recalcula momentos com massa/inércia locais como os setters0x3432B0/0x343300. Extrapolação local máxima100ms, expiração500ms; sem novas amostras a posse é liberada.

A restauração verifica manager/array/race/opp/vehicle/car atuais. Restaura somente o bit0x10 original, preservando novos bits nativos de crash/chegada. Não escreve sobre instâncias antigas quando os ponteiros do mundo mudam. Se o jogo já destruiu o mundo antes do próximo gancho, nenhum dado antigo é tocado. Como reutilização completa de endereços ainda é possível, `Stop/SetRace` e as transições devem ser ensaiados com reloads repetidos.

## Progresso e resultado final

Por padrão a versão transporta progresso como **telemetria**. `SetRace(...,true)` habilita importação experimental nos estados5/6: `P=[race+658]+i*60`, terminou+4, preso+5, DNF+6, participação+7, timer+0C, tempo+10, posição+14, voltas+18, passagemvolta+1C, última/melhorvolta+24/+28, passagemCP+30, CPi16+34, distânciaquadrada+38, métrica+3C, pontos+44, CPcount/aux+48/+49 e masks+4C..58. O nome+5C nunca é escrito; +5D é parte desse ponteiro, não vitória. O contadorIA `race+1254` é atribuído ao número de `finished` do lote completo em cada aplicação, não incrementado por pacote. Repetir um lote final não conta duas chegadas. O teste sintético confirma a atribuição idempotente e a preservação do ponteiro de nome.

Escrever progresso arbitrário pode permitir a chegada local antes do pacote final; `0x1DF215..27` altera terminou/tempo/contadorIA+1254 e chama virtualrace+68. A importação não chama esse callback e não reproduz seus efeitos específicos. O ranking0x1CF440 roda novamente no resultado e depende também dos humanos. Para resultados iguais falta instalar um snapshot final confiável de **todos** os humanos/IA antes do pagamento, com remapeamento de jogadores e domínio da fase do gancho. Reaplicar posição+14 depois não desfaz prêmio/carreira processados. Logo esta implementação não promete posições finais iguais; a opção de progresso é separada para ensaiar isso sem confundir resultado nativo com cópia de campos.

Teste executado em `out/codex/ai_memory_tests.inc`, com o código real e RAM sintética: rejeição de geração/layout/NaN, suspensão somente5/6, aplicação da pose/momentos, restauração idempotente preservando flags nativas e importação final opcional sem duplicar contador/nome. Compilação isolada clang-cl `/W4` passou. O invólucro de testes em `out/codex/ai_tests.cpp` reutiliza o harness existente e emite um aviso de retorno na função `main` antiga renomeada; `ai.cpp` compilado isoladamente não emitiu avisos. Nenhum teste desta subtarefa abriu jogo, runner, janela ou rede.

## Autoridade do setup e resultados (18/10/2026)

Implementado e testado no runner0x6AD5001B: kRaceSetup, protocolo de rede3, repete máscara/limite e8 registros inline2C (29bytes de conteúdo; padding zerado), com serial/revisão. Nome completo em setup+68 é inline, não ponteiro. Setters nativos26B0F0/26B1B0/26B170/26B210 confirmam modelo/nome/variante/enabled no bloco+274. Aplicação apenas antes de OppMgr (estado<2 ou ronda), aos setups atual e NEXT com nome correspondente. Preserva Task* local do registro de carreira+20. Nomes de pilotos não participam do hash; modelos e spawn continuam obrigatórios.

Importação de progresso está habilitada: campos conhecidos do registro60bytes, sem ponteiro de nome+5C. Contagem de IAs terminadas race+1254 é atribuída uma vez por snapshot completo, sem chamar callbacks. Checkpoint -1 ou abaixo da contagem real; contadores limitados à corrida. Snapshot IA2 marca terminal a partir do estado7; permanece válido após500ms e é aplicado também em estados7..10, evitando devolver resultados à IA local. Nova corrida/desconexão invalida o lote.

Validação ao vivo:4/4 e5/5; cinco IAs abaixo1,27m na repetição final, colocações [6,3,4,2,1] iguais após ambas as chegadas. Evidência e limites em TESTE-ISOLADO-2026-10-18.md. Continua experimental; não houve teste entre PCs nem de classificações humanas diferentes em cada mundo.
