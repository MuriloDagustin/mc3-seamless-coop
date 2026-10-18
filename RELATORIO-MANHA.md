> Atualização ao vivo autorizada em 18/10/2026: teste isolado confirmou tela inteira e chegada sem crash neste circuito, com $800 para ambos. Largada automática falhou; tráfego 48/0 reproduzido; modo padrão de controles do runner contorna o hook. Ver [TESTE-ISOLADO-2026-10-18](docs/TESTE-ISOLADO-2026-10-18.md). O restante deste relatório descreve a entrega anterior e seus testes estáticos.

# Continuação do co-op — 18/10/2026

Trabalho em `C:\dev\mc3-coop`, branch `coop-noite`, sem commit/push. Esta revisão não abriu jogo/runner, não encerrou processos e não acessou `D:\Jogos`.

## Alterações desta revisão

- Vaga remota sem HUD/câmera: `slot+0x14=-1` nos setups atual e próximo, mantendo uma tela local.
- Na família comum de corridas por voltas/checkpoints, o progresso remoto é concluído por stores antes da chegada local: `+4=1`, voltas restantes zero, guarda dos checkpoints, flag de desistência e contador `race+0x1250` incrementado uma vez. Não chama o callback que exige HUD/câmera. A desistência é um marcador interno desta cópia; o resultado real do parceiro chega pela rede.
- Removida a antiga desistência por desconexão, inclusive o acionamento do timer global. Desconectar não conta outro fim. Os humanos dos setups carregados ficam estáveis durante a corrida.
- A vitória local também é detectada no estado 7: agora ela pode pular diretamente de 5 para 7, sem uma consulta de XInput no estado 6.
- Complemento de prêmio: aceita somente o serial da corrida atual, atualiza dinheiro e prêmio mostrado no painel, marca carreira suja, não paga novamente por retransmissão e recusa overflow. O estado reinicia no próximo início de corrida.
- O proxy agora verifica `_ReturnAddress()==0x1404E35C4`, base/timestamp do runner e bytes do call site. Não usa mais `user==0` como prova de contexto guest nem executa esse trabalho por GetStateEx. As escritas de corrida/tráfego aguardam esse gancho validado da thread do jogo. Retirado o fallback que fazia essas escritas pela thread de rede quando faltava XInput; isso evitava uma janela de concorrência no contador de chegada. Sem o gancho, o log informa a suspensão.
- Tráfego: diagnóstico agora distingue pool pronto, slots realmente criados, links, destroços, gate de spawn, freeze, densidade escolhida, telas/humanos, sessão nativa e regiões. Não chama de sincronizado um pool com zero puppets desenhados.
- Lançador da entrega do Codex incorporado à pasta principal: mensagens Unicode, bloqueio por caminho de processo, oferta opcional de firewall UDP, `--logs` e abertura sem TOML. O empacotador usa o build atual em `out\mod` e produz `out\MC3Coop-test.zip`.

## Validação desta revisão

`python tools\test_coop_memory.py` compila os fontes reais de carro/corrida/tráfego contra RAM Xbox simulada, sem carregar qualquer executável do jogo. Cobre: HUD -1; guardas de CP/volta; contagem idempotente; desconexão; resultado 5→7; prêmio compartilhado; pacotes duplicados, negativos e de outra corrida; overflow; proteção de outras classes de corrida; filtro de contexto/build do XInput; diagnóstico de tráfego somente leitura e seleção por telas.

`mod\package.cmd` compila o mod e o lançador. O build usa `MC3COOP_BUILD_OUT` para manter as saídas nesta pasta; a chamada direta legada a `build.cmd` conserva seu destino antigo quando essa variável não está definida. O zip pode conter `coop-online.toml`, permanece ignorado pelo Git e não foi publicado.

Os testes do lançador usam diálogos/destino Desktop/processo do jogo interceptados; o PowerShell da coleta é real. Não testam UI real, criação efetiva da regra de firewall ou abertura do runner.

## Ainda pendente / não afirmar que está resolvido

1. **Teste visual da tela e da chegada com duas cópias.** A alteração foi comprovada por leitura de asm e testes de stores, não por execução desta versão. Uma cópia já carregada com HUD remoto precisaria de recarga do mundo; trocar o número do slot não destrói um HUD já criado.
2. **Vitória recebida tarde.** Se o resultado local já foi processado antes de chegar o aviso de vitória do parceiro, esta versão não refaz o relatório de carreira retroativamente. Concluir o remoto no início torna a janela menor. É necessário projetar sincronização do encerramento/crédito tardio antes de prometer vitória para ambos em qualquer latência.
3. **Tráfego 48/0.** A causa real da ausência de slots ativos no parceiro continua sem captura nesta revisão. O pool pode estar alocado com todos os handles `0xFFFF`, sem nenhum objeto inserido no phLevel. Escrever matrizes não cria esses objetos. Não forcei handles nem chamei spawn. O diagnóstico novo permite localizar o gate no próximo teste autorizado.
4. **Correção do mapa de densidade:** asm 0x6CDCD..0x6CDFB usa `setup+0x50` (telas), não `setup+0x4C` (humanos). Uma segunda tela pode escolher outra densidade; não prova que foi a causa do 48/0 observado. Ver `docs/RE-HUD-SEM-CAMERA.md`.
5. **Modalidades especiais.** Contador DWORD `+0x1250` é usado somente nas vtables comuns conhecidas; outras classes não recebem essa escrita. Eliminação, pontos, perseguição, hookman e variantes ainda precisam de validação específica.
6. **Puppets de tráfego/IA e colisão.** Tráfego continua WIP, caminhões/crashes ficam locais e o método atual de carros sem road link ainda carece de prova de broadphase. IA adversária continua independente; o mapa `docs/RE-IA.md` não fornece um interruptor confirmado do cérebro.
7. **Dois PCs/EOS.** Criar sala localmente não prova entrada entre PCs. O pacote desta revisão é de teste e não foi executado nesta sessão.

## Histórico recebido do Claude (não repetido agora)

Nos commits anteriores ele relatou ter testado duas cópias na mesma máquina: carro de campanha do parceiro, conexão IP, criação de sala EOS, F10/F8, largada comum e contador de vitórias compartilhado. A versão anterior tinha HUD remoto sem câmera e tela dividida; seu teste de chegada não valida a segurança dessa configuração. O zip antigo em `D:\dev\mc3-coop-build\MC3Coop-test.zip` antecede a largada/vitória; o novo zip fica em `C:\dev\mc3-coop\out\MC3Coop-test.zip` e não substitui o arquivo antigo fora deste workspace.
