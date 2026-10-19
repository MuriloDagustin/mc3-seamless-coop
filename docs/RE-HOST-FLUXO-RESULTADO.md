# Host conduz: espera do parceiro no resultado

Análise estática em 2026-10-19, base informada `780a698`, pasta principal `C:\dev\mc3-coop`. Li a tarefa nova em `HANDOFF-CODEX.md`. Não executei jogo, funções guest, executáveis do jogo ou firewall; não editei runtime. Evidências obtidas por `tools/xref.py`, buscas delimitadas nos recompilados e leitura das jump tables/vtables no `assets/default.xbe` como arquivo de dados.

**Conclusão:** priorizar a trava nativa por dados em `SM+4C`, restrita a `CURRENT+18` igual1 ou3. Ela segura o avanço automático do estado12 e permite prêmio/save anteriores. Filtrar A não impede todos os caminhos de avanço; `SM+44` também não é barreira universal de torneio. **Não recomendar reescrever SM+4 para13:** o no-op do Update foi confirmado, mas não a segurança em todos os consumidores. Também não basta escrever `career+2C=3/4`: a tarefa de torneio pode sobrescrever o pedido durante `278270`. O caminho deve validar tarefa/cursor antes de liberar. A suspensão temporária da tarefa foi analisada abaixo, mas não fechou uma transação segura através da carga.

## 1. Objetos e guardas

```text
SM = [0x557EEC]                       ; máquina de estados da corrida
SM.vtable = 0x4691B8                  ; construtor1C3C60
race = [[0x533E24]+4]
CURRENT = [0x53F2E0]
NEXT = [0x53F2E4]
career = [0x557F0C]
gameSM = [0x557ED0]
```

| Offset | Uso confirmado |
|---|---|
| SM+04 DWORD | estado cujo Update roda agora |
| SM+48 DWORD | estado em processo de entrada; EnterState copia para+04 ao terminar |
| SM+08/+0C float | tempos locais usados pelos estados |
| SM+10 float | dt calculado pelo Update |
| SM+14 byte | autosave/pedido de save pendente |
| SM+26 byte | ramo de saída especial/network/frontend no resultado; não usar como trava |
| SM+44 byte | impede o avanço automático de9 para10; já usado pelo torneio |
| SM+45/+46 byte | modificadores de apresentação/recompensas do resultado, usados nativamente; preservar |
| SM+4C byte | one-shot de SetupNextCareerRace/Progress no ramo12 de tipos1/3 |
| CURRENT+18 DWORD | tipo do setup, distinto do seletor career+2C |
| career+24 DWORD | ação de carreira que iniciou o contexto atual |
| career+28 DWORD | resultado reportado |
| career+2C DWORD | próxima ação de carreira:3=cruise,4=corrida por UniqueId |
| career+30 DWORD | cidade da próxima ação |
| career+34 WORD | UniqueId da próxima corrida |
| career+57 byte | impede preparação de alguns tipos de próxima ação quando não zero |

Guardas mínimas para qualquer experimento: conectado como parceiro; modo `[53F2D4]==2`; native network `CURRENT+108==0`; race/SM/CURRENT/career válidos e ligados à mesma geração/serial; estado terminal real da corrida ativa. Não aplicar durante o state7 usado pelo cruise para preparar uma largada. Usar a identidade do objeto **e** a serial, porque endereço pode ser reutilizado por uma série.

## 2. Fluxo dos estados7..12

Tabela original de Update em `1C3B1C` e EnterState em `1C3C28`:

| Estado | Update | Entrada |
|---|---|---|
|7|1C37B9|1C2B00|
|8|1C399A|1C34F0|
|9|1C3A6F|zera SM+08|
|10|1C3A45|1C3580|
|11|1C3854|1C33E0|
|12|1C3A84|1C2BC0|
|13|1C3B15, retorna|1C2C40|

**CONFIRMADO:** EnterState é síncrono, não uma fila em+48:

```asm
001C3B94 mov [esi+0x48], edi
; chama a entrada correspondente pela jump table
001C3C0B mov ecx, [esi+0x48]
001C3C0E mov [esi+0x04], ecx
```

Estado7 espera o limiar float em `469230` (mapa anterior:3,1s). `1C2010` retorna true para `CURRENT+18` de0 a2. O Update7 pede11 quando true; caso contrário pede8. O Update8 pode chamar a apresentação de resultado e, para tipos1/3/10h, pede9. Não congelar SM+08 antes de entender se a corrida está no caminho de resultado ou no caminho de largada pelo cruise.

**Crédito já feito na entrada7:** `1C2B34 ->1CFC50` e `1C2B55 ->1D1810`; este último chama ReportRaceResult e guarda `race+0D=1`. Não reexecutar EnterState7 para governar menus. O gate de vitória tardia deve terminar seu trabalho normalmente antes do controle de fluxo pós-corrida.

**Há trabalho adicional na entrada10:** `1C3580 ->1CC450` processa recompensas extras e marca `race+0F=1`; há outros ramos de UI/recompensa antes de pedir11. Assim, estacionar9 indefinidamente antes da decisão pode adiar recompensas adicionais e autosave; pular10 para12 seria incorreto.

**Autosave11:** `1C33E0` inicia a operação, guarda `SM+14`, e o Update11 consulta o cartão via `1AB50`. Quando termina, limpa+14 e pede12. Se+14 já está zero, também pede12. A entrada12 e o retorno são separados do próximo Update12; o Update11 não cai diretamente no código1C3A84.

Não fingir save concluído escrevendo `SM+14=0`; não congelar11 inventando uma operação pendente; não alterar dirty/saved flags para sincronizar fluxo. É possível deixar crédito/recompensas/save terminarem e conter só a saída posterior.

## 3. Trava nativa do estado12: SM+4C

**CONFIRMADO:**

```asm
001C3A84 mov ecx, [0x0053F2E0]
001C3A8A mov eax, [ecx+0x18]
001C3A8D cmp eax, 0x01
001C3A90 jz 0x001C3AA0
001C3A92 cmp eax, 0x03
001C3A95 jz 0x001C3AA0
001C3A97 mov ecx, esi
001C3A99 call 0x001C20B0          ; OUTROS tipos: caminho diferente
...
001C3AA0 cmp [esi+0x26], bl      ; bl=0
001C3AA3 jz 0x001C3AC1
; se26!=0: gameSM transições8/16h, sem usar4C
001C3AC1 cmp [esi+0x4C], bl
001C3AC4 jnz 0x001C3AE5         ;4C!=0: não chama SetupNext/Progress
001C3AC6 mov edi, [0x00557F0C]
001C3ACC mov ecx, edi
001C3ACE call 0x00278270
001C3AD3 push ebx
001C3AD4 mov ecx, edi
001C3AD6 call 0x002779D0
001C3ADB mov byte ptr [esi+0x4C], 0x01
```

O byte é zerado no construtor `1C3C92` e por `1C2038`; é o indicador nativo de que a transição foi consumida. **Receita restrita demonstrada em asm:** no parceiro, prearmar+4C=1 antes do próximo Update12, com tipo1/3 e+26=0, impede as duas chamadas. Restaurar+4C=0 autoriza exatamente o consumidor nativo já mostrado. Guardar/restaurar só se mesma máquina/race/serial e se o mod ainda possui esse campo.

**Limite importante:** para tipo4 e outros tipos, o Update12 usa `1C20B0`, que chama `race.vtable+D4`; nas vtables comuns46B2D8/46A7D8/46B608 o alvo é `1D0770`, animação/câmera de apresentação. O byte+4C não bloqueia esse ramo nem callbacks independentes da UI.

## 4. Por que SM+44 e só filtrar A não bastam

**CONFIRMADO:** o Update9 possui esta barreira:

```asm
001C3A6F cmp [esi+0x44], bl
001C3A72 jnz 0x001C3AE5
001C3A74 mov eax, [esi]
001C3A76 push 0x0A
001C3A7A call [eax+0x04]
```

Mas o torneio já a usa e possui seu próprio avanço:

```asm
0026476C mov ebx, [0x00557EEC]
002647DE mov byte ptr [ebx+0x44], 0x01
002647E2 mov byte ptr [ebx+0x45], 0x01
; Update da tarefa266660 exige SM+4==9 em266906
; controla fases task+EC e widgets task+E4/task+E8
00266CE9 mov ecx, [esi+0xE8]
00266CEF mov edx, [ecx]
00266CF1 call [edx+0xA8]
00266CF7 test al, al
00266CF9 jz 0x00266D03
00266CFB mov ecx, [ebp-0x08]      ; SM
00266CFE call 0x001C2310
001C2310 mov eax, [ecx]
001C2312 push 0x0A
001C2314 call [eax+0x04]         ; Enter10 direto, ignora44
```

Não sobrescrever task+EC ou ponteiros dos widgets: fases anteriores acumulam pontuação de série, constroem ranking e widgets, processam recompensa e sincronizam animações. Repetir fases pode duplicar efeitos; pular fases pode deixar UI inválida.

Há callbacks de menus que fazem `278270 ->2779D0` diretamente (`16A810`, `16B730`, `16B790`, `16B7F0`), sem passar pelo Update12. Filtrar somente a tecla de farol já existente em ChallengeInput não constitui bloqueio desses comandos. Filtrar os controles de confirmação/volta/Start de resultado desde o primeiro estado terminal, preservando o controle do mod/F10, é uma proteção necessária; o conjunto exato de controles e bordas precisa corresponder ao mapeamento de input guest.

Não reenviar um A mantido no momento de liberar: exigir soltar e uma borda nova, ou injetar uma única confirmação específica pelo proxy já autorizado, se a UI e sua ação forem comprovadas. Nunca enviar teclas do Windows.

## 5. Estado13: no-op confirmado, uso como estacionamento não recomendado

**CONFIRMADO em asm:** a jump table de Update aceita estado13 e o despacha para `1C3B15`, que apenas desfaz a pilha e retorna. A entrada nativa13 chama `1C2C40`, com mudanças em players/câmeras; **não é necessário nem desejado chamar essa entrada** para a hipótese abaixo.

Foi considerado estacionar o seletor de Update em13 depois da entrada12. **Não implementar com a evidência atual:** outros sistemas podem interpretar13 como um modo diferente, e a entrada13 real tem efeitos nos players/câmeras. Não foi auditado o conjunto de consumidores que depende de estados exatos.

`SM+48==12` sozinho tampouco significa “entrada12 concluída”: `1C3B94` o escreve **antes** da entrada. A evidência de conclusão seria `+4==12 && +48==12` depois de uma entrada nativa na mesma thread, pois+4 só é publicado em `1C3C0E`, ao retornar da entrada. Essa propriedade não se aplica se outro código tiver alterado o seletor diretamente. Ela ajuda no diagnóstico/ownership, não autoriza o estacionamento13.

**Janela do hook:** o Update11 retorna após pedir12, então existe fronteira de chamadas entre a entrada12 e o primeiro Update12. Ainda é necessário comprovar que o proxy de XInput roda nessa fronteira em todas as variantes: um loop de frame pode executar mais de uma atualização sem consultar XInput. Guard+4C prearmado cobre o ramo automático1/3 mesmo antes de observar12. Filtragem de input cobre a UI; nenhuma das duas demonstra por si só que o experimento13 é interceptado a tempo.

## 6. Preparar a decisão do host e liberar pelo consumidor correto

**CONFIRMADO:** `SetupNextCareerRace278270` lê `career+2C`; se zero, deriva uma ação por `2780E0`. Portanto não deixar a ação do parceiro ser derivada de sua carreira divergente ao seguir a próxima corrida do host.

Para ação4 e+57=0, `2782D1..E1` chama `277AE0(u16 career+34)`. `277AE0` busca o registro, atualiza cidade/UniqueId na carreira, prepara NEXT por `26B8E0`, e seleciona recursos/adversários. Para ação3, o ramo `278315..20` chama `277350(career+30)`, que prepara cruise. `2779D0(false)` copia+2C para+24, executa as transições globais de carga correspondentes e limpa o pedido/id.

Planejamento de dados antes de liberar uma decisão:

1. Decisão deve trazer a serial encerrada, revisão/ordem e ação autorizada; retransmitir até ACK. Aplicar uma única vez, só à espera que possui aquela serial. ACK não deve depender de vitória.
2. Próxima corrida: receber novo Start com serial nova, registro válido/UniqueId/cidade e setup autoritativo. Instalar/sanitizar o registro pelo mecanismo existente antes de liberar carga. Preparar `career+2C=4`, `+30=city`, WORD+34=UniqueId, `+57=0`; não inventar ponteiros nem copiar o índice de lista do host como se fosse índice local.
3. Rua ou host foi a menus/garagem: a ação desejada é `career+2C=3`, `+30=cidade válida`, WORD+34=0, +57=0. Mas só liberar quando as guardas de tarefas da seção8 garantirem que não será sobrescrita; ter esses quatro campos não basta. Não conduzir o parceiro para a garagem do host. A escolha de cidade deve ser explícita no pacote e validada.
4. Para parceiro em estado12 **tipo1/3**, a liberação nativa comprovada é+4C=0, mantendo o seletor nativo12. Seu Update executa exatamente278270 e2779D0. Não aceitar o pacote se+26!=0: há outro fluxo de saída. Validar também a tarefa/cursor descritos na seção8.
5. Para resultado comum **tipo4/outros**, Update12 não executa essas funções. É necessário provar uma ação nativa da UI que consuma o pedido já preparado (ex.: callback16B7F0 faz ambas as chamadas sem novo Report), ou primeiro um fluxo seguro de retorno ao cruise. A injeção de confirmação só é candidata quando estiver comprovado qual widget/ação está ativa. **Não mudar CURRENT+18 para1 temporariamente:** esse campo participa de classificações e do ciclo de carga; o trace não prova segurança. Este ramo permanece fora da receita limitada.

O ACK de preparação deve vir antes de o host liberar a própria próxima corrida; caso contrário o parceiro pode ainda estar salvando ou sem o registro. Pode guardar Start/decisão enquanto o parceiro conclui11, sem disparar seu marcador. Se o host decidir antes de o parceiro chegar, é preciso política explícita de conclusão/espera que respeite o gate de vitória já existente.

## 7. Serial nova e testes necessários

O reset de série atual em race.cpp encerra a serial ao detectar objeto novo ou7+→2..6. O novo fluxo deve criar/enviar a nova serial **antes** da largada seguinte e vinculá-la ao registro/setup correto. Não permitir que um `BeginRace(0)` posterior apague um Start novo recebido para aquela próxima geração. Repetições da decisão antiga não podem liberar uma corrida nova, nem crédito/prêmio da corrida antiga serem aplicados a ela.

Testes sintéticos úteis: guard de+4C com tipos1/3 e+26=0; rejeição para tipo4; não alterar o seletor+4; liberar uma vez só; perda de ownership por nova entrada nativa; pacote de serial antiga; host sai durante autosave; timeout/desconexão restaura só campos ainda possuídos; novo Start chegando antes da conclusão11; BeginRace/objeto reutilizado em série; tarefa ativa que tentaria substituir pedido3/4; previsão da próxima etapa diferente da serial/registro do host.

Teste ao vivo ainda pendente e atualmente suspenso pelas regras de firewall: observar as fases7/8/9/10/11/12 e o hook; verificar prêmio/dirty/save antes e durante a espera; duas corridas seguidas de clube/torneio; ambos podem pressionar todos os controles enquanto parceiro espera; host volta à rua; host vai à garagem; parceiro desconecta durante11 ou aguardando12; retorno ao mundo sem duplicar Report/recompensa. Nenhuma dessas validações foi feita nesta análise.

## 8. Tarefa ativa pode substituir a decisão: guardas obrigatórias

**CONFIRMADO:** o gerenciador de tarefas é `tm=[[career+0C]+10]`. A coleção `[tm+78]..[tm+7C]` contém entradas de12 bytes; `tm+94` é o índice ativo, ou `FFFFFFFF` quando não existe. Cada entrada tem WORD de identidade em+0, segundo componente em+4 e ponteiro de tarefa em+8. Validar limites/alinhamento da coleção e do índice, não dereferenciar `-1`.

No torneio de vtable **470348**, os slots lidos do arquivo são:

| Slot | Alvo | Uso |
|---|---|---|
|+04|C6A80|retorna byte task+E0, elegível/pendente para ativação|
|+08|2390A0|escreve byte task+0C a partir do argumento|
|+14|264730|inicializa tarefa no novo mundo|
|+18|2658A0|libera recursos/objetos de UI da tarefa|
|+1C|266660|update da tarefa|
|+24|264C60|prepara a próxima etapa/setup|
|+28|264D80|recebe resultado da corrida|

`278270` chama `279210` antes de preparar ação3/4. `279210` só deixa de procurar outra tarefa quando `tm+94!=-1`; se não há ativa, procura uma com predicate vt+04 true, guarda seu índice e escreve active via vt+08(1). Portanto **sem tarefa ativa não basta** para garantir pedido3: pode haver uma pendente que seja ativada nessa própria chamada.

Depois do preparo explícito, `278270` sempre chama `279370`. Ele chama vt+24 da ativa, ou os vt+24 das tarefas cujo+0C==0 quando não há ativa. Compara `2783D0` antes/depois (retorna se+94==-1) e repete o processamento se essa condição mudar. É possível haver mais de um callback dentro da mesma chamada278270.

### Não há garantia de setupType3 em todas as etapas

`264C60` resolve o registro da próxima etapa e chama `277AE0` em `264D33..42`. Este chama `26B8E0`, que copia o tipo do descriptor da corrida para NEXT+18:

```asm
0026B9A8 mov edx, [eax]           ; descriptor da corrida, campo0
0026B9AF mov [esi+0x18], edx
00264D47 movzx ecx, byte ptr [esi+0x44]
00264D4B mov edx, [0x0053F2E4]
00264D53 mov [edx+0x40], ecx      ; tarefa altera limit; não força tipo3
```

Assim, a restrição de gate tipo1/3 deve ser checada em RAM por etapa. O código da tarefa não torna todas as etapas tipo3; a propriedade depende dos arquivos da série. Não extrapolar o suporte limitado só porque a corrida veio de um torneio.

### Próxima corrida: validar e seguir a seleção nativa, sem suspender a tarefa

Campos do torneio confirmados: task+0C ativo; task+E0 pendente; task+10 quantidade de entradas; task+14 array de entradas8; task+DC cursor; task+D5 guarda de pontuação local, task+D8 acumulado; task+48 tabela de pontuações dos adversários, com marcadores por entrada. O Report264D80 incrementa+DC em `264DA3` para resultados0/1. Não copiar/zerar os acumuladores para sincronizar uma largada.

Para uma decisão “próxima etapa”, uma receita **conservadora** é manter a tarefa nativa e só liberar quando sua previsão coincide com o Start autoritativo do host:

- tarefa ativa local é a esperada e de vtable470348, +0C!=0 e+E0!=0;
- cursor+DC está no intervalo `[0,task+10)`; entry atual `entry+4==0` representa registro normal, não segmento nomeado que usa277440;
- o WORD em entry+6 é o identificador usado por `2670A0`; `2632F0` resolve o registro por ele e retorna CarCareerRecord cujo WORD+4 é o UniqueId que264D3B/42 entrega a277AE0;
- registro resolvido, cidade e nome coincidem com o Start do host; configuração/mascara recebida está preparada antes do preload;
- o cursor da tarefa e os registros ainda são os mesmos ao liberar o byteSM+4C.

O callback pode processar pontuação/flags da etapa anterior (`264CBD ->264460`, `264CCE ->263340`) com guardas nativas D5 e marcadores da tabela. Não reexecutar ReportRaceResult para tentar ajustar o cursor. Se a previsão local não coincide com o host, recusar a liberação como série sincronizada; escrever somente career+34 não vence o callback264C60 que escolherá sua própria entrada.

### Rua: pedido3 é comprovado, força sobre tarefa ativa não é

`278315..20` para ação3 chama **277350**, não277300 (este pertence à ação1). Em277350:

```asm
00277367 mov dword ptr [esi+0x2C], 0x03
00277379 mov [esi+0x30], ebx      ; cidade
0027737C mov [esi+0x34], ax       ; zero
002773DF mov [edi], ebx           ; NEXT.city
002773E1 mov dword ptr [edi+0x18], 0x01 ; NEXT.cruise
```

Mas264C60 ativo pode chamar277AE0 de novo depois disso, substituindo NEXT por uma corrida. Para afirmar que a liberação de rua funcionará só com os quatro campos de carreira, é preciso garantir **nenhuma tarefa ativa, nenhuma elegível para ativação e nenhum callback de tarefa inativa que modifique o setup**. O último item exige conhecer as vtables presentes, não simplesmente contar a coleção. Em torneio ativo essa guarda falha; não anunciar o retorno à rua como resolvido por `2C=3`.

O caminho nativo de abandonar/desativar passa por `2907E0 ->279280`: chama activeTask.vt+08(0) e só então grava `tm+94=-1`. Funções do torneio fazem também ajuste de progresso/elegibilidade/cursor. Forjar somente o índice não reproduz esse abandono.

### Suspensão temporária: a supressão foi provada, a transação completa não

Existe uma combinação que faz o callback264C60 retornar sem alterar o setup: **preservar tm+94 apontando para a mesma tarefa** e guardar/escrever temporariamente **task+0C=0 e task+E0=0**. O ramo `264D58..5A` retorna para264CF9. Assim279210 não escolhe outra tarefa e279370 não reconstrói a etapa. Entretanto:

- só zerar+E0, mantendo+0C=1, executa264C7B..9B: desativa e reinicializa contadores;
- só zerar+0C, mantendo+E0=1, executa264D5C..77: reinicializa contadores;
- `278270` e `2779D0` são consecutivos no consumidor1C3ACE/AD6. O hook não possui uma fronteira garantida entre ambos para restaurar os bytes;
- a carga chama `2792B0` (chamador29337), que chama task.vt+14 em todas as tarefas. `264730` testa task+0C em26474C/55: se ainda zero, pula a inicialização da tarefa para a corrida nova;
- restaurar no Tick seguinte pode ocorrer tarde demais, e restauração de bytes não refaz a inicialização que foi pulada. Restaurar antes da liberação faz o callback voltar a sobrescrever a decisão.

Portanto **não implementar essa suspensão como receita segura ainda**. É necessário demonstrar um ponto natural de consumo em que NEXT já foi preparado mas Init ainda não começou, ou outro pedido nativo de abandono que preserve corretamente o estado da tarefa. Não usar setters/vtables guest nem patches para criar esse ponto sob as regras atuais.

**Entrega prática desta análise:** gate12+4C para tipos1/3 e+26=0, filtragem de controles de resultado e validação de próxima etapa que já coincide nativamente são sustentados pelo trace. Forçar rua/etapa arbitrária sobre torneio ativo e generalizar para outros tipos permanecem bloqueados pelas condições descritas, sem esconder essa limitação sob o pacote de decisão.
