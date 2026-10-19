# Troca de modelo do humano remoto: factory e pedidos nativos

Análise estática em 2026-10-18, sem executar jogo, executáveis do jogo, funções guest ou testes ao vivo. Fontes consultadas: buscas delimitadas em `generated/game/x86_recomp.*.cpp`, `out/xref.json` via `tools/xref.py`, `mod/src/car.cpp`, `RE-MAPA.txt`, `RE-REMOTO.txt` e `RE-PENDENCIAS-MENORES.md`. Somente este documento foi criado por esta investigação.

**Resultado:** não foi encontrado um pedido por dados, consumido durante o mundo carregado, que destrua e reconstrua somente o veículo de um humano de carreira. A factory humana encontrada é temporária na pilha do construtor do jogador. O caminho de garagem reconstrói um veículo de showroom, e o comparador de setups opera sobre etapas globais de carga. Esses fatos impedem recomendar um simples campo `rebuild=1` ou zerar `player+14`.

**CONFIRMADO**: observado nas instruções citadas. **PALPITE / NÃO PROVADO**: hipótese para continuar a investigação, não autorização para escrever os campos em runtime. Não encontrar um mecanismo nestes caminhos não prova que ele inexista em todo o binário.

## 1. Factory humana não fica em player+14

**CONFIRMADO:** a cadeia ativa é:

```text
manager = [0x558814]
player = [manager+8+4*i]             ; quantidade em manager+28
vehicle = [player+14]               ; wrapper do veículo já construído
car = [vehicle+8]
phInst = [vehicle+1C]
CarCfg observado = [vehicle+64]
```

`player+14` não é uma factory nem um comando de troca. O construtor `2BFE50` prepara uma factory na própria pilha, constrói o veículo, guarda o resultado e encerra a factory:

```asm
002BFED1 mov [esi+0x14], ebx       ; começa NULL no ctor
002C0037 imul edx, edx, 0x2C
002C003A mov ecx, [edx+ecx*1+0x120] ; CarCfg do slot
002C0048 push ecx
002C004C lea ecx, [esp+0x6C]       ; factory na pilha; SP varia com os pushes
002C0050 call 0x002CB270
002C0055 lea ecx, [esp+0x54]
002C0059 call 0x002CBC40           ; build
002C005E mov [esi+0x14], eax       ; guarda vehicle, não factory
002C00CE lea ecx, [esp+0x54]
002C00D2 call 0x002CB2C0           ; encerra factory
```

`2CB270` chama o construtor base `361B30` e instala vtable `0x476940`. O construtor base guarda o CarCfg em `factory+54`; `361B80` retorna `[factory+44]`, o produto construído. `2CB2C0 ->361B70 ->376630` encerra o descritor da factory; o produto transferido continua existindo.

Consulta `xref callers 2CBC40` retorna apenas a continuação `2C004C` do construtor de jogador. `xref callers 2BFE50` retorna `2BE100`, construtor do gerenciador. A busca direta nos recompilados confirmou essas chamadas. Isso delimita os chamadores diretos; chamadas virtuais precisam ser avaliadas separadamente.

## 2. O que o build realmente faz

**CONFIRMADO:** `2CBC40` não equivale a copiar uma descrição de modelo. Há alocação, criação de customização e uma sequência de construtores virtuais de componentes:

```asm
002CBC6C push 0x3138
002CBC75 call 0x001B3B90           ; aloca mcCarCustom
002CBC9B call 0x000F4430           ; constrói customização
002CBCA9 mov [esi+0x50], eax
002CBCBA call 0x000F32A0           ; aplica CarCfg inicial
002CBCCA call [eax+0x20]
002CBCD1 call [edx+0x04]
002CBCD8 call [eax+0x10]
; mais componentes +0C/+08/+14/+24/+28/+2C/+1C/+30/+34/+38/+3C/+18
```

`2CBDE0` é parte da criação do wrapper:

```asm
002CBDE2 push 0x6C
002CBDE6 call 0x001B3B90
002CBDF4 call 0x000FF160
002CBE16 mov [esi+0x4C], eax       ; car data
002CBE30 mov [esi+0x64], ecx       ; CarCfg local observado
002CBE43 call 0x0035E070           ; cria phInst
002CBE53 mov [ebp+0x40], esi       ; vínculo inverso inst -> vehicle
002CBE56 mov [esi+0x1C], ebp
002CBE76 mov [esi+0x68], eax       ; customização
002CBE79 mov [edi+0x44], esi       ; produto da factory
002CBE7C mov dword ptr [edi+0x50], 0x00 ; transferência da customização
```

O build ainda registra o veículo em sistemas obtidos por `3308C0` e termina chamando método do produto. Portanto substituir `player+14` por outro endereço, ou copiar o wrapper de outra cópia, não reproduz registro, ownership, recursos ou vínculos de componentes.

## 3. O update não recria um ponteiro ausente

**CONFIRMADO:** `2BDEC0`, update do gerenciador de jogadores, percorre os jogadores existentes. Um slot NULL é apenas ignorado:

```asm
002BE011 lea ebp, [edi+0x08]
002BE014 mov ecx, [ebp]
002BE017 test ecx, ecx
002BE019 jz 0x002BE020
002BE01B call 0x002C1520           ; atualiza player existente
```

Não há chamada a `2BFE50`, `2CB270` ou `2CBC40` no update examinado. As verificações do modo de rede trocam jogador/câmera observado; não materializam um novo player/vehicle a partir do CarCfg.

**CONFIRMADO:** `2C1520` também não trata `player+14=NULL` como pedido para build. Alguns trechos toleram NULL para posição e velocidade, mas um trecho posterior dereferencia sem guarda:

```asm
002C18C2 mov ecx, esi
002C18C4 call 0x002C0170
002C18C9 mov ecx, [esi+0x14]
002C18CC mov eax, [ecx+0x24]       ; não testa ecx aqui
```

Logo, **zerar player+14 não é uma receita de recriação**. Zerar o slot `manager+8+4*i` tampouco solicita construção: o loop o pula e outros sistemas continuam possuindo referências ao jogador.

O gerenciador é criado por `2BE2C0` (aloca `0x74`, chama `2BE100`, publica em `0x558814`). `2BE100` constrói a coleção inteira a partir do setup, alocando `0x2EC` por jogador e chamando `2BFE50`. Seu destrutor `2BDE20` percorre a coleção, chama destrutores, limpa slots e libera estruturas globais. Não é um caminho de manutenção de um veículo isolado.

## 4. Dirty bits: funcionam para customização do mesmo modelo

**CONFIRMADO:** o update do gerenciador de veículos `FEF80` percorre a coleção e chama `F96C0` com o CarCfg observado em `vehicle+64`:

```asm
000FF021 mov ecx, [ecx+0x64]
000FF024 push ecx
000FF025 mov ecx, esi
000FF027 call 0x000F96C0
000F96D7 mov ecx, [ecx+0x2F0]
000F96DE push 0x00                ; forceAll=false
000F96E1 call 0x000F32A0
```

`F32A0` consulta a máscara `cfg+8` e aplica `mask & 0x3F7F`, excluindo o bit `0x80`. Os bits consumidos são limpos em `F4308..F4314`. `271E20`, CopyFrom nativo, marca `cfg+8=0x3FFF`; o memcpy simples não reproduz esse ato automaticamente.

**Consequência:** marcar a máscara pode solicitar pintura/peças/tuning do objeto atual; não solicita o build básico de outro TypeName. O nome fica em `CarCfg+E3`, é resolvido por `270AE0`, mas não há comparação desse nome com recriação dentro do update examinado.

Receber outro modelo deve manter a configuração integral pendente para a próxima construção normal. Não aplicar peças e níveis do modelo novo ao objeto antigo. Para o mesmo TypeName, a receita incremental proposta em `RE-PENDENCIAS-MENORES.md` continua a hipótese testável menor; seus efeitos específicos ainda precisam de teste.

## 5. 20260: comparação global de recursos, não pedido por slot

**CONFIRMADO:** `20260` compara setups current `[53F2E0]` e next `[53F2E4]`, incluindo humanos `+4C`. Quando essa contagem não muda, compara cada CarCfg por índice de modelo, flag `+E2` e texto `+F9`:

```asm
00020512 mov eax, [edi+0x4C]
00020515 cmp [esi+0x4C], eax
0002051A mov bl, 0x01             ; coleção mudou
00020539 mov ebp, [esp+0x18]
0002053D add ebp, 0x120           ; CarCfg do primeiro slot
00020547 mov ecx, [eax+ebp*1]
0002054A mov esi, [ebp]
0002054D call 0x00270AE0
00020556 call 0x00270AE0
0002055B cmp eax, edi             ; modelo atual vs próximo
000205BD mov bl, 0x01             ; uma mudança invalida a etapa comum
000205CB add ebp, 0x2C
```

O resultado é um booleano global, não um índice do humano alterado. `20613..20644` verifica a etapa de recurso 3, chama wrappers de descarregamento, método virtual `+0C` e finalização. `20834..20865` a carrega se ausente. As etapas 0/1/2 têm lógica semelhante, com dependências de cenário/setup.

`1FF30` obtém `[resourceManager +4+4*stage]`, sem argumento de jogador. Os wrappers `1FF40`, `1FFB0`, `1FFD0` executam métodos virtuais das etapas e manutenção global. Os chamadores diretos `21170` e `212E0` de `20260` são transições da máquina de estados, não o update ordinário do jogador.

O caminho de carga em `29337..29685` demonstra a amplitude do ciclo normal: inicializa recursos visuais, OppMgr, recria o gerenciador de jogadores em `294F6 ->2BE2C0`, recria a lista de corredores em `2951E ->2C86E0`, e faz trabalho de corrida/HUD. **Não foi provado que escrever uma flag de etapa possa restringir esse ciclo ao remoto.** Não usar uma diferença artificial entre setups para tentar uma troca isolada.

Há uma fila real de eventos globais na máquina de estados: o consumidor `20ED0..21089` usa contador `+3C`, índice `+38` e buffer circular de dez DWORDs em `+0C`, depois despacha eventos por uma jump table. Os casos examinados chamam transições e descarregamento por etapas. **CONFIRMADO:** essa fila existe. **NÃO PROVADO:** qualquer evento dela reconstrói só um veículo; não há argumento de slot no item consumido. Não a tratar como fila de factory por jogador.

## 6. Garagem: troca individual existe, mas é showroom

**CONFIRMADO:** a garagem usa duas configurações e dois veículos de exibição, indexados por `menu+72C`, com endereços de veículo em `menu+714+4*i`, CarCfg em `menu+71C+4*i` e bancos/escopos de memória em `menu+724+4*i`.

`152420`, update da garagem, muda o índice de buffer em `152E3E..49`, copia a configuração escolhida para esse buffer, atualiza textos e chama diretamente `13C8E0` em `152F79`. Não passa pelo player remoto ou pela lista de humanos em corrida.

```asm
0013C8F2 mov eax, [esi+edi*4+0x724]
0013C8FA call 0x002699C0          ; seleciona escopo/banco de recursos
0013C8FF mov ecx, [esi+edi*4+0x71C] ; CarCfg de showroom
0013C91D call 0x002CB270          ; factory temporária
0013C969 call 0x002CBAE0          ; build de frontend, não2CBC40
0013C972 mov [esi+edi*4+0x714], eax ; publica no menu
0013CA22 call 0x002699E0          ; sai do escopo
0013CA5C call 0x002CB2C0
```

`13CA70` destrói esse veículo por método virtual, remove dados do gerenciador de modelos (`1057B0`), destrói customização e libera/reset o banco (`1B4550`), com trabalho extra sobre o outro buffer. A seleção de bancos e o ciclo de espera de recursos fazem parte da operação. Há chamadas de física repetidas no update da garagem enquanto o carro de exibição fica pronto.

**Limite:** o fato de a garagem reconstruir somente um objeto de exibição não demonstra uma flag reaproveitável para o humano remoto durante corrida. Os próprios destinos das publicações são diferentes (`menu+714` versus `player+14`), assim como o build. Tentar instalar um objeto do showroom em `player+14` não preserva automaticamente input, áudio, física, câmera, racer e registros.

## 7. Bloqueios e próximos passos concretos

Não há implementação de troca de modelo distinto recomendada apenas com os campos encontrados. Em particular, não usar estes supostos gatilhos:

- `player+14=0` ou `manager.players[i]=0`: não há construção tardia demonstrada; há dereferência sem guarda.
- `cfg+8=0x3FFF`: o aplicador exclui o bit de modelo e não chama a factory.
- mudar TypeName e esperar `2C1520`: não há comparação/rebuild nesse update.
- manipular etapas globais ou a fila de eventos para simular a garagem: perde o requisito de trocar somente o remoto e não há transação de recursos comprovada.

Para provar uma receita nova, falta achar **um consumidor nativo** que receba identidade do player e CarCfg, faça teardown do veículo atual, espere recursos e publique o novo em `player+14`, reparando registros e referências. A factory na pilha e o gerenciador já examinados não fornecem esse consumidor. Alternativamente, seria necessária uma ampliação explícita do mecanismo permitido para executar a transação nativa; isso não foi usado nem implementado aqui.

Próxima instrumentação útil, se houver teste ao vivo autorizado ao agente principal: registrar TypeName e dirty mask em `vehicle+64`, identidade de player/vehicle/car/inst/body, etapas de recurso e geração do mundo; escolher outro carro pela garagem normal e comparar as mudanças. Isso pode validar o caminho global já mapeado e delimitar sua fase. Não prova por si só um sinal isolado durante o mundo carregado.

Enquanto não existir consumidor por slot comprovado, o comportamento honesto é aplicar alterações compatíveis do mesmo modelo pelo caminho incremental e adiar o novo TypeName até a próxima carga normal de corrida/cruise. Esta análise não concluiu a troca de modelo ao vivo.
