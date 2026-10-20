# Host conduz o resultado e a próxima etapa — 19/10/2026

Implementado nos fontes e validado offline; **não validado no jogo**. Evidência dos endereços e consumidores nativos em `RE-HOST-NEXT.md` e `RE-HOST-SAIDA-UI.md`.

## Comportamento

- Protocolo7 acrescenta decisão pós-corrida: Wait, Street ou Next, vinculada à serial anterior e a uma revisão. Next leva a identidade e o registro da próxima corrida. Ambos os jogadores precisam do mesmo build.
- O parceiro bloqueia os comandos do controle0 do guest nos estados7..12 da corrida compartilhada, mesmo se o pacote Wait se perder. O filtro também neutraliza eixos. Não envia teclas ao Windows nem altera outros controles.
- Street percorre a opção nativa de voltar à rua e a confirmação Sim. O jogo cancela sua própria tarefa de torneio. Saída do host para garagem/menu produz a mesma decisão Street.
- Next consulta o cursor da tarefa local e compara cidade, índice e nome completo da corrida; UUIDs podem diferir entre saves. Com correspondência e setup recebido, confirma Continue no resultado, após recompensa10 e autosave11, e ativa nova serial. Se o cursor local não corresponder, sai pelo menu nativo para a rua e mantém a largada pendente do host.
- Os popups de torneio são fechados somente em contextos identificados e depois da decisão do host. Fechar um popup não troca a serial: a troca ocorre na confirmação de Continue ou na largada pela rua.
- Pulsos Up/Down/A passam por nova validação no gancho, com intervalo mínimo160ms e uma leitura neutra entre pulsos. Uma confirmação por página/decisão evita repetir A. Ao liberar, exige soltar os botões físicos para não confirmar a tela seguinte por acidente.
- O host guarda e reenvia as últimas8 decisões e setups. Isso permite ao parceiro atrasado receber a etapa intermediária mesmo quando o host já avançou. Histórico finito: não oferece recuperação ilimitada de atraso.

Não se chama código guest, não se fabrica menu, não se escreve cursor da tarefa e não se força estado13. Os planejadores apenas leem RAM; os comandos passam pelo XInput do guest. Prêmio e chegada anteriores permanecem ativos até a confirmação da próxima etapa.

## Verificação

`python tools/test_coop_memory.py` passou. O harness compila o código real e simula RAM e input: espera sem Wait; navegação Quit/Sim com padrão Não; revisões antigas; menu desconhecido; popup normal e overlay; próxima etapa com UUID local diferente; setup obrigatório; troca de cursor entre planejamento e input; endereço da corrida reutilizado; resultado atrasado da etapa anterior; retransmissão de setups antigos; reset de rede. Também passaram as suítes existentes de IA, tráfego, chegada/prêmio e configuração do carro.

`cmd /c mod\build.cmd` passou. Logs locais: `out/test-host-flow-memory.log` e `out/build-host-after.log`. Avisos do ImGui já existentes. Nenhum jogo, launcher, socket de teste, janela ou comando de firewall foi aberto. Não houve novo ZIP.

## Limites e próximos testes

Ainda é necessário provar com duas cópias: disponibilidade dos menus, tempo entre input e carga, aplicação do setup antes da alocação, série de pelo menos duas etapas, crédito/prêmio únicos, abandono do host e fallback pela rua entre carreiras diferentes. Modalidades1/3 não entram neste controlador; as etapas de torneio examinadas nos assets são7/21.

Menus sem a opção nativa de rua ou com assinatura desconhecida ficam aguardando, sem forçar saída. Carga não confirmada após10s mostra aviso para desconectar pelo F10. A implementação não remove o ausente da classificação nativa: a vaga pode aparecer concluída/DNF. Troca de modelo do carro ainda requer recarregar o mundo. Internet/EOS entre PCs e colisões físicas continuam com as limitações dos relatórios anteriores.
