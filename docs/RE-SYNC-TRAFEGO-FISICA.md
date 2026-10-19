# Sincronização de reboques e estado físico do tráfego

18/10/2026. Desenvolvido na worktree independente `codex-sincronizacao-trafego`, base `dc25ed3` da `coop-noite`. Posteriormente, por autorização do usuário, os fontes e testes foram integrados à principal `C:\dev\mc3-coop`, sem commit/merge/push. A branch local antiga chamada `codex` impede o namespace Git `codex/...`; por isso o nome usa hífen. O ZIP privado já entregue e `D:\Jogos` permanecem intactos.

## Estado da implementação

**IMPLEMENTADO, validado em RAM simulada; SEM validação no jogo.** A nova recepção é opt-in por `MC3COOP_TRAFFIC_PHYSICS=1`. Ausente/0 mantém o tratamento anterior de puppets comuns; o formato da rede mudou em ambos os modos. Protocolo de sessão **5**, tráfego interno versão **2**: não misturar com a versão 4 do ZIP entregue.

O host captura os snapshots no gancho da thread do jogo (`traffic::Capture`, chamado em `FiberTick`). A thread de rede apenas envia a cópia pronta; não percorre corpos em atualização. O parceiro aplica dados no mesmo gancho. Não há chamadas a funções Xbox, patches de código, criação artificial de collider ou modificação de listas livres.

Cada registro inclui pose, velocidade linear e angular, centro de massa, dano/incapacitação, pose articulada do reboque e velocidade/rotação/centro do seu segundo corpo. Os valores não incluem ponteiros. A associação continua sendo slot e assinatura ordenada de tipo/variante/cidade/horário. Acrescentada chave do nome completo da corrida e geração do mundo do host.

São oito carros por datagrama: registro de 156 bytes, cabeçalho de tráfego de 24 e cabeçalho de sessão de 20, máximo de **1.292 bytes**. O ciclo inclui slots inativos explicitamente. O receptor só publica um ciclo completo, com dimensões/ordem de slots verificadas; duplicatas, versões/gerações antigas, vetores inválidos, NaN e quadros incompletos são recusados. Um ciclo expirado não renova o prazo por retransmissão duplicada.

## Caminhão e reboque

**CONFIRMADO em asm:** `slot+0xEC` é o objeto de articulação, com matriz em `+0x13C`. Pode haver um segundo collider em `reboque+0xA4`; esse segundo corpo também precisa de correção. Ver [evidência detalhada](RE-SYNC-TRAFEGO-ASM.md).

A recepção escreve a matriz do reboque e as matrizes relativas das submalhas, reproduzindo em C++ a operação `reboque * inverse(cavalo)` de `0x72B40`. Isso atualiza os dados usados pelo desenho e pelo bound composto sem esperar a atualização nativa. Tipo, variante, contagem e endereço das matrizes locais são validados antes de qualquer escrita.

Se há dois corpos locais, ambos recebem pose, velocidades e momento. O joint deve apontar para os dois corpos locais correspondentes (`reboque+4/+8`); ponteiros estranhos são recusados, nunca consertados com endereços do host.

## Corpo físico de acidente

`slot+0x13C` aponta para o **collider**; o corpo fica em `collider+0x10`. A validação confere dono, instância, handle, corpo embutido, matriz, massa e inércia. O alias da matriz precisa corresponder ao flag do bound, conforme `0x2A3340`.

Corpos existentes recebem rotação/posição física, matriz visível, velocidade linear/angular e os momentos correspondentes. A posição visual é separada do centro de massa. Não se sobrescrevem forças, impulsos pendentes, referências do joint ou estado de alocação/ativação do motor.

**Quando o host tem acidente e o parceiro ainda não tem corpo**, a matriz segue o host por um proxy cinemático. O mod não cria um collider por escrita de ponteiros. Esse proxy não reproduz integralmente o dano nativo: dano é limitado abaixo do ramo que suspende a atualização e o flag de incapacitado fica zero enquanto não existe corpo local.

## Localização na grade de colisão

O caminho experimental mantém os links de estrada e usa `C4=1`, `AC=0`, `A8=0`, com LOD próximo, `B2=0` e velocidade de entrada mínima 0,1. O ramo nativo de `0x78940` reconstrói a matriz e chama `UpdateObjectLocation`, além da atualização de proximidade de `0x80D10`. Links nulos ou scripts nativos não são tomados pelo mod. Corpos ativos continuam no caminho físico nativo.

**CONFIRMADO:** esses ramos existem. **HIPÓTESE DE INTEGRAÇÃO:** a frequência/ordem do gancho mantém a posição e a articulação com erro aceitável e a grade física acompanha corretamente. A rotina nativa pode recompor orientação e articulação depois da aplicação. Os testes de RAM não executam essas rotinas; portanto não comprovam colisão nem articulação durante uma curva real.

Slots inativos no host são escondidos movendo os dois corpos, a articulação e as submalhas. A desconexão/expiração restaura o último estado visível e os flags originais do modo scriptado, somente se mundo/pool/handle/tipo/reboque ainda correspondem. A mudança de resultados libera a posse antes do reset observado pelo gancho; alterações de alocação descartam endereços antigos.

## O que continua faltando

- **Batida causada apenas pelo parceiro:** ainda não há evento de impulso parceiro→host. O host continua simulando os contatos de seu mundo, incluindo o carro remoto. Não está comprovado que verá todo contato ocorrido no outro PC; se não o vir, a correção seguinte pode desfazer o acidente local. Este trabalho replica o estado físico decidido pelo host, não resolve toda a autoridade bidirecional dos impactos.
- Sem corpo local, o proxy não equivale a um acidente físico completo. A criação/remoção e a ativação/suspensão do corpo continuam nativas.
- Remoção de corpo pelo motor pode zerar links; esse caso é recusado em vez de acessar estrada nula. `blocked` permite localizar esses slots.
- Faltam testes reais de reta/curva, contato com cavalo e carreta, grade após deslocamento entre células, carro capotado, recuperação, troca de cidade, reset e desconexão. Os limites de câmera/foco e a reescrita nativa precisam ser medidos.
- Não muda colisão jogador↔jogador ou IA; o escopo implementado é o estado do tráfego e reboques.

## Validação feita nesta worktree

`python tools/test_coop_memory.py`: passou com os fontes reais do mod e RAM sintética. Inclui a suíte anterior de IA/chegada e a nova suíte `traffic_memory_tests.inc`:

- dois chunks em ordem invertida; nenhum lote parcial aplicado; MTU;
- caminhão articulado a 90 graus, submalha relativa e os dois corpos;
- centro de massa com/sem alias e momentos linear/angular; forças e impulsos existentes preservados;
- proxy sem criar corpo; corpo local de acidente corrigido para o estado normal do host;
- NaN, versões/tamanhos/slots inválidos, duplicatas e ciclos antigos;
- recusa de script nativo, links nulos, tipo incompatível, matriz/handle/dono/joint inválidos e inércia zero;
- ocultação/restauração dos dois corpos, timeout, troca de handle/mundo e publicação a partir de captura imutável.

`mod/build.cmd` compilou a DLL nesta própria worktree com `MC3COOP_BUILD_OUT=<worktree>\out\mod`. Artefatos não substituem os da pasta principal. Os avisos do build são nos arquivos preexistentes/terceiros, sem erro de compilação.

Nenhum runner/jogo/lançador foi executado, nenhuma regra de firewall alterada, nenhum diálogo de elevação aberto e nenhum processo alheio encerrado. O binário de teste da suíte é somente um programa de RAM simulada.

## Próximo ensaio

Manter a suspensão de testes ao vivo do handoff até existir isolamento silencioso comprovado e regra prévia para o caminho fixo de teste. Não usar o lançador/firewall para obter essa autorização. Nas duas cópias descartáveis, usar a DLL desta worktree e o mesmo protocolo, habilitando `MC3COOP_TRAFFIC_PHYSICS=1` no parceiro.

Os diagnósticos `physics`, `trailers`, `bodies`, `proxies`, `blocked`, `epoch` e `cycle` foram acrescentados ao estado do mod. Primeiro verificar um caminhão em reta/curva e contato com cada parte; depois um acidente no host, um no parceiro, recuperação e transições. Não distribuir outro ZIP nem anunciar colisões sincronizadas antes dessa validação.
