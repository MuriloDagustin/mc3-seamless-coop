# Teste ao vivo isolado — 18/10/2026

Autorizado pelo usuário nesta sessão, com áudio mutado e fora do desktop em uso. Nenhuma tecla do Windows foi enviada; apenas comandos UDP do controle interno do mod. Os commits locais posteriores foram autorizados pelo handoff; nenhum push foi feito.

## Isolamento

- Duas cópias do runner em um desktop Windows separado (`CreateDesktopW`), sem `SwitchDesktop`.
- Executável obtido da pasta de teste `D:\dev\mc3-coop-data\testgame\Midnight Club 3 DUB Edition Remix`; não foi acessado `D:\Jogos`.
- Assets e saves de desenvolvimento copiados para `out\live-isolated`; alterações só nas cópias descartáveis. Atenção: `out` usa o redirecionamento de armazenamento já existente no workspace.
- `MC3COOP_GAME` apontou para a pasta de teste exigida; `MC3COOP_MUTE=1`, drivers SDL dummy e ganho de áudio zero solicitados. A consulta Core Audio confirmou **muted=true nas duas sessões de áudio do jogo**. Não foi alterado o volume geral nem a sessão do filme.
- Segunda rodada limitada a 30 FPS e prioridade abaixo da normal. Apenas os processos iniciados pelo controlador deste teste foram encerrados por seus próprios handles.

## Resultados observados

1. **Conexão IP local: passou.** Duas instâncias ligadas em 127.0.0.1, UDP 45520; portas de diagnóstico exclusivas 45803 e 45804.
2. **HUD/tela dividida: passou neste circuito e na ronda.** Capturas do framebuffer mostram tela inteira nas duas cópias. Setup: humanos=2, views=1, HUD local=0, HUD remoto=-1.
3. **Contagem remota/chegada: passou neste circuito.** Vtable 0x46B2D8. Antes da chegada local, remoto finished=1, laps=0, nextCP=65535, finishCounter=1. Depois da chegada local, finishCounter=2; ambas as cópias continuaram processando estados 7/8, sem crash observado.
4. **Vitória comunicada antes da chegada do parceiro: passou.** Host venceu; parceiro recebeu partner_won=1 e prize=800 antes de terminar. Saldo host 6500→7300; parceiro 6140→6940; rewards+0xBC=800 em ambos. A travessia dos checkpoints foi acelerada com teleporte pela interface de teste. Ambos terminaram em posição local 1: este caso NÃO exercita o ramo de complemento de um prêmio menor, nem vitória recebida após o resultado local.
5. **Largada automática: falhou nesta configuração.** O host iniciou normalmente com RB no marcador. Pacote chegou ao parceiro, que foi teleportado, mas o desafio por escrita de farol expirou: `race: the host's race did not start here`. Para testar as chegadas, o parceiro foi levado ao registro correto (city=0, race=30) e iniciou manualmente com RB pelo controle interno. Portanto, este teste não valida largada automática.
6. **Tráfego: defeito 48/0 reproduzido.** Com o hook ativo, host chegou a spawned=48 e parceiro permaneceu spawned=0, pool=48, target=48, frozen=0, spawn_gate=1, native_net=0, sessão=-1/-1, humanos=2 e views=1; links de região disponíveis. O problema já aparece nos menus e permanece após carregar a ronda. Foi observado takeover de tráfego enquanto as cópias estavam nos menus. tm+0x80 podia estar em 48 mesmo sem handles válidos no parceiro. Hipótese a investigar: takeover antes de Reset deixa slots fora das listas livres; não é causa confirmada nem correção implementada.

## Bloqueio adicional do runner

Na primeira rodada, os bytes do runner em 0x14296CA64 e 0x14296CA14 estavam em 1 (native_controllers e keyboard). O contador XInput subia, mas nenhum trabalho de corrida/tráfego chegava ao hook com retorno 0x1404E35C4. Mensagem: `game: waiting for guest xinput hook; race/traffic writes suspended`.

Na segunda rodada, `XRECOMP_NATIVE_CONTROLLERS=0` e `XRECOMP_KEYBOARD=0` produziram bytes 0 e ativaram o hook verificado. Timestamp 0x6AD2B76C, base 0x140000000 e instrução FF 15 0C 21 09 02 foram conferidos no processo. Não foi alterado código do runner. O lançador ainda não recebeu uma correção para compatibilizar esse requisito com o controle por teclado; o pacote anterior continua experimental.

## Evidências locais (privadas)

`out\live-isolated\logs\mc3coop-3.log`, `mc3coop-4.log` e capturas `cruise-*`, `grid-*`, `result-*`. Controlador e consulta de mute em `out\codex\live_isolated.py` e `audio_status.py`.

## Ainda não validado/resolvido

Complemento real de prêmio de colocação inferior; vitória tardia; autosave/reabertura dos saves; outros tipos de corrida; largada automática com o hook verificado; recuperação e ciclo de vida do tráfego; caminhões, colisão e IA sincronizada; EOS entre dois PCs. Nenhum zip novo foi declarado pronto com base neste teste.

## Rodada seguinte — correções de largada e ciclo do tráfego

Testado com `XRECOMP_NATIVE_CONTROLLERS=0`, `XRECOMP_KEYBOARD=1` (padrão do lançador), controle virtual equivalente ao XInput no guest slot1, teclado guest0. Gancho funcionou. O lançador passa a forçar native0 mantendo teclado. Não foi injetada tecla Windows.

- O desafio agora mescla RB no estado XInput retornado ao jogo quando o mod está levando o jogador ao marcador. Host e parceiro iniciaram o circuito automaticamente, sem comando adicional no parceiro; logs `started the host's race here`.
- `traffic::Snapshot/Tick` passam a operar somente com carro local e estado de corrida5/6. Links originais dos puppets são guardados e devolvidos antes dos estados de resultado/reset e ao desconectar.
- Ronda: host48 ativos, parceiro48 puppets. Comparação de48 posições:48 a menos3m, maior erro1,81m (amostras não simultâneas via UDP).
- Transição ronda→circuito: links foram devolvidos; ambos pools esvaziaram no carregamento e repovoaram até48. Parceiro voltou a `traffic=on shown=48`.
- As duas chegadas passaram novamente, sem tela dividida. Após continuar os resultados, ambos voltaram à ronda, com48 handles válidos no parceiro e sincronização ativa. `NumberOfWonRaces` u16 estava2 em ambas as cópias descartáveis.
- Comparações devem ignorar handles do host sem road ativo: slots em reciclagem podem existir no host e ser corretamente ocultados abaixo do mapa no parceiro.

A colisão de puppets deslocados entre células, caminhões, crashes e a entrada por teclado sem gamepad ainda exigem testes específicos. Vitória tardia e IA estão em desenvolvimento separado.

## Handoff, item 1 — autoridade dos adversários (18/10, build novo)

Teste com duas cópias próprias no desktop invisível, portas45803/45804, runner timestamp0x6AD5001B, native_controllers=0, keyboard=1 e áudio desabilitado. Outros processos de teste já abertos não foram encerrados. Controlador retém os handles dos processos que criou.

- A máscara isolada não bastou: modelos de carros também divergiam entre carreiras. O host agora envia máscara+3D4, limite+40 e os oito registros inline de adversários+274 (modelo, habilitado, nome e variante). A entrega se repete a cada100ms, com geração/revisão; o parceiro só aplica antes da alocação e quando o nome completo inline do setup+68 corresponde à corrida. Nenhum ponteiro do host é importado; o Task* local+20 do registro da carreira é preservado.
- Nomes cosméticos de pilotos saíram do hash de identidade. Modelo, vaga, tipo, pontos de largada e características da corrida permanecem verificados.
- Rodada normal:4/4 adversários, applied=4 e erro máximo observado1,73m.
- Rodada com diferença de contagem provocada no setup NEXT descartável do host: máscaraFFFFFF9F sobre7 vagas do arquivo, cinco habilitadas. Parceiro recebeu revisão2 antes da alocação e criou5, como o host. Inicialmente erro máximo3,45m.
- A primeira tentativa de chegada revelou divergência de classificação após o parceiro terminar: o mod devolvia os adversários à lógica local no estado7. Corrigido para manter autoridade durante estados de resultado7..10. O snapshot terminal fica retido após o timeout normal, até troca de corrida/desconexão.
- Repetição `python tools/ai_test.py 3 --host-port 45803 --partner-port 45804 --finish`: PASS. Cinco adversários em modo experimental, erros máximos das três amostras1,27m/0,72m/0,98m. Host terminou, parceiro continuou por mais de2s, depois também terminou. Colocações finais dos cinco adversários em ambas as cópias: [6,3,4,2,1], iguais em três leituras e na checagem posterior. Humanos locais terminaram em5º, remoto em7º; estado8 estável. Evidências privadas em out/live-isolated/archive/ai-five-final-pass-*/evidence.json e capturas validated-final-*.
- Houve uma leitura transitória stale antes dessa repetição, seguida de recuperação. Não foi identificada causa nesse teste; a perda de pacotes retorna a IA local após500ms durante a corrida. Não se declara validação de latência/perda de internet.
- Testes de memória simulada passaram: setup antes da alocação, nomes longos, revisões/gerações antigas, pose/momento, restauração do bit do cérebro, NaN, modelos diferentes, progresso idempotente, snapshot terminal e importação nos resultados. A validação rejeita nextCP>=n e contadores acima de n antes de qualquer escrita; nome/ponteiro de piloto permanece local.

Limites: modo experimental, corridas comuns com layout compatível; outras modalidades, colocações humanas divergentes entre cópias, colisões entre células e EOS entre PCs continuam sem validação específica. O bit car+15C/0x10 bloqueia Opp::Update, mas produtores locais de comandos continuam existindo e a pose é corrigida pelo hook. Protocolos incompatíveis são recusados (rede3, IA2).

## Handoff, item 2 — pacote e lançador

`cmd /c mod/package.cmd` passou e gerou out/MC3Coop-test.zip (privado, com credenciais, ignorado pelo git). O runner novo foi copiado somente da instalação de leitura para a pasta testgame; nome Midnight Club 3 DUB Edition Remix.exe. RunnerPath reconhece esse nome além do legado; mensagens em português e README atualizados.

- `python tools/test_launcher_unit.py`: PASS. Compila o lançador real com CreateProcess do jogo interceptado, diálogos e netsh/UAC simulados. Testa Unicode/UTF-16/apóstrofos, DLL ausente, TOML ausente permitindo IP, pasta forçada inválida, seleção de runner novo/legado por tamanho/data, processos abertos, firewall recusado/repetição/cancelamento e coleta completa/parcial. Três ZIPs reais do PowerShell íntegros e sem TOML/marcador privado.
- `python tools/test_launcher.py --logs-only`: PASS com o EXE extraído do ZIP real. Criou ZIP de logs na Área de Trabalho, incluindo logs/smoke.log e logs/runner/smoke.log; sem TOML. O teste recolheu somente seu próprio ZIP para out/launcher-tests/1792352973020730100/MC3Coop/collected-smoke-logs.zip, sem deixar arquivo de teste na Área de Trabalho. Nenhum jogo foi iniciado por esse comando.
- `python tools/test_launcher.py 30`: BLOQUEADO como esperado pelo xrevive já aberto (PID15016, executável C:/Users/muril/AppData/Local/xrevive/xrevive.exe). A mensagem em português informou o caminho e pediu fechamento manual. Dois runners preexistentes16980/18624 também permaneceram intactos. O lançador desse teste foi encerrado apenas pelo Job Object próprio.
- O harness agora usa pasta extraída única, desktop invisível, processo suspenso atribuído a Job Object antes de iniciar (encerramento somente dessa árvore), nenhuma limpeza por nome/linha de comando e nenhum envio de tecla Windows. Firewall first-run fica pré-marcado apenas no INI descartável. O novo comando interno `panel toggle` usa o painel real sem mensagens de teclado.

**Pendente do item2:** carga do mod e captura do painel através do próprio lançador do pacote não puderam ser verificadas com o xrevive aberto. Não foi criado bypass e nenhum processo alheio foi fechado. Repetir o smoke completo quando o usuário fechar manualmente xrevive e runners. O pacote permanece experimental; este bloqueio não impede os testes separados de corrida do item3.

## Handoff, item 3 — vitória tardia e contexto final dos humanos

**Ao vivo, antes da suspensão dos testes:** duas cópias próprias, mudas e no desktop invisível; runner0x6AD5001B. Sem chamadas a funções guest. Circuito city0/race30, vtable46B2D8. O teste acelerou checkpoints por teleporte e induziu derrota local pelo byte arrested+5, preservando DNF/participação. Não representa uma corrida natural em2ºlugar.

- A proteção de chegada está ligada por padrão somente no contexto suportado (exatamente dois humanos, uma câmera/HUD local, rede nativa desligada e sem timer); MC3COOP_FINISH_GATE=0 mantém opt-out técnico.
- `tools/result_delay_proxy.py` mantém poses/handshake imediatos e atrasa apenas Won/Finished do parceiro para o host. O proxy não inicia nem fecha executáveis. Teste loopback sintético passou.
- Parceiro chegou primeiro e venceu. Host chegou com derrota induzida e permaneceu no estado6, contador humano1, guardas de resultado/pagamento ainda0. O resultado do parceiro chegou2s depois; a segunda chegada exclusivamente LOCAL levou o contador1→2 e executou o resultado permanente uma vez. Remoto permaneceu sem HUD e protegido contra callback.
- Rodada final de vitória: NumberOfWonRaces1→2 nas duas cópias; dinheiro host6500→7300 e parceiro6140→6940, +800 cada. Host teve prêmio nativo zero e complemento de800. Repetições de pacotes não mudaram esses totais. Melhor volta host0,9022/parceiro1,2101 permaneceu igual após o replay (tempos artificiais do teleporte).
- A ordem inversa de chegada revelou dois defeitos, corrigidos: liberar IA enquanto o parceiro já mostrava resultados e o host ainda corria; e deixar o remoto como DNF definitivo, deslocando o ranking nativo. Agora a IA continua seguindo snapshots ativos nos resultados. Finished tem20bytes (protocolo4), com arrested, tempo original, métrica e score; os dois mundos recebem os mesmos dados reais dos humanos, trocando apenas a vaga local/remota. Não importa ponteiros nem chama chegada remota. O tempo local usado na classificação é restaurado somente depois do Report nativo; estatísticas já gravadas ainda podem incluir a espera.
- `python tools/late_victory_test.py --case win`: PASS. Humanos terminaram em5º/1º, mas as três IAs ficaram [2,3,4] nos dois mundos em três amostras. Parceiro permaneceu ai=experimental durante a espera. Evidência privada: out/live-isolated/late-tests/win-1792354062085606700.
- `python tools/late_victory_test.py --case loss`: PASS. Derrota induzida nos dois, sem deadlock, contador2 em cada cópia, vitórias e dinheiro inalterados. IAs [1,2,3] iguais nas três amostras. Evidência: out/live-isolated/late-tests/loss-1792354241854222300.
- Timeout: após15s mantém o resultado local. Um terminal posterior ainda pode atualizar dados de classificação, mas não reabre crédito/pagamento nem concede vitória. Esse ramo e o fallback de replay foram verificados SOMENTE em RAM simulada. A tentativa ao vivo com20s foi interrompida antes da corrida por causa dos avisos de firewall; não é um teste aprovado.

`python tools/test_coop_memory.py` passou com as fontes finais: vitória2s depois, perdas, retransmissão500ms, timeout15s, desconexão, duplicatas/serial antigo, máscaras de1/7/32/64CP, guardas permanentes, melhor volta, contexto dos dois humanos invertido, pacote inválido, metadata após timeout sem crédito e resultado nativo idempotente. Helpers de evento nativo no harness são simulação, não prova da callback real.

**Ainda pendente deste item:** timeout/desconexão ao vivo, vitória natural de quem chegou em colocação inferior, persistência/reabertura dos saves do novo gate e outras modalidades. Não há promessa de vitória após o prazo nem de tempo de corrida exato sem a espera.

## Interrupção: avisos de firewall e correção dos harnesses

O usuário relatou avisos repetidos do Windows. As rodadas usavam um caminho novo de MC3Runner.exe a cada execução, perdendo a associação das permissões existentes. Os testes ao vivo foram suspensos e somente os processos próprios foram encerrados. O último controlador estava no boot, que ainda não consultava seu stop-file: os dois processos próprios34560/30484 foram encerrados por handles após conferir seus caminhos exatos. O controlador fechou seu Job Object. xrevive15016 e runners preexistentes16980/18624 permaneceram intactos. Codex não executou netsh nem alterou regras de firewall.

- Os novos tools/isolated_pair.py e tools/test_launcher.py usam caminho fixo para o runner. Saves/evidências continuam separados por rodada. O stop-file também é consultado durante boot e esperas.
- `firewall_preflight.py` consulta somente Get-NetFirewall* em ActiveStore, PowerShell oculto/não interativo. Sem regra de entrada UDP/Any já habilitada para o caminho exato, perfil/portas/endereçosAny, ou em caso de erro/bloqueio explícito, recusa iniciar. Não cria regra, não pede UAC e não oferece aprovação.
- `python tools/test_firewall_preflight.py`:9 testes mockados PASS. `python tools/test_launch_guards.py`:2 testes PASS, provando recusa antes de criar Job Object/processo; o teste do par também prova o mesmo caminho de EXE em duas rodadas. Nenhum jogo, janela, socket ou consulta real de firewall foi aberto nesses testes.
- `python tools/test_launcher_unit.py`: PASS novamente. Processos de jogo, diálogos e netsh são interceptados; apenas ZIPs locais de logs são criados pelo PowerShell real. Nenhum runner foi iniciado.

Os novos guardas não foram usados para voltar a executar o jogo nesta etapa. O smoke completo do pacote continua pendente. Nenhum aviso do Windows foi respondido automaticamente. O trabalho continuou offline.

## Fechamento offline

`mod/package.cmd` compilou e gerou novamente out/MC3Coop-test.zip com as fontes finais (protocolo4 e README atualizado). Verificação por zipfile: CRC íntegro; MC3Coop.exe e xinput1_4.dll dentro do ZIP idênticos byte a byte aos compilados; sem default.xbe/MC3Runner.exe. O ZIP contém configuração privada, permanece ignorado pelo git e não foi publicado. Manifesto/hash privado em out/codex/package-final-check.json.

Consulta real SOMENTE LEITURA de firewall para out/live-isolated/MC3Runner.exe executou sem elevação e recusou o caminho por não encontrar a regra ampla exigida pelo preflight. Nenhum runner foi iniciado. A importação de late_victory_test também foi conferida com sockets/parser bloqueados: não tem efeitos de rede ou execução ao importar.

O item4 permanece na análise estática em RE-PENDENCIAS-MENORES.md. Pintura/peças exigem marcar dirty sem reintroduzir a máscara a cada frame; reconstrução de modelo e reboques ainda não têm implementação validada. Não foram feitas mudanças especulativas de runtime nesses itens. Os scripts históricos pair.py/xrun.py não devem ser usados durante a suspensão dos testes ao vivo; não possuem os novos guardas de firewall.

## Continuação offline, 19/10: integração e autoridade de largada

Base recebida na pasta principal: `780a698`, com as correções posteriores de garagem/desempenho preservadas. A seção histórica acima descreve builds anteriores; o estado seguinte corresponde aos fontes atuais, protocolo6. Nenhum commit/push nesta continuação.

Integração anterior preservada: sincronização física de tráfego/articulação permanece opt-in por `MC3COOP_TRAFFIC_PHYSICS=1`, com cobertura sintética; não há validação nova de colisões ao vivo. Configurações compatíveis do mesmo modelo aplicam dirty uma vez por revisão/objeto; outro modelo fica pendente para a próxima construção nativa. Não foi descoberta uma reconstrução segura do carro completo por dados sem recarregar o mundo. Ver `RE-TROCA-CARRO-IMPLEMENTACAO.md`, `RE-TROCA-CARRO-FACTORY.md` e `RE-SYNC-TRAFEGO-FISICA.md`.

Nesta continuação foram implementados o bloqueio de largadas próprias do parceiro na rua, o aviso de largada pendente na garagem e a identidade de mundo nos pacotes de posição. Posições de outra corrida e interpolação antiga são descartadas, preservando CarCfg e sua sequência. O ranking nativo ainda mantém a entrada do ausente concluída/DNF como proteção contra callback sem HUD; não afirmar exclusão do ausente da classificação.

Uma largada recebida durante outra corrida fica pendente: não apaga gate, resultado, prêmio ou serial ativos. O setup da próxima autorização possui buffer/revisão próprios. Fases de marcador registram serial/cruise/setup/dados/cidade; substituição de autorização, troca de mundo ou geração de rede cancela a fase velha na thread do jogo antes de qualquer teleporte. Campos emprestados só são restaurados no registro ainda identificado. Timeout não equivale a largada bem-sucedida.

Verificações finais:

- `python tools/test_coop_memory.py`: PASS contra os fontes reais em RAM sintética. Inclui `RaceContextTests`, `HostStreetTests`, `HostMarkerPhaseTests`, `WorldPoseTests` e `WorldCreditTests`, além das suítes existentes de setup/IA, chegada tardia/ranking, tráfego físico e CarCfg.
- Novos casos cobrem posse suspensa em garagem/frontend e retorno ao mesmo mundo, restauração ao desconectar, serial consumida, dados/cidade/máquina/veículo alterados na mesma VA, nova largada durante fase velha, reinício de sessão com serial1, timeout e ausência de teleporte indevido.
- Contexto/posição: arquivo completo após o prefixo de32bytes, corrida ainda não alocada, garagem/menu, serial diferente e mudança local sem pacote novo; nenhum `Apply` de pose incompatível. Configuração/revisão/ordem permanecem preservadas.
- Crédito: serial pendente não importa vitória/prêmio/terminal na corrida ativa; contexto incompatível não arma gate nem altera crédito. A autorização nova preserva o gate/prêmio antigos; ativação posterior consome seu setup separado. O caminho legado de crédito da identidade correta continua idempotente.
- `cmd /c mod\build.cmd`: PASS, DLL em `out/mod/xinput1_4.dll`. Log `out/build-host-flow.log`; log da suíte `out/test-host-flow-memory.log`.
- `git diff --check`: PASS. Avisos de normalização CRLF são do Git; não houve erro de whitespace.

Não foram iniciados jogo, MC3Coop/MC3Runner, sockets de sessão ou testes de GUI. Nenhuma janela/foco, processo estrangeiro, regra de firewall ou UAC foi alterado. O teste sintético mantém a inspeção inerte de PE com `DONT_RESOLVE_DLL_REFERENCES`, sem executar código do runner. O pacote privado já entregue não foi regenerado nem publicado; protocolo6 exige atualizar os dois jogadores quando houver nova entrega.

Pendências da prioridade1: espera no resultado/decisão pós-corrida, próxima etapa de série com serial nova, retorno à rua ao abandonar torneio ativo e remoção nativa da entrada ausente. A análise em `RE-HOST-FLUXO-RESULTADO.md` prova o gate limitado de12 para tipos1/3, mas também mostra callbacks de tarefa que podem sobrescrever a ação e inicialização que seria pulada ao suspender bytes isolados. Essas propostas não foram ativadas no runtime. Os critérios de duas cópias permanecem sem execução nesta etapa.
# Complemento offline — fluxo pós-corrida, 19/10/2026

Implementação e limites em `RE-HOST-FLUXO-IMPLEMENTACAO.md`. `python tools/test_coop_memory.py` passou com fluxo completo simulado de espera, Quit/Sim, Next, cursor incompatível, perda/reordenação de decisões e histórico de setups. Log: `out/test-host-flow-memory.log`.

`cmd /c mod\build.cmd` passou e produziu `out/mod/xinput1_4.dll`; log `out/build-host-after.log`. Avisos existentes de ImGui. Nenhum executável do jogo/launcher, rede de teste, janela ou firewall foi aberto. Isso não comprova timing dos menus, carga nem prêmio em série no jogo. Sem novo ZIP e sem push.

## Investigação posterior: troca integral do carro

Prioridade solicitada após9663375. Apenas análise estática: xref de wrappers/factory, tabela de métodos no XBE e pacote nativo de configuração44. Ver `RE-TROCA-CARRO-REDE.md` e `PLANO-TROCA-CARRO-NATIVA.md`. Asserções locais sobre o índice confirmaram272010→271E20, consumidores103A50/103C50 no updateFFD90 e ausência de chamada direta à fábrica nesse update; o único chamador direto indexado de2CBC40 continua o fragmento2C004C do construtor do jogador. Isso delimita o grafo de chamadas diretas, não prova inexistência de outro mecanismo.

Não houve modificação de runtime, nova compilação, teste em RAM ou ensaio ao vivo nesta investigação. A troca completa permanece não implementada. Nenhum jogo/launcher, conexão de teste, UAC ou firewall aberto. A alternativa de ponte nativa é proposta de ampliação do escopo, não feature entregue.

## Complemento offline - contexto da ponte nativa, 19/10/2026

`python -m unittest discover -s tools/tests -p test_native_bridge.py -v`: 12 PASS. Casos novos: faixas de `.pdata`/flags de unwind, referência RIP-relativa resolvida por deslocamento (não por valor absoluto) e grafo de chamadas diretas com alvo interior resolvido ao dono.

`python tools/inspect_native_bridge.py D:\dev\mc3-coop-data\xrun\MC3Runner.exe`: PASS, saída privada `out/native-bridge-current.json`. Na build `6AD54432`, o mutex do callback (`1427E9EE8`) aparece só em `31067F/3106C5/3106DC`, dentro de `3105B0`; o caminho direto do input (`300D00`→`3068D0`/`306BB0`) e 13 ancestrais diretos têm zero referências e zero chamadas à aquisição. Unwind `3AD270`/`39EFA0` = versão 1, flags 0.

Isso é inspeção de bytes, sem carregar o PE, executar código, abrir processo/jogo, rede, janela ou firewall. Não valida reentrância do callback durante o HLE, chamadas indiretas, exceção/longjmp, stack scratch, CarCfg persistente, buckets, perfil nem comportamento ao vivo. `execution_safe` e `native_calls_enabled` continuam false; nenhuma função guest foi chamada. A cópia `testgame` tem SHA/timestamp diferente (`6AD5001B`) e recebeu apenas a descoberta de tabela.

## Teste ao vivo - ponte nativa e troca de modelo, 19/20-10-2026

Testes com duas cópias descartáveis (`tools/pair.py`, portas 45703/45704), desktop invisível, áudio mudo, runner em caminho fixo. Não criei regra de firewall, não pedi UAC e não toquei no jogo/xrevive do usuário. O DLL testado foi compilado por `mod/build.cmd` com `native.cpp`.

### 1. Prova de ABI no gancho (implementada)

`MC3COOP_NATIVE_PROBE=1` chamou, da thread do jogo e pelo wrapper full-save, `3308C0`, `2BD910` (com ECX=player manager e um argumento) e mais três `3308C0`. Resultado nas duas cópias:

```
native: call guest=003308C0 argc=0 result=1 mismatch=0 esp=04927F8C
native: call guest=002BD910 argc=1 result=1 mismatch=0 esp=04927F8C
native: call guest=003308C0 argc=0 result=1 mismatch=0 esp=04927F8C (x3)
native: probe PASS
car: new world, local car ...
```

`mismatch=0` cobre EAX/EBX/ECX/EDX/ESI/EDI/EBP/ESP, x87, xmm0..7, FCW/MXCSR e flags emulados. Os cinco testes são repetidos no mesmo contexto; o jogo continuou e carregou o mundo normalmente. Nenhum deadlock de mutex foi observado.

### 2. Troca de modelo em mundo carregado (bloqueada pelo próprio jogo release)

A tentativa real de construir `vp_eclipse_04` no lugar do `vp_d_scion_tc_05` chegou ao ctor e ao build nativos:

```
native: model resolve ok=1 index=00000000
native: factory ctor=1 f04=vp_eclipse_04 f54=80E7AD00 fE8=1 fE1=00 fEC=1
native: factory build=1 f44=00000000 f4C=00000000 f50=837658E0
native: swap build failed (wrapper=00000000 carData=00000000)
```

Causa demonstrada: `2CB2D0` chama `105B80`, que no caminho de entrada livre chama `105AA0`; esta rotina tenta criar o recurso e o próprio binário release responde com a string `Cannot make car resource on RELEASE - build on Beta/debug or ask Yomal why they are not in the assets!`, devolvendo 0. A continuar, `2CBC40` chama métodos virtuais com produto nulo e o runner registra `[ICALL] unresolved target` e encerra. O pool `[50A08C]` tem 16 entradas, mas só o modelo em uso está em modo 0; o modo 1 é do pool de garagem/showroom e `105B80` não o reutiliza no modo offline. Portanto **não há como criar um modelo diferente em mundo carregado nesta build release**, nem pela ponte nativa. A implementação final recusa antes de chamar o construtor e mantém o carro antigo.

### 3. Troca sem reconectar pelo próximo carregamento (funciona)

Fluxo com o host em ronda e a sessão conectada:

1. No parceiro, a entrada selecionada da garagem foi trocada para `vp_eclipse_04` pelo comando de diagnóstico `localcar 2` (simula a escolha da garagem; não é a UI real).
2. O host recebeu a configuração e manteve `live slot cars: vp_d_scion_tc_05 vp_d_scion_tc_05` (o remoto ainda era o tC). `car_model_pending=1`.
3. O host carregou uma corrida normal pela ponte de marcador (`gorace`), sem reconectar.
4. Depois da carga: `live slot cars: vp_d_scion_tc_05 vp_eclipse_04` no host, `current/next slot cars` com Eclipse na vaga 1. As duas cópias permaneceram `COOP 2/8 CONNECTED` e na mesma corrida (`serial=1`).

Isso confirma o caminho suportado: a troca de modelo é aplicada no próximo carregamento normal (corrida ou garagem), sem reconectar; não é instantânea no mundo atual.

### Limites

- A simulação da escolha da garagem usou um comando de diagnóstico; a UI real da garagem não foi dirigida neste teste.
- O jogo release não cria recursos de carro em runtime; a mensagem e o encerramento observados são a prova. Um build beta/debug poderia permitir o construtor, mas não é o produto.
- Nenhum pacote/ZIP foi regenerado; o ZIP entregue continua no protocolo anterior.

## UI real da garagem e correção de `LocalCarCfg` (20/10/2026)

`LocalCarCfg` agora espelha `155920`: origem 1 → `0x541700 + index*0x108` (banco central); origem 0 → `0x53F810 + 53F80D*0x108` (coleção local). Só publica com `mode==2` (carreira/cruise/corrida), não na garagem (5) nem no front end (3). Teste sintético novo em `car_config_tests.inc` passou para os dois ramos e para a recusa em mode 5.

Teste ao vivo com duas cópias: o parceiro entrou na garagem pela UI real (pausa → Go to Garage → Yes), abriu Manage Vehicles, trocou a versão com o **stick direito**, confirmou `A` → `Load Vehicle` → `Yes` e saiu. O carro local dele mudou para `vp_d_scion_tc_05`; o host recebeu a configuração e, após carregar uma corrida, `live slot 1` passou a `vp_d_scion_tc_05`. As duas cópias permaneceram `COOP 2/8 CONNECTED`, serial 1, mesma corrida.

**Limite:** a UI real foi exercitada com duas versões (a coleção local), não com um segundo modelo comprado legalmente. A injeção de uma segunda entrada crua na coleção (`addgarage`/`setgarage`) fez o preview da garagem encerrar o runner (`[GARAGE-LIFE] preview construction`, ExitProcess), então esses comandos foram removidos. O aviso novo de HUD foi compilado, mas não conferido visualmente.

Correção da conclusão: a troca **instantânea** continua **não demonstrada**. O release bloqueia a criação de recurso novo; a reutilização de um carData já carregado (modo 1, ramo `105BD6`/`210630`) é um caminho em aberto que exigiria prova de ownership/registro/compatibilidade. A ponte foi validada apenas com getters; construção/IO/yield não estão provados.

## Correção da sincronização de tráfego (20/10/2026)

**Causa:** `traffic::Capture` grava `g_sent_at` com o relógio da thread do jogo (`GetTickCount64`), mas `traffic::Snapshot` comparava esse valor com o `now` da thread de sessão, que vem de `NowMs()` (`steady_clock`). Como os dois relógios têm origens diferentes, `now < g_sent_at` era praticamente sempre verdadeiro e `Snapshot` devolvia `false`: o host capturava (`publish=6`) mas não enviava nenhum pacote de tráfego.

**Correção:** `Snapshot` agora usa `GetTickCount64()` internamente para a validade/frescor, o mesmo relógio de `Capture`. O parâmetro `now` da sessão é ignorado nessa comparação.

Teste ao vivo (duas cópias, ronda):
- host: `traffic: capture on`, `session: sending traffic snapshot (6 chunks)`;
- parceiro: `session: traffic packet received (1292 bytes)`, `traffic: received batch epoch=1 cycle=1 cars=48 layout=AE7B8A19`, `traffic: syncing 48 cars with the host`;
- estado: parceiro `traffic=on shown=48 host_cars=48`, layouts iguais;
- `python tools/traffic_test.py 10`: pool 48/48, 48 ativos, 48 dentro de 3 m, pior erro 1,1 m, nenhum não spawnado.
- Regressão sintética nova em `traffic_memory_tests.inc`: `Capture(test_now)` seguido de `Snapshot(..., 1)` deve publicar; a segunda chamada no mesmo ciclo não.
