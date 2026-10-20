# Próxima etapa nativa de torneio: identidade antes da carga

Análise estática em 2026-10-19 na principal `C:\dev\mc3-coop`, a partir de `981aaa2`. Sem jogo, chamadas guest, executáveis do jogo, firewall ou leitura de processos. Foram usados `tools/xref.py`, buscas delimitadas no recompilado e leitura de `assets/default.xbe`/`assets/assets_x.dat` como dados. Este documento complementa `RE-HOST-FLUXO-RESULTADO.md`.

**CONFIRMADO:** as séries reais examinadas usam tipos de setup **7 (`cp_ordered`) e21 (`autocross`)**, não3/4. Foram cruzadas todas as67 referências a etapas dos19 arquivos `Tournament`/`TokyoTournament` com seus `.rinf`:58 tipo7 e9 tipo21. A trava de `SM+4C` do estado12, restrita a tipos1/3, portanto não governa essas séries. Elas passam pelo outro ramo `1C20B0`/UI. Não extrapolar a eficácia da trava automática a torneios.

**CONFIRMADO:** é possível conhecer uma candidata exata à próxima corrida lendo tarefa/cursor e resolvendo seu UniqueId local, sem suspender a tarefa nem alterar listas. A seleção final deve continuar sendo a do jogo: `278270` pode ativar tarefas e chamar callbacks que substituem a primeira escolha. Um candidato não autoriza liberar a carga de outra corrida.

## 1. Identidades e offsets

| Objeto/campo | Significado confirmado |
|---|---|
| `[53F2E0]` | CURRENT, setup atual |
| `[53F2E4]` | NEXT, setup preparado para a próxima carga |
| `setup+0` DWORD | cidade |
| `setup+18` DWORD | tipo vindo do descritor `.rinf` |
| `setup+68` string inline | nome completo da corrida, copiado da DB |
| `[557F0C]` | career |
| `[career+0C]` | career data |
| `data+50` DWORD | cidade da corrida corrente/selecionada |
| `data+58` WORD | UniqueId da corrida corrente/selecionada |
| `[data+10]` | gerenciador de tarefas |
| `manager+78/+7C` | begin/end de entradas de12 bytes; ponteiro da tarefa em entrada+8 |
| `manager+94` DWORD | índice ativo; FFFFFFFF significa nenhum |
| `task.vtable=470348` | tarefa comum a Tournament eTokyoTournament |
| `task+0C` byte | tarefa ativa |
| `task+E0` byte | ramo preparado/continuação; `264C60` só prepara etapa se não zero |
| `task+10/+14` | número de itens e ponteiro para itens de8 bytes |
| `task+DC` DWORD | cursor nativo atual |
| `item+4` byte | zero=corrida; não zero=segmento nomeado/movie |
| `item+6` WORD | **UniqueId local**, não RaceIdx |
| `data+A5C/+A60/+A64` | Open begin/end/cap; registros de4C bytes |
| `data+A6C/+A70/+A74` | Pending begin/end/cap; entradas de50 bytes, registro em+0 |
| `record+4/+6/+8` | UniqueId WORD / RaceIdx signed WORD / cidade signed byte |
| `record.vtable=473E74` | registro de corrida usado pelo runtime existente |
| `[557EE4]+city*50` | linha da DB de corridas da cidade |
| `cityDB+14` DWORD | número de descritores/nomes |
| `cityDB+18` | array de ponteiros para nomes completos |
| `cityDB+20` | descritores de30 bytes; tipo em+0, posição inicial em+14 |

Não foi encontrado um campo RaceIdx próprio em NEXT neste caminho. Ele vem do registro selecionado ou da busca exata de `NEXT+68` no array de nomes da cidade. `Start.name[32]` é rótulo truncado para exibição; não serve como identidade completa.

## 2. Resolver antes da preparação sem modificar a tarefa

`2670A0` recebe o subobjeto `task+10`, portanto seu `[ecx+4]` é `task+14`:

```asm
002670A0 mov eax,[ecx+04]
002670A7 mov ax,[eax+index*8+06] ; UniqueId
002632F4 call 00290780          ; data=[ [557F0C]+0C ]
00263303 call 002670A0
0026330D call 0027F0B0          ; procura em Pending
0026331A call 00283370          ; MOVE Pending para Open quando encontrado
00263322 call 0027F010          ; devolve registro Open
; sem Pending:
0026332D call 0027EFF0          ; procura Open
00263335 call 0027F010
```

Não chamar `2632F0` nem imitar a movimentação: é suficiente procurar em ambos os vetores, somente leitura. O stride é provado pelos getters:

```asm
0027F014 mov edx,[ecx+A5C]
0027F01A imul eax,eax,4C
0027F01D add eax,edx
0027F0D4 mov edx,[ecx+A6C]
0027F0DA lea eax,[eax+eax*4]
0027F0DD shl eax,04             ; index*50
0027F0E0 add eax,edx
```

Algoritmo conservador: validar manager/índice/vetor; exigir tarefa470348, ativa+0C e continuação+E0; validar cursor contra count; exigir item+4==0 e UniqueId não zero; resolver esse UniqueId em Open/Pending desta cópia; ler cidade/RaceIdx do registro; validar limites e nome completo na DB local. Para comparar com a corrida recebida do host, comparar cidade/RaceIdx e a identidade desses campos no `Start.record`. **UniqueIds de duas carreiras não precisam coincidir.**

`NativeNextMatches(const Start&)` em `mod/src/race_next.inc` implementa essa consulta. Rejeita UUID duplicado, tamanho de vetor não múltiplo do stride, registros com vtable inesperada, cursor/contagens fora dos limites, movie e nome inválido. Não modifica tarefas, registros, setups ou serial. Os limites conservadores são cidade0..3, RaceIdx0..159 e até512 registros/itens por vetor; mapeia somente os objetos/entradas acessados, sem sondar regiões enormes. A rotina também relê os ponteiros/cursor/UUID antes de retornar; deve ser chamada pela thread do jogo.

**Limite confirmado:** um matcher da tarefa ativa prova qual etapa seu cursor solicita **agora**. Não prova que uma tarefa ainda inativa será a que `278270` ativará; não prevê automaticamente a próxima etapa antes de o `ReportRaceResult` nativo atualizar o cursor.

## 3. A preparação final escreve a identidade antes de carregar

`277AE0(UniqueId)` resolve a corrida Open e prepara NEXT:

```asm
00277AE9 mov ebp,[53F2E4]        ; NEXT
00277AF7 call 0027EFF0          ; UniqueId -> índice Open
00277B04 call 0027F010          ; registro
00277B11 movsx eax,byte [ebx+08] ; cidade
00277B15 movsx edi,word [ebx+06] ; RaceIdx
00277B3A mov dword [career+2C],4
00277B41 call 00276F50          ; career+34 e+30
00277B4E call 002806F0          ; data+58 e+50
00277B75 call 00290590          ; dificuldade, no ramo que a exige
00277B7C call 002774B0
00277B8D call 0026B8E0          ; NEXT(cidade,RaceIdx)
00277BD3 call 0028D580          ; completa adversários/outros campos
```

`26B8E0` faz a ligação precisa entre DB e NEXT:

```asm
0026B96C mov [NEXT],city
0026B974 mov ecx,[cityDB+18]
0026B97B mov edx,[ecx+RaceIdx*4] ; nome
0026B981 add eax,descriptorBase ; eax=descriptorBase+RaceIdx*30
0026B983 lea edi,[NEXT+68]
0026B990 mov cl,[edx]           ; loop de cópia até NUL
0026B99A movsx edx,byte [desc+0D]
0026B99E mov [NEXT+04],edx       ; hora
0026B9A1 movsx ecx,byte [desc+0C]
0026B9A5 mov [NEXT+08],ecx       ; clima
0026B9A8 mov edx,[desc]
0026B9AF mov [NEXT+18],edx       ; tipo
```

O ramo `RaceIdx==-1` escolhe um descritor de roam e escreve NEXT+18=0; não é uma etapa de corrida válida. `277350` prepara cruise tipo1 e zera career+34. Tratar esses casos como ação diferente, não tentar fabricar RaceIdx.

`2806F0` atualiza cidade/data+50 e WORD data+58. Em seguida, `2779D0` consome a ação e limpa career+2C/+34, portanto `career+34` é efêmero; `data+58` continua útil após a carga:

```asm
00280718 movsx eax,byte [record+08]
00280726 mov [data+50],eax
00280731 mov [data+58],di
0028073F mov [data+58],di        ; outro ramo
00277A7C mov dword [career+2C],0
00277A85 mov word [career+34],0
```

**Cuidado:** data+58 pode já apontar para a próxima corrida enquanto CURRENT ainda é a anterior. Para confirmar uma corrida carregada, `ResolveHostLoadedStart(Start&)` consulta Open por data+58 e exige data+50, registro, DB/tipo e **CURRENT completo** coincidentes, além de native-network byte+108 zero. Só produz `out` no sucesso e deixa serial0. O chamador decide se há mundo/race carregado e se precisa abrir uma nova serial; a rotina não confere o objeto race nem inventa evento de largada.

Para confirmar NEXT, fazer o mesmo cruzamento com `[53F2E4]`, sem confundi-lo com o helper de CURRENT. **Não há uma fronteira de hook comprovada entre o retorno de `278270` e a chamada `2779D0`**. Ver um nome novo em NEXT também não prova que `28D580` já terminou de preencher seus adversários. Não prometer captura do setup completo antes da alocação apenas por esse nome. Vincular captura/envio ao contexto e ao cursor estáveis e confirmar a identidade na carga.

## 4. Preservar a escolha nativa e os trechos que não são corridas

`278270` pode resolver uma ação explícita4 por `277AE0`, ação3 por `277350` ou ação5 por `277440`; ainda chama o gerenciador e callbacks `279370`. Antes deles pode ativar uma tarefa por `279210`. Logo, escrever career+2C/34 não garante o destino final quando há torneio ativo.

`279370` usa manager+94 para achar a tarefa ativa no vetor de12 bytes e chama vtable+24. `Tournament` e `TokyoTournament` são registrados com a **mesma factory2661B0** em `266EBE..266ED4`; seu construtor265CC0 grava470348.

No callback264C60:

```asm
00264C64 mov al,[task+0C]       ; precisa ativa
00264C6B mov al,[task+E0]
00264C79 jnz 00264CA4           ; continuação pronta
00264CA4 mov eax,[task+DC]
00264CD9 cmp eax,[task+10]      ; cursor no fim: recompensa/movie/encerrar
00264D1A mov dl,[entries+cursor*8+04]
00264D23 jz 00264D33            ; item de corrida
00264D2A call 00277440          ; item nomeado/movie
00264D36 call 002632F0          ; resolve/move UUID local
00264D3B movzx eax,word [record+04]
00264D42 call 00277AE0
00264D47 movzx ecx,byte [task+44]
00264D53 mov [NEXT+40],ecx      ; limite de adversários da tarefa
```

`264D80` incrementa task+DC ao reportar certos resultados0/1; `263210` trata segmentos nomeados já vistos e pode avançar o cursor além de um item. Ler o cursor **depois da lógica nativa**, em vez de somar1 por conta própria. `264C60` também calcula/preserva pontuação, recolhe o registro anterior para Pending e escolhe `EndingMovieIfWon/IfLost` no fim. Fim de série ou movie não é largada: não publicar uma corrida fictícia para representar esse trecho. Preservar active, E0, cursor, score, flags de resultado e save.

## 5. Tipos verificados diretamente nos assets

`26DA70` lê `RaceType` do `.rinf`; `26DB20` chama `26D430` e `26DB31` grava o índice no descritor. `26D430` compara com a tabela de strings em **4B37D0**:

| Tipo | String |
|---|---|
|1|find_hook|
|3|follow|
|4|cp_unordered|
|7|cp_ordered|
|21|autocross|

Todos os19 taskdata e suas referências `Race` foram extraídos somente para memória, com descompressão raw DEFLATE e conferência do tamanho descomprimido. O arquivo `assets/assets_x.dat` tem cabeçalho Dave,41591 entradas; tabela em800 de665600 bytes e bloco de nomes logo depois,501760 bytes. Os nomes usam códigos de6 bits e compressão de prefixo, reconstituídos seguindo `2B3130`/`2B3020`; conteúdo das entradas é raw DEFLATE quando packedSize é não zero. Não foi necessário executar o jogo nem criar extrator persistente.

Os taskdata estão em `tune/progress/taskdata/`; cada referência resolve em `tune/race/<city>/<nome>.rinf`. São67 referências, não necessariamente67 arquivos únicos.

| Taskdata | Etapas | Tipo real |
|---|---:|---|
|tokyotournament.tkaclass1|4|7|
|tokyotournament.tkbclass1|4|7|
|tokyotournament.tkcclass1|3|7|
|tokyotournament.tkchopperclub|3|7|
|tokyotournament.tkdclass1|3|7|
|tokyotournament.tkexoticclub|5|7|
|tokyotournament.tkluxuryclub|3|7|
|tokyotournament.tkmuscleclub|3|7|
|tokyotournament.tksportsbikeclub|3|7|
|tokyotournament.tksuvclub|5|7|
|tokyotournament.tktunerclub|4|7|
|tournament.atldubone|4|7|
|tournament.atldubtwo|3|21|
|tournament.detautox|3|21|
|tournament.detmuscle|4|7|
|tournament.sdautox|3|21|
|tournament.sdbalboa|3|7|
|tournament.sdcompound|4|7|
|tournament.sddub|3|7|

Exemplo conferível: `tournament.sdbalboa` referencia `ordered\hard\sd_ordered_hard_balboa_race02`, `drag\easy\sd_drag_easy_bridges_race01` e `circuit\easy\sd_circuit_easy_balboa_race01`; todos são `cp_ordered`. O último `.rinf` especifica cidade sd e NumLaps2. O nome de pasta `drag` não basta para inferir um tipo de setup diferente. `NumItems` do taskdata pode incluir movies; contar apenas linhas `Race`.

## 6. Alcance da implementação e pendências

**CONFIRMADO por asm/asset:** resolução de identidade/cursor acima, tipos7/21 de todas as etapas examinadas, ausência de necessidade de suspender a tarefa para consultar a próxima escolha. `race_next.inc` apenas consulta os dados.

**Não validado ao vivo:** momento disponível entre a confirmação de NEXT e a alocação dos carros, e interação da espera/remessa com a UI nativa de resultado dos tipos7/21. A consulta de cursor não substitui uma barreira comprovada nesse caminho de UI. O chamador deve manter identidade de corrida/serial/cidade e tratar invalidação, disconnect, movie e fim de série explicitamente; não avançar o parceiro só por `UniqueId` igual ou pelo texto truncado do pacote.
