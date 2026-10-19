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
