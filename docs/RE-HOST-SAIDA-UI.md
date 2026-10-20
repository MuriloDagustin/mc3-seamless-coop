# Host decide: saída nativa para a rua pelo menu

Análise estática em 19/10/2026, na pasta principal `C:\dev\mc3-coop`. Não executei jogo, launcher, runner, firewall ou funções guest. Endereços são VAs do Xbox. Evidência: asm indexado em `out/xref.json` e dados de `assets/default.xbe`, lido como arquivo. **CONFIRMADO** significa fluxo demonstrado no binário; não significa teste ao vivo.

## Conclusão

**CONFIRMADO:** existe uma opção nativa `StartCruise` que faz o abandono completo da tarefa de torneio e retorna à rua. É a cadeia:

```text
item existente {contexto frontend, função1642C0, argumento1A, modo2}
  → confirmação de ação1A, tela6, Sim=chave4
  →164480 →1646F9 →16A340 (DoQuitToCruise)
  →277EE0(4) →tarefa264D80(resultado4) →264400
  →278270 →2779D0(0)
```

O mod pode navegar com DPad/A através do proxy de input guest e deixar a UI chamar os callbacks já instalados. Não precisa chamar guest, trocar function pointers, modificar cursor de tarefa nem escrever carreira+2C diretamente. A autorização de rede deve ser associada à serial da corrida terminada; a navegação deve revalidar a UI antes de **cada** pulso.

**Limitação confirmada:** o item não é construído em todos os contextos. Se ele estiver ausente, esta receita deve retornar pendente/indisponível. Não inventar o callback nem limpar os flags que impediram a criação do item.

## 1. Cadeia de ponteiros e tipos

```text
app      = [0050F148]
menus    = [app+0C]
frontend = [menus+0C]             ; F, estado da interface
mainList = [menus+FC]             ; M, vtable0045DAF8
mainPage = [menus+2C]             ; contexto do callback Continue
confirmPage = [menus+C0]          ; vtable0045DE48
dialog   = [menus+118]            ; vtable0045E0D0
dialogData = [dialog+60]
yesNoList = [dialogData+08]        ; vtable0045DFA8
```

**CONFIRMADO:** `156CBB..156CCD` instala `app+0C`; `156D03→1669A0` constrói `mainList` e o instala em `menus+FC` em `156D23`. O construtor1669A0 escreve vtable45DAF8 em1669E7. O objeto de confirmação é construído por168F50, vtable45DE48, instalado em1586B3 (`menus+C0`). O diálogo é construído por1698B0, vtable45E0D0, instalado em157588 (`menus+118`). A lista interna é construída por169220, que deriva de1669A0 e troca a vtable para45DFA8 em169245.

Não confundir `menus+FC` (ponteiro para lista) com `frontend+FC` (DWORD de ação pendente de confirmação).

Campos de frontend relevantes:

| Offset | Evidência / significado |
|---|---|
| +D8 DWORD | Tela atual, 5 opções pós-corrida, 6 confirmação. Consumido16A4EE..16A4FF. |
| +E0 DWORD | Guard de estado da UI;16A493 exige1 para input offline. Não é uma fila de pedido de tela. |
| +FC DWORD | Ação da confirmação;1642C4 escreve o argumento, aqui1A. |
| +100 DWORD | Texto/argumento auxiliar de confirmação, zerado por1642CA. |
| +220 byte | Inibe a criação de StartCruise;16C620..16C628. |
| +222 byte | Overlay de resultado da tarefa;1D01B0 equivale a SM9 e este byte igual0. |

**CONFIRMADO:** escrever apenas+D8,+E0 ou+FC não reproduz a transição.1642C0 grava a ação e chama163830(6), que faz entrada/saída dos widgets e1628C0. O método1628C0 despacha tela6 para1629E5 (`menus+C0`), chama sua entrada e só então grava+D8 em162D61.

## 2. Lista nativa: banco, seleção e callbacks

Layout compartilhado por mainList e yesNoList:

| Offset | Tipo | Uso confirmado |
|---|---|---|
| +230 | DWORD | Capacidade de itens por banco. |
| +234 | DWORD | Quantidade de colunas de texto. |
| +238+4*b | DWORD | Quantidade de itens do banco b. |
| +248 | ponteiro | Buffer de entradas,16 bytes por item. |
| +258 | DWORD | Banco sendo construído. |
| +25C | DWORD | Banco ativo/exibido; ler este para navegar. |
| +268+16*b | DWORD | Índice selecionado. |
| +26C+16*b | byte | Seleção válida. |
| +270+16*b | DWORD | Chave0 da entrada selecionada. |
| +274+16*b | DWORD | Chave1 da entrada selecionada. |
| +300 | DWORD | Estado da animação;0 estável.3..6 são transições em166B50. |
| +358 | byte | Fechamento já solicitado;0 aberto. |
| +44 | DWORD | Bit0 do widget ativo. |

Entrada i do banco b:

```text
entry = [list+248] +16*(b*[list+230]+i)
entry+0  DWORD chave0
entry+4  DWORD chave1
entry+8  ponteiro para células de texto
entry+C  DWORD flags
callback = list+EC+16*i          ; só i<20
callback+0  contexto/this
callback+4  endereço da função
callback+8  argumento
callback+C  modo de chamada
```

**CONFIRMADO:**348BE0 grava a entrada em348C38..348C67, copia o descritor de callback em348C11..348C35 quando i<20 e incrementa count em348D36.348B90 devolve o descritor.20ECE0 produz `{contexto,função,0,1}`;20ED00 produz modo2 quando o último bool é0.20ED50 despacha essas formas. Não chamar20ED50 pelo mod.

**CONFIRMADO:** seleção nativa348AF0 atualiza índice, valid e as duas chaves; pode chamar o callback de seleção embutido em `list+B4` quando muda o banco ativo (348B51..348B60), e marca dirty264/265. Portanto escrever somente o índice perde efeitos nativos. Preferir DPad pelo proxy. O helper `race_ui.inc` não escreve nenhum desses campos.

**CONFIRMADO:** em16A583..16A632, o eventoC0000BB8 lê o índice do banco+25C e executa seu descritor. Flag exatamente2 impede essa ativação;BBC/BBD são caminhos especiais para flags&2, que alteram o argumento do callback em−1/+1. O helper aceita somente alvos com flags0. Bit0 de flags é usado como não selecionável em348CFA..348D06; nunca navegar intencionalmente para item com esse bit.

## 3. Identificação do item correto

### Voltar à rua

**CONFIRMADO**, criação16C607..16C6A3:

```asm
0016C607 cmp dword ptr [0053F2D4],02
0016C620 mov al,[frontend+220]
0016C628 jnz 0016C6A8
0016C62E mov al,[config+04]             ; config=[app+98]
0016C633 jz 0016C6A8
0016C642 mov eax,[menus+0C]            ; frontend
0016C645 push 1A
0016C647 push eax
0016C64C push 001642C0
0016C658 call 0020ED00
0016C680 call 00348BE0
0016C685 push 0045D444                 ; ASCII "StartCruise"
0016C6A3 call 00348D70
```

Assinatura exata de leitura: callback `{frontend,1642C0,1A,2}`, entrada flags0. Não usar o texto traduzido ou um índice fixo para identificar o item: a quantidade/ordem depende do contexto.

### Continuar / próxima corrida

**CONFIRMADO:**16C368 carrega `[menus+2C]`;16C375..16C384 cria callback20ECE0 com função16B7F0. A assinatura é `{[menus+2C],16B7F0,0,1}`, não contexto frontend.16B7F0 fecha a lista, chama278270 e2779D0(0). O log literal45E4F4 identifica `DoMoveToNextRace`.

Outros callbacks investigados não equivalem a sair da série:

| Callback | Comportamento confirmado |
|---|---|
| 16A810 | DoResetRace; informa resultado3, reinicia. |
| 16B730 | DoMoveToNextHookmanRace;276FC0 escolhe corrida hookman. |
| 16B790 | DoMoveToNextSideQuestRace;277620 escolhe side quest. |
| 16B7F0 | Continua a progressão nativa. |
| 16A340 | DoQuitToCruise, caminho de saída desejado. |

Os itens específicos hookman16BC70/side16BDB0 só aparecem sem tarefa ativa (`2783D0`) e com os campos correspondentes no registro. Não selecionar esses callbacks como se fossem Continue geral.

## 4. Confirmação Sim/Não e guardas

**CONFIRMADO:**1642C0(1A) grava `frontend+FC=1A`,+100=0, e abre tela6.161CE0 lê a ação e despacha1A para161E3B; essa branch configura o diálogo por16A010 com máscara0C e seleção padrão8:

```asm
00161E3B test bl,bl                    ; raceSM>=7
00161E43 push 08                      ; seleção padrão = Não
00161E45 push 0C                      ; opções4 e8
00161E53 push 0045D444                 ; "StartCruise" no resultado
00162075 call 0016A010
```

169EA0 cria primeiro a opção com key4, depois key8, através de1694D0→348BE0. Ambas têm key1=0 e flags0. Os nomes localizados vêm de450CE4 e450CE0. A seleção padrão é feita por3499B0(key8,0,−1).

**CONFIRMADO:**169D84..169DE1 lê a seleção da lista interna e copia o DWORD chave0 do item para resposta+0C. Após o fechamento,169C64 emite4001E726;16A4F4..16A527 só processa como confirmação quando `frontend+D8==6`. A tabela original em16A640/16A64C mapeia resposta4 (e1) para164480, resposta8 (e2) para164410. Logo4 é Sim e8 é Não.

Guardas propostos antes de cada pulso:

1. Mesma sessão/serial e decisão do host ainda pendente; modo carreira2, corrida terminada7..12, sem cruise.
2. Tela5: frontend+E0=1, jogo pausado (`[557ED0]+9!=0`),+FC=0. Lista main com vtable45DAF8, ativa, aberta, estável. Encontrar exatamente um descritor alvo e flags0.
3. Tela6 **somente para sair**: frontend+FC==1A, confirmPage vtable45DE48 ativo, dialog vtable45E0D0 ativo, sem transição (`dialog+64==0`). Lista interna45DFA8 com exatamente duas entradas, keys4/8, key1=0, flags0. Não aceitar confirmação de garagem, reset, saída do jogo ou outro FC.
4. Banco ativo0..2; capacidade1..20; count dentro da capacidade; seleção válida e dentro do count. Confirmar que as duas chaves em+270/+274 correspondem à entrada selecionada. Validar todos os intervalos de memória antes de ler.
5. Emitir Up/Down para aproximar o índice do alvo. Só emitir A quando a seleção atual for o alvo e a assinatura acima continuar igual. Liberar botão entre pulsos e esperar a mudança nativa de estado; não manter A apertado atravessando telas.

**PALPITE / precisa teste:** esses guardas permitem usar o XInput guest existente para navegar todas as combinações reais de telas. Nenhum teste ao vivo foi feito. Disponibilidade/tempo da UI, ordem do gancho versus leitura dos controles e fronteiras de animação precisam de validação.

## 5. Por que o abandono nativo resolve a tarefa ativa

**CONFIRMADO:**164480 usa tabela de ação (`164884/1648E4`). Ação1A aponta para1646F9:

```asm
00164701 mov ecx,[menus+2C]
00164704 call 0016A340
00164709 mov ecx,frontend
0016470B call 001643A0                 ; fecha/restaura UI
00164711 mov [frontend+FC],0
```

16A340 contém a string `Racers before quitting to cruise (DoQuitToCruise)` em45E2EC e a sequência:

```asm
0016A369 push 04
0016A36B mov ecx,career
0016A36D call 00277EE0                 ; ReportRaceResult(4)
0016A374 call 00278270
0016A379 push 00
0016A37D call 002779D0
```

277EE0 escreve career+28=4 e chama2793C0(4) mesmo se o resultado anterior já existe (277F03 e277FC2..277FC6). Quando career+24==4, resultado0 ou4 prepara ação rua3, cidade do careerData+50 e id0 (277F58..277F8C). As branches de crédito de vitória exigem resultado0 em277FD8..277FE1 e277FF4..278001; resultado4 não repete esses créditos.

**CONFIRMADO:** o índice da tabela264E84 corresponde a resultado4→264DCE→264400, não ao reset3.264400 executa:

```asm
00264413 call 00276F20                 ; cidade da carreira
00264418 push 03
0026441C call 00276F10                 ; próxima ação rua
00264421 push 00
00264425 call 00276EF0                 ; flag carreira57
0026442C call 002907E0                 ; desativa a tarefa nativamente
00264433 call 00263540
0026443A call 002631B0                 ; reinicializa estado/cursor da série
00264441 mov [task+DC],0
0026444B call 00263210
00264451 mov byte ptr [task+E0],0
```

2907E0 usa o task manager e279280; este chama tarefa.vt+8(0) e limpa o índice ativo+94. Esta é a sequência nativa de cancelamento, com os efeitos de inicialização/reset preservados. É diferente de o mod zerar+94,+0C ou+E0 isoladamente. SetupNext278270 ainda pode processar outras tarefas conforme o jogo normal; não assumir que toda carreira/mode tem comportamento idêntico sem teste.

**Limite de crédito:**277EE0(4) também recalcula campos gerais/agregados e emite atualizações de carreira, como o caminho normal de saída. Não afirmar ausência de qualquer estatística secundária; o que o asm confirma é a guarda de crédito de vitória para resultado0. A sequência deve acontecer uma vez pelo caminho UI, sem repetir A após a transição.

## 6. Torneio em estado9: onde segurar input

**CONFIRMADO:**task manager=`[[career+0C]+10]`, índice ativo+94, array de entradas12 bytes em+78..+7C, ponteiro de tarefa na entrada+8. Torneio tem vtable470348; task+E4 é seu objeto de input; task+E8 é `TournamentResultsMenu`, vtable46FFF8, construído264886→263A30. Sua lista usa os mesmos campos+300,+358.

`1D01B0()` não significa “venceu”: retorna `raceSM+4==9 && frontend+222==0`. Nesse ramo,266D03..266D1A lê input0 via206880 e chama264640 quando pressionado.264640:

```asm
0026464B call [widget.vt+A8]           ; já fechado? então retorna
0026465D call [widget.vt+A4]           ; animando? então retorna
00264667 mov eax,[task+EC]
0026466D cmp eax,03
00264670 jnl return
00264672 inc eax
00264676 mov [task+EC],eax
0026467C jnz return
00264686 push 01
00264688 call [widget.vt+A0]           ; solicita fechamento quando chega3
```

EC é colocado em1 em2669E7. Primeiro A pode avançar1→2; segundo2→3 solicita fechamento.266B0F..266C41 monta a segunda página enquanto EC2. O fechamento usa166B30→34ACC0, que seta widget+358=1 e estado de animação4/6. A abertura166F60→34ABE0 zera+358; o construtor inicialmente coloca+358=1.

**CONFIRMADO:**widget.vt+A8=348850 é `!vt+A4() && widget+358!=0`; vt+A4=166B50 testa+300 em3..6.266CE9..266CFE, quando o widget termina de fechar, chama1C2310 (Enter10) **sem novo botão**. Portanto esperar EC>=3 para “permitir A” chega tarde. É necessário bloquear A desde o início do resultado9 e só emitir os pulsos depois da decisão do host. Se+358 já é1 por uma autorização anterior, filtrar input não desfaz a autorização.

Não apareceu um temporizador que escreva+358=1 espontaneamente: no índice, as escritas relevantes são o construtor e34ACC0. Isso apoia o bloqueio por input para este caminho de torneio, mas não prova todos os tipos de tarefa/modalidade.

**Restrição do helper:**`CanAdvanceTournamentResult()` só aceita SM9, frontend+D8=1,+FC=0,+222=0, tarefa ativa470348, EC1 ou2, widget46FFF8 ativo/aberto/estável e input com flags0x201 (as guardas206880). O overlay+222!=0 usa outro ramo, autorizado separadamente por `CanDismissTournamentOverlay()`. Ausência de retorno true deve manter pendente, sem forçar campos.

**CONFIRMADO, fechamento separado do overlay:**266D24..266D2F exige `frontend+222!=0`.266E3C..266E4F lê input0 de task+E4 por206880 e chama264100.264100 faz exatamente:

```asm
0026410B mov byte ptr [frontend+222],00
00264112 mov edx,[task+E8]
00264118 mov eax,[edx+58]
0026411B mov dword ptr [eax+20],01
00264122 mov ecx,[task+E8]
0026412A push 00
0026412C call [widget.vt+B0]           ; reset da lista
```

Este consumidor **não consulta nem modifica task+EC**; não reutilizar a regra EC1/2 do avanço normal para identificá-lo. O helper separado exige modo2/SM9, frontend página1/+FC0/+222 exatamente1, tarefa470348 ativa, inputflags0x201, widget46FFF8 ativo/aberto/estável e ponteiro de descritores `[widget+58]` mapeado até+23, destino da escrita nativa+20. Ele só lê, não zera222, não escreve o descritor nem emite A. Street/Next podem autorizar esse fechamento após decisão explícita do host através de um OR separado no controlador; isso não autoriza confirmações da página6 nem avança a serial.

## 7. Artefato preparado e validação necessária

`mod/src/race_ui.inc` contém somente planejamento por leitura: `UiPlan`, `PlanResultMenu(bool,UiPlan&)`, `CanAdvanceTournamentResult()` e `CanDismissTournamentOverlay()`. Ele devolve Up1, Down2 ou A1000 e a identidade da lista/página. Não injeta input, não chama guest e não escreve memória do jogo; o controlador de rede/input é responsabilidade da integração emrace.cpp. O contexto Continue foi validado contra16C368..16C384.

Não compilei este arquivo isolado: ele depende dos helpers internos de race.cpp e precisa ser incluído pela integração. Recomenda-se harness sintético de memória para páginas5/6, defaults No8, assinatura errada/duplicada, item desabilitado, banco inválido, seleção incoerente, confirmação de outra ação e torneio EC1/2 versus3/fechamento. Depois, quando os testes silenciosos forem novamente autorizados, validar: série espera em9; Continue só após host; rua via1A/Sim; índices ativos/cursor reinicializados pelo nativo; crédito e prêmio únicos; perda/overlay pendente reportados sem forçar saída.
