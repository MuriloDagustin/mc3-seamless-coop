# Handoff para o Codex — co-op do MC3 (18/10/2026, ~14h30)

## Worktree de sincronização — atualização posterior

O usuário pediu trabalhar sincronização de reboques/acidentes numa worktree separada e depois autorizou integrá-la. Os dez arquivos foram integrados na principal `C:\dev\mc3-coop`, branch `coop-noite`, a partir da worktree `codex-sincronizacao-trafego` baseada em `dc25ed3`. O ZIP já entregue permanece intacto. Estado e validação: `docs/RE-SYNC-TRAFEGO-FISICA.md`; evidência asm: `docs/RE-SYNC-TRAFEGO-ASM.md`.

Implementação experimental opt-in `MC3COOP_TRAFFIC_PHYSICS=1`, protocolo de sessão5 incompatível com o ZIP de protocolo4. Replicação host→parceiro de reboque e dois corpos existentes, proxy quando não há corpo, captura no gancho da thread do jogo, publicação de ciclos completos e restauração validada em RAM. Colisões reais, grade física e impactos vistos somente pelo parceiro NÃO foram validados/resolvidos por esta etapa. Não distribuir este build como correção comprovada. A suspensão de jogo/UAC/firewall continua valendo. Sem commit/merge/push nesta etapa; integração feita nos arquivos de trabalho após conferir a principal limpa e a mesma base.

## Carro remoto — continuação após a integração

Implementada a instalação da configuração do mesmo modelo uma vez por revisão/objeto, preservando vtable e máscara local, marcando `3F7F` para consumo nativo e atualizando também `vehicle+64`. Recepção ordenada de configurações independente das poses. `car::Tick` saiu da thread de sessão e roda no gancho antes de `race::Tick`. Modelo diferente fica pendente; só NEXT independente recebe os dados para a próxima carga nativa, preservando a base CURRENT e protegendo aliases locais. Diagnóstico `car_model_pending`. Testes de memória passaram; visual/peças no jogo ainda não verificados.

**Trocar o modelo inteiro sem recarregar o mundo continua NÃO implementado.** Investigação não encontrou pedido por slot consumido pelo jogo: factory temporária, updates não recriam, categoria3 destrói corrida/OppMgr/jogadores/tráfego, showroom não instala em player. Não zerar `player+14`, não forjar ponteiros, não chamar funções guest e não marcar loader carregado=false como se fosse request. Detalhes: `RE-TROCA-CARRO-IMPLEMENTACAO.md`, `RE-TROCA-CARRO-FACTORY.md` e `RE-TROCA-CARRO-RECURSOS.md`. Bit80 não é pedido de modelo; o setter271E00 escreve E0/E1.

### Prioridade do usuário em19/10: troca integral sem recarga

Depois do commit local9663375 (fluxo de resultado), o usuário priorizou explicitamente esta troca. Nova auditoria estática incluiu métodos virtuais do veículo/jogador e pacote nativo de configuração por rede; não foi identificado pedido de reconstrução individual. O pacote44 escreve somente NEXT; o update virtualFFD90 consome dirty80 para componentes existentes, sem chamar a fábrica humana. Não confundir esses caminhos com hot swap implementado.

`PLANO-TROCA-CARRO-NATIVA.md` apresenta alternativa de investigação: ponte para rotinas nativas, ABI da versão atual e transação completa de recursos/referências. Isso depende de ampliar a restrição vigente “não chamar funções guest”; nenhuma chamada ou alteração de runtime foi feita. Não afirmar impossibilidade geral nem prometer que permitir chamadas basta. Modelo diferente permanece pendente para carga normal. Nenhum jogo, launcher ou firewall aberto nessa investigação.

**Autorização posterior do usuário:** respondeu “Investigar a ponte nativa”. Prosseguir com investigação da ponte e ABI; não repetir essa pergunta. Isso amplia o escopo da investigação acima, sem revogar a suspensão de jogo/testes ao vivo/firewall e sem provar viabilidade da troca. `tools/inspect_native_bridge.py` faz inspeção estática dos arquivos PE (não os carrega nem executa); seus9 testes passaram e os runners atual/antigo tiveram tabela de20302 funções identificada. Ainda não é uma API de chamadas nativas nem hot swap implementado.

Responda em português. Trabalhe em `C:\dev\mc3-coop` (branch `coop-noite`). Commits locais podem ser feitos ao fim de cada item testado. **Nunca faça push.**

## Interrupção por avisos de firewall — atualização Codex

O usuário relatou avisos repetidos de firewall durante os testes. O Codex encerrou somente suas próprias cópias e suspendeu testes ao vivo. **Continuar apenas offline enquanto não houver caminho de teste silencioso comprovado. Não abrir jogo, pedir UAC nem criar/alterar regras de firewall nesta etapa.** A causa identificada na infraestrutura foi copiar MC3Runner.exe para um caminho novo por rodada: regras do Windows são associadas ao caminho. Não repetir esse padrão. Os harnesses passam a verificar regras já existentes, somente leitura, e recusam iniciar o jogo quando não há autorização prévia adequada; não contornam a decisão do Windows. Produto continua oferecendo firewall uma vez ao usuário final conforme a tarefa original; testes não devem provocar essa oferta.

## Regras fixas (do usuário, não negociáveis)
- Nunca feche, mate ou mexa no jogo dele nem no `xrevive.exe`. Nunca mate processo pelo nome. Só feche cópias de teste que você abriu: use o arquivo `D:\dev\mc3-coop-data\xrun\stop-tests` ou confira a linha de comando, que precisa conter `mc3-coop-data`.
- Testes sempre mudos, num desktop invisível (`tools\xrun.py` / `tools\pair.py` já fazem isso). Não mande teclas do Windows e não roube foco.
- Não altere nada em `D:\Jogos` (só leitura). A pasta de jogo de teste é `D:\dev\mc3-coop-data\testgame\...`.
- Nada de arquivos do jogo nem do `coop-online.toml` (credenciais) no git. O zip tem credenciais: uso local, não publicar.
- Não diga que funciona sem ter testado. Registre o que testou e como em `docs/TESTE-ISOLADO-2026-10-18.md`.

## Estado recebido de Claude (histórico; base já consolidada em a0a472a)
Testado ao vivo com 2 cópias (host 45703, parceiro 45704) no **build novo do xrevive** (`0x6AD5001B`):
- carro do parceiro no mundo do host, sem tela dividida; chegada sem travar;
- largada comum (o parceiro larga 0,3 s depois do host);
- vitória para os dois e prêmio;
- tráfego: 48 de 48 carros, erro máximo de 0,3 m.

Mudanças desta sessão que **ainda não estão em commit** (compilam; o pacote foi gerado):
1. **`mod/launcher/launcher.cpp` e `tools/xrun.py`:** o xrevive mudou o nome do runner para `Midnight Club 3 DUB Edition Remix.exe` (antes era `.xrevive-<sha>.exe`). Agora a busca aceita qualquer `.exe` de nível superior com 20 MB ou mais.
2. **`mod/src/race.cpp`:** quando a corrida do host já existe na carreira do parceiro, o parceiro empresta os campos `+0x0B..+0x4B` do registro do host para essa largada (adversários e dificuldade) e devolve os originais depois que a corrida carrega. Os tipos de tarefa (`+0x1E`) e o flag de "nova" ficam como eram.
3. **`mod/src/guest_poll.h`:** o ponto de chamada do gancho de jogo agora é achado pela tabela de importação (`call [rip+iat]` para `xinput1_4` ordinal 2). Já está no commit `a10a532`.

Faça o commit dessas mudanças primeiro.

## Receita histórica de teste ao vivo (suspensa após os avisos de firewall)
```
cmd /c mod\build.cmd                      (DLL do mod; mod\package.cmd gera out\MC3Coop-test.zip)
python tools\pair.py                      (2 cópias até o cruise, ~2,5 min; reinicia as anteriores)
python tools\traffic_test.py 15           (tráfego host x parceiro)
python tools\race_start_test.py           (host vai a uma corrida aberta; parceiro deve largar junto)
python tools\ai_test.py 3                 (posição de cada adversário host x parceiro)
python tools\race_finish_test.py          (leva o host à chegada por teleporte e lê os dois)
python tools\test_coop_memory.py          (testes de memória simulada, sem abrir o jogo)
python tools\ctl.py <porta> state|shot <nome>|players|peek <addr> [n]
```
`pair.py` usa os saves descartáveis `D:\dev\mc3-coop-data\xsave\i3` e `i4`. A DLL nova só entra quando as cópias são reiniciadas.

## Tarefas, em ordem

### 1. IA dos adversários igual nas duas cópias (critério validado localmente em 18/10)
- **Atualização Codex:** implementados setup autoritativo e resultados; teste5/5 com chegada passou. Ver TESTE-ISOLADO-2026-10-18.md. Texto abaixo preserva a investigação original. Próximo item: pacote.
- **Sintoma:** `tools\ai_test.py` mostra `ai=layout_mismatch`. Os pacotes `kAi` chegam (received=5), mas o parceiro recusa.
- **Causa encontrada:** a máscara de adversários do setup difere. No host, `[[0x53F2E0]+0x3D4] = FFFFFF1F` (5 adversários, `race+0x18 = 5`); no parceiro, `FFFFFF0F` (4). Os nomes também diferem (vêm do pool de nomes, `0x1D15B0`), e o hash de layout em `ai.cpp` inclui o nome (`progress+0x5C`).
- **O que fazer:**
  - **(a)** Incluir no `race::Start` (o pacote do host) a máscara `setup+0x3D4` e `setup+0x40`, lidas do setup NEXT `[0x53F2E4]` assim que ele estiver preenchido. Isso fica a ~3 s do trigger: espere `[[0x53F2E4]+0x68]` ser o nome da corrida. Se precisar, mande um segundo pacote, `kRaceSetup`, depois.
  - **(b)** No parceiro, escrever essa máscara nos dois setups (`[0x53F2E0]` e `[0x53F2E4]`) depois que o jogo dele montar o setup e antes de o player manager e o OppMgr serem criados (`OppMgr::Init 0x1E43C0` usa `race+0x18`). O mapa está em `docs/RE-MAPA.txt` (ai-traffic): número de adversários = popcount de `setup+0x3D4` sobre os adversários do arquivo da corrida (`0x26B0C0`).
  - **(c)** Tirar o nome do piloto do hash de layout em `mod/src/ai.cpp` (`ReadPool`, `HashString(... progress+0x5C ...)`). Os nomes são cosméticos, e a ordem e os pontos de largada já identificam cada adversário. Outra opção: o host manda os nomes e o parceiro escreve.
- **Critério de pronto:** `ai_test.py` com `ai=experimental applied=N`, a distância por adversário abaixo de ~5 m, e o host vendo a mesma posição de chegada que o parceiro.
- **Referências:** `docs/RE-IA.md` e `docs/RE-IA-IMPLEMENTACAO.md`. O bloqueio do cérebro da IA é o bit `car+0x15C` 0x10, já em `ai.cpp`.

### 2. Pacote para o amigo (gerado; smoke completo bloqueado pelo xrevive aberto)
- **Atualização Codex:** build/package, unitários e --logs do EXE extraído passaram; duplicata foi corretamente recusada. Carga completa/painel aguardam fechamento manual do xrevive pelo usuário. Ver relatório de teste.
- Testar o zip de verdade com `tools\test_launcher.py`. Ele extrai o zip numa pasta de teste e força `MC3COOP_GAME` para a pasta de teste.
  - **Atenção:** a pasta de teste tem o runner ANTIGO. Copie o runner novo (`D:\Jogos\...\Midnight Club 3 DUB Edition Remix.exe`, só leitura) para `D:\dev\mc3-coop-data\testgame\Midnight Club 3 DUB Edition Remix\`.
  - Confira que o lançador acha o runner novo, que o mod carrega e que o F10 aparece.
- Atualizar `mod/README-JOGAR.txt`: o estado da IA e o nome novo do runner.

### 3. Vitória tardia (implementada; vitória2s/perdas locais passaram, timeout ao vivo pendente)
- **Atualização Codex:** proteção padrão para46B2D8/dois humanos, Finished20bytes/protocolo4, contexto humano real para ranks. Win2s e derrota dos dois passaram ao vivo; ramo15s/metadata posterior passou só em RAM simulada. Não retomar jogo enquanto houver risco de avisos de firewall. Ver relatório.
- O projeto está em `docs/RE-VITORIA-TARDIA.md`: adiar o primeiro `ReportRaceResult` e mandar também resultado de derrota.
- Implementar só para o circuito comum (vtable `0x46B2D8`) e testar com `race_finish_test.py`, fazendo o parceiro chegar primeiro no próprio mundo.

### 4. Pendências menores (análise registrada; mudanças de runtime ainda não implementadas)
- **Análise:** docs/RE-PENDENCIAS-MENORES.md explica máscara dirty do CarCfg, reconstrução de modelo ainda sem pedido de dados comprovado e candidato C4 para trailers. Não remover guardas nem anunciar essas propostas como implementadas.
- Caminhões de tráfego (`+0xEC` reboque) ficam de fora: escondê-los ou sincronizá-los.
- Troca de carro do parceiro sem recarregar o mundo.
- Outras modalidades de corrida (eliminação, pontos, perseguição).

## Endereços úteis (guest Xbox; host = base + VA)
- Player manager `[0x558814]`: players em `+0x08`. Carro: `player+0x14 → vehicle`, `vehicle+0x08 → car`, `vehicle+0x1C → phInst` (matriz em `+0x10`).
- Setups: `[0x53F2E0]` (atual) e `[0x53F2E4]` (próximo). Neles: humanos `+0x4C`, views `+0x50`, máscara de adversários `+0x3D4`, nome da corrida `+0x68`.
- Corrida atual: `[[0x533E24]+4]`. Máquina de estado: `[0x557EEC]+4` (5 largada, 6 correndo, 7 chegada, 11 autosave, 12 fim).
- Carreira `[0x557F0C]`: `+0x0C` dados da carreira, `+0x2C == 4` quando há largada pedida, `+0x34` id da corrida.
- OppMgr `[0x534074]`: `+0` total, `+4` competidores, `+8` array com entradas de 0x43C.
- Tráfego: `tm = [[0x50152C]+8]`, pool em `tm+0x130`, slots de 0x21C bytes.

## BUG CRÍTICO (teste real, 18/10 22:55): o parceiro crasha ao conectar — CORRIGIDO em 780a698 (ver abaixo)
- Log do amigo (runner novo `linked=0x6AD54432`): AV em `MC3Runner.exe+0x129F2E8`, ~10 s depois de `joined ... as player 2` e `car: slot 1 partner car vp_jetta_03`.
- Pilha do guest: `sub_002BFE50+0xA7A` (ctor do player) → `sub_002BF920+0x1B6` (objeto em player+0xC8, chamado SEMPRE pelo ctor em 0x2C007C) → `sub_0021AC90+0x28E` → `sub_0021A280+0x708` → `sub_00217F20+0xA8`.
- No crash: `edi = 0x501238`, a string `"Hud_MC3_GARAGE:HUD_HAND_OF_GOD"`; `esi = CDCDCDCD` (não inicializado). O HUD ativo era o da GARAGEM quando o jogador 2 foi construído.
- **Hipótese:** `car::KeepSetups` escreve `humanos = 2` nos setups assim que há um parceiro com CarCfg, inclusive enquanto a cópia está na garagem ou no front-end (a garagem constrói players por `0x2CBAE0`). Montar o player 1 nesse contexto quebra.
- **Correção pedida:** só escrever `+0x4C = 2` e a vaga 1 nos setups quando a cópia está carregando ou dentro de um mundo de carreira (cruise ou corrida). Nos menus e na garagem, manter 1 humano; revisar também o fluxo de saída da garagem. Depois testar: conectar, entrar na garagem, trocar de carro e sair, com carros de modelos diferentes nos dois lados (um tC e um Jetta, por exemplo).
- Antes disso, o usuário foi orientado a conectar no menu principal, dar Start Career direto e não entrar na garagem conectado.

## Correções de Claude, 18/10 noite (commits a922716 e 780a698, já no GitHub `main`)
- **5 fps com parceiro** (a922716): `Mapped()` chamava `VirtualQuery` centenas de vezes por tick (25 ms no `car::Tick`). Agora guarda as regiões por 20 ms por thread; medido 160–200 fps conectado. O `state` mostra `tick_us` por subsistema.
- **Crash na garagem** (780a698): `car::KeepSetups` só pede o 2º humano com `[0x53F2D4] == 2` (carreira: rua e corridas). Garagem = 5, front end = 3. Reproduzido nas cópias de teste (pausa → Go to Garage → Yes): `ICALL unresolved target 0 at 0x6543B` e `FATAL unproven mid-entry PC 0x16002A`, o mesmo do log do amigo. Com a correção, entra e sai da garagem e volta com os 2 players.
- **Série de corridas** (780a698): `race::Tick` detecta corrida nova carregada sem largada da rua (outro objeto de corrida, ou o mesmo endereço voltando de 7+ para 2..6) e encerra a serial (`BeginRace(0)`, `ai::SetRace(0,...)`). Antes, a vitória/trava de chegada da 1ª corrida passava para a 2ª: "Time Up" num e "travado" no outro.
- Testes: `tools/test_coop_memory.py` passa (inclui garagem/front end e reset de série). `race_start_test.py` + `ai_test.py --finish`: largada, vitória compartilhada e prêmio OK. Pré-existente (falha igual na versão anterior): ordem final da IA às vezes difere (2 posições trocadas) e `ai=layout_mismatch` em `ordered\hard\sd_ordered_hard_cu`.
- Transcrição da sessão real do usuário com o amigo: `C:\Users\muril\Videos\NVIDIA\Desktop\transcricao-2026.10.18-22.33-large.txt`.

## TAREFA NOVA (prioridade 1): o host conduz, o parceiro só acompanha
Pedido do usuário: "só eu como host deveria poder iniciar as corridas e prosseguir, e meu amigo deveria só ficar esperando até eu fazer algo, senão vira essa confusão". Na sessão real, depois da 1ª corrida de uma série, cada jogo seguiu sozinho (um foi para a próxima corrida, o outro ficou esperando um "sprint de três voltas") e os dois ficaram presos.

### Regras
1. **Rua:** o parceiro NÃO inicia corrida por conta própria. **Hoje NADA bloqueia isso** (confirmado no teste real: o amigo largou uma corrida sozinho e o host outra, cada um correu a sua, e um afetou o placar/classificação do outro, porque cada mundo tem o carro do outro como 2º humano da corrida). O que existe é só o caminho do host: `kRaceStart` → `race::PartnerTick` arma `cruise+0x1250` e põe o carro do parceiro no marcador. Falta: os marcadores/hookman do parceiro não dispararem fora dessa largada (bloquear também desafios de hookman/rivais da rua). Corrida que o próprio host larga sozinho continua indo para os dois.
   - Enquanto um estiver numa corrida que o outro não está, o carro do ausente não pode contar na classificação nem no fim da corrida (hoje entra como racer remoto completado/DNF em `race::CompleteRemoteRacers`, e o pose dele vem de outro mundo).
2. **Fim de corrida:** o parceiro fica na tela de resultado (estados 7–12) até o host decidir:
   - host volta para a rua → parceiro volta para a rua;
   - host segue para a próxima corrida da série (torneio/clube; `mcTkTournament` 0x264D80; `SetupNextCareerRace` 0x278270 chamado no Update do estado 12, ver `docs/RE-CORRIDA.txt` linha 9) → parceiro entra na MESMA próxima corrida com nova serial (o host manda `kRaceStart` da seguinte; vitória compartilhada vale para ela também);
   - host sai para garagem/menus → parceiro volta para a rua.
   O avanço nativo do parceiro (A no resultado, autosave, próxima corrida) precisa ser segurado até a decisão do host. Avaliar no RE: segurar o estado 12 de `[0x557EEC]` (impedir o Update de 12 de chamar `SetupNextCareerRace`) ou filtrar o A do parceiro nesse estado (`ProxyXInputGetState` já altera o input do guest, ver `race::ChallengeInput`).
3. **Garagem:** o parceiro pode ir à garagem dele estando na rua (não crasha mais). Se o host largar nesse meio tempo, avisar no F10/HUD do parceiro ("o host iniciou uma corrida — volte para a rua"), ou levá-lo de volta se for seguro.
4. **Créditos:** inalterados (vitória de qualquer um conta para os dois; corrida não liberada também conta para o parceiro).
5. Pacote novo provável: host → parceiro "decisão pós-corrida" (rua / próxima da série / menus) com a serial terminada.

### Critérios de pronto (2 cópias de teste, regras de sempre: ocultas, sem som, só fechar processos com os parâmetros de teste)
- Parceiro na rua num marcador com farol: nada acontece. Host larga: os dois correm.
- Série de pelo menos 2 corridas: os dois terminam a 1ª, o parceiro espera; host avança; os dois correm a 2ª juntos, com vitória compartilhada nas duas.
- Host volta para a rua após corrida comum: o parceiro volta junto.
- Parceiro na garagem quando o host larga: sem crash e com aviso.
- `tools/test_coop_memory.py` passa e ganha testes sintéticos do bloqueio de largada e da espera pós-corrida.

## Continuação Codex, 19/10: prioridade1 parcialmente implementada

- Fontes na pasta principal, sem commit/push. Preservadas as correções de garagem/desempenho de780a698/a922716 e o pedido acima.
- Implementados: bloqueio de largadas próprias do parceiro na rua (`cruise+0D`, posse/restauração restritas), aviso F10/HUD se houver largada pendente na garagem, fases de marcador protegidas por serial/mundo e reset por geração de rede.
- Protocolo6: posição identifica rua/corrida, cidade, índice e serial. Recepção, aplicação e F8 recusam outro contexto; interpolação antiga é descartada preservando configuração. Prêmio/resultado de outra identidade não são importados. Os dois lados precisam do mesmo build; o ZIP dc25ed3 continua antigo e não foi regenerado.
- Largada pendente e setup pendente separados da corrida ativa: novo pedido enquanto ainda termina a anterior preserva gate, serial, metadata e prêmio anteriores. Isso não implementa sincronização de série; apenas evita destruir o resultado anterior prematuramente.
- Ainda faltam: espera de resultado/decisão do host, próxima etapa com serial nova, retorno à rua com tarefa de torneio ativa e exclusão nativa completa do ausente. `RE-HOST-FLUXO-RESULTADO.md` documenta os consumidores que sobrescrevem a ação e por que suspender apenas bytes da tarefa pode pular a inicialização da carga. Não usar estado13 nem zerar índice de tarefa como atalho.
- Verificação offline: `tools/test_coop_memory.py` passou com testes novos de rua/fase/posse, contexto/posição e isolamento de crédito, mais os testes existentes de IA, chegada, tráfego e configuração. Nenhum jogo/launcher, janela, rede ou firewall foi aberto para essas validações.
- Mapas novos: `RE-HOST-BLOQUEIO-LARGADA.md`, `RE-HOST-PRESENCA-CORRIDA.md`, `RE-HOST-FLUXO-RESULTADO.md`. Detalhes dos comandos e limites no relatório `TESTE-ISOLADO-2026-10-18.md`.

## Continuação Codex, 19/10: fluxo pós-corrida implementado offline

- A etapa anterior foi commitada localmente em `981aaa2`, sem push. Este registro substitui o status histórico acima sobre ausência de implementação do resultado.
- Protocolo7: decisão Wait/Street/Next por serial/revisão; histórico de8 decisões e setups retransmitidos. Parceiro espera mesmo sem receber Wait. Input do guest0 é filtrado; saída e avanço usam menus nativos identificados, com confirmação Sim e preservação de recompensa/autosave.
- Host detecta nova etapa carregada e publica identidade/serial nova. Parceiro valida seu cursor por identidade, não pelo UUID do save. Cursor diferente usa saída nativa para a rua e largada pendente. A serial anterior só é abandonada quando a confirmação nativa de Continue é entregue.
- Build DLL e suíte sintética completos passaram. Não foi aberto jogo/launcher nem firewall; a suspensão dos testes ao vivo permanece. Não foi gerado pacote novo. README atualizado para protocolo7; ZIP dc25ed3 continua antigo.
- Evidência e limites: `RE-HOST-NEXT.md`, `RE-HOST-SAIDA-UI.md`, `RE-HOST-FLUXO-IMPLEMENTACAO.md`. Próximo passo da prioridade1: validar os critérios de duas cópias quando os testes ao vivo forem retomados, especialmente série2+ etapas e retorno à rua. Não tratar o fluxo como comprovado no jogo.
- Pendências de implementação preservadas: exclusão nativa do ausente da classificação; troca de modelo sem recarga. Não zerar contadores/ponteiros da corrida nem reconstruir veículo por escrita especulativa.

## Continuação Claude, 19/10: contexto do mutex da ponte nativa (offline)

- Continuei a investigação autorizada da ponte a partir do ponto em que o Codex parou. Estendi `tools/inspect_native_bridge.py` para validar, por bytes e de forma reproduzível, o contexto estático do gancho: delimitação de funções por `.pdata`, referências RIP-relativas, grafo de chamadas diretas, unwind e alcance do mutex do callback.
- Resultado na build `51354a04…` (`6AD54432`): o mutex `1427E9EE8` só é referenciado dentro de `3105B0`; o callback `31B690` a chama e libera. O caminho direto do input (`300D00` → `3068D0`/`306BB0`) e 13 ancestrais diretos não referenciam nem chamam essa aquisição. Os unwind de `3AD270`/`39EFA0` seguem versão 1, flags 0.
- Isso remove o mutex já detido como deadlock inevitável **nessa cadeia direta**, mas NÃO prova reentrância do callback durante o HLE, chamadas indiretas, exceção/longjmp, stack scratch, vida útil da CarCfg, capacidade dos buckets ou perfil de habilidade. `execution_safe`/`native_calls_enabled` continuam false; nenhuma função guest foi chamada.
- `python -m unittest discover -s tools/tests -p test_native_bridge.py -v`: 12 testes PASS (9 anteriores + `.pdata`/unwind, RIP-relativo e grafo por dono). `python tools/inspect_native_bridge.py D:\dev\mc3-coop-data\xrun\MC3Runner.exe` passou; a cópia `testgame` (`6AD5001B`) não tem perfil de runtime.
- Próximo passo técnico continua sendo provar reentrância/lifecycle; isso exige uma chamada inofensiva em execução isolada autorizada. A suspensão de testes ao vivo/firewall permanece e não foi revogada por esta etapa. Nada de ZIP novo, push ou alteração de runtime.

## Continuação Claude, 20/10: ponte validada e resultado da troca de modelo

- `mod/src/native.cpp` implementa a sonda opt-in `MC3COOP_NATIVE_PROBE=1`: valida build/contexto (`.pdata`, globais, KPCR/KTHREAD, stack), chama `3308C0` e `2BD910` pelo wrapper full-save e compara registradores/x87/xmm/flags. Ao vivo, duas cópias, cinco chamadas, `probe PASS`, `mismatch=0`; o jogo continuou e carregou o mundo. Isso valida resolução, argumentos, ECX/thiscall, retorno e preservação de contexto no gancho.
- A construção de um modelo diferente foi exercitada. `2CB270`/`2CBC40` rodam, mas `105B80` retorna 0: `105AA0` tenta criar o recurso e o **release** responde `Cannot make car resource on RELEASE ... not in the assets!`. `2CBC40` então segue com produto nulo e o runner encerra em ICALL não resolvido. O pool de carData só tem o modelo em uso em modo 0; o modo 1 (garagem/showroom) não é reutilizado no offline. **Troca instantânea de modelo em mundo carregado é impossível nesta build release.**
- `SwapRemote` final apenas faz o preflight e recusa antes do construtor, mantendo o carro antigo. A configuração diferente continua staged em NEXT e é aplicada no próximo carregamento normal. Testado ao vivo: parceiro trocou a garagem para Eclipse; host ainda mostrava tC; após `gorace`/carga da corrida, `live slot 1` passou a Eclipse, com as duas cópias conectadas e na mesma corrida. Sem reconectar.
- Comandos de diagnóstico adicionados ao canal de teste: `cars`, `garage`/`swapcar`, `swapfrom`, `localcar`. O teste usou `localcar` para simular a escolha da garagem, não a UI real.
- `python tools/test_coop_memory.py`: PASS com `native.cpp` incluído e `Env` stub; `cmd /c mod\build.cmd`: PASS. Nenhuma regra de firewall/UAC foi criada; nenhum jogo/xrevive do usuário foi tocado. Sem commit/push e sem ZIP novo.
- Próximo passo possível: aplicar a troca também quando a cópia entra/sai da garagem no fluxo real da UI, e validar a escolha de carro pela UI do parceiro. A limitação de recurso do release permanece para qualquer troca no mundo atual.
