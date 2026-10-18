# Teste ao vivo isolado — 18/10/2026

Autorizado pelo usuário nesta sessão, com áudio mutado e fora do desktop em uso. Nenhuma tecla do Windows foi enviada; apenas comandos UDP do controle interno do mod. Nenhum commit/push.

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
