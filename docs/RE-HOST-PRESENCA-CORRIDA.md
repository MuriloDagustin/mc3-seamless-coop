# Presença do parceiro na corrida: classificação e chegada

Análise estática em 19/10/2026. Escopo: corrida comum, vtable `0x46B2D8`, a partir dos comentários asm de `generated/game/x86_recomp.*.cpp`, consultados pelo índice existente com `tools/xref.py`. Nenhum jogo, runner, firewall ou executável de jogo foi iniciado; não houve alteração de runtime neste estudo.

## Conclusão para implementação

**CONFIRMADO:** `progress+7` não é uma chave de inclusão no ranking comum. Zerar esse byte impede a vitória e representa eliminação em vários consumidores, mas `0x1CF440` continua incluindo essa entrada na comparação. O campo sozinho NÃO cumpre “ausente não conta na classificação”.

**CONFIRMADO:** a quantidade de humanos esperada na chegada é `setup+4C`; a contagem efetiva de chegados é `race+1250`. Não foi localizado um limite de chegada por entrada nem um campo alternativo de “humanos presentes” na classe comum.

**Conclusão limitada à análise realizada:** não há exclusão nativa segura por um único flag demonstrada. Não reduzir `setup+4C`, `playerMgr+28` ou `racerMgr+28`; não compactar `race+65C`; não zerar ponteiros de player; não chamar callbacks de chegada remota. Um placeholder DNF permanece uma vaga do ranking, embora normalmente fique atrás de competidores válidos. Não anunciar isso como exclusão real.

É possível corrigir já o vazamento de estado entre mundos: exigir presença correspondente à mesma sessão/serial/identidade antes de aceitar pose, vitória, prêmio, metadata terminal ou IA do outro mundo. Isso é diferente de remover a vaga do ranking nativo. O requisito de ocultar/remover totalmente a vaga ausente permanece uma pendência de RE/implementação, não um efeito confirmado de `+7=0`.

## Layout e sinais

`race = [[0x533E24]+4]`; humano `i`: `entry = race+98+i*B8`.

| Campo | Significado e limite |
|---|---|
| `entry+4` byte | Chegada processada. Impede a callback subsequente de `1E06E0` de incrementar o contador. |
| `entry+5` byte | Preso. Entra diretamente no comparador. |
| `entry+6` byte | DNF/desistência. Entra diretamente no comparador. |
| `entry+7` byte | Elegível/não eliminado em consumidores; NÃO consultado pelo comparador comum. |
| `entry+10` float | Tempo de chegada, usado na ordenação. |
| `entry+14` dword | Colocação calculada; reescrita pelo jogo a cada atualização e antes do pagamento. |
| `entry+18` dword | Voltas restantes; `<=0` evita o processamento de volta em `1DF8B0`. |
| `entry+34` int16 | Próximo checkpoint. `-1` evita o teste de cruzamento. |
| `entry+3C` float | Métrica de progresso, usada no desempate. |
| `entry+44` dword | Pontuação, usada no ramo de modalidades por pontos. |
| `entry+5D` byte | Flag de vencedor. Não pode ser importado de corrida distinta. |
| `race+1250` dword | Humanos já contabilizados como chegados. |
| `race+1254` dword | IAs já chegadas. |
| `race+1258` dword | Máscara de humanos processados na chegada. |

**CONFIRMADO:** reset `1D11C0`, endereços `1D1205..1D120E`, zera `+4/+5/+6` e escreve `+7=1`. `1E159B` zera `+7` no caminho de eliminação e esse caminho ainda faz DNF/chegada (`1E1695..1E16C1`). Logo “eliminado” também não significa “vaga retirada”.

## Ranking: onde a exclusão não ocorre

```asm
001CF446 mov ecx, [0x00558838]
001CF44C mov edx, [ecx+0x28]      ; total de racers, não humanos presentes
001CF460 mov edx, [ebx+0x65C]
001CF466 mov ecx, [edx+eax*4]
001CF469 mov dword ptr [ecx+0x14], 0x01
```

`1CF440` percorre a tabela de progress de TODOS os racers e inicializa suas posições em1. A ordenação normal usa preso (`1CF5BF..1CF5D0`), DNF (`1CF5D2..1CF5F0`), finished e tempo (`1CF5F2..1CF609`), voltas (`1CF60E..1CF61D`), finished (`1CF61F..1CF6AD`) e métrica (`1CF6B2..1CF6C4`). Não lê `+7` em nenhum desses ramos.

Há um tratamento especial para humano cujo `Player*` é nulo e que ainda não terminou: `1CF4D9..1CF500` lhe atribui posição10; `1CF59D..1CF5BD` pula a comparação interna. **Não é uma receita segura:** outras rotinas exigem o player; e, quando `finished=1`, esse skip deixa de valer. Não fabricar ausência zerando ponteiros.

O ramo por pontuação usa `IsPlayerActive` (`2BD920`) em `1CF539..1CF547`, enquanto o comparador normal checa o ponteiro diretamente. Portanto modificar a atividade do slot não constitui filtro geral de classificação.

`entry+7` é lido por `1DF0A0`:

```asm
001DF0C9 mov cl, [esi+0x07]
001DF0CC test cl, cl
001DF0CE jnz 0x001DF0D8
001DF0D2 xor al, al              ; não vencedor
```

Também é lido por auxiliares `1D09D0`, `1D0A40`, `1D0AB0` quando seu parâmetro de filtragem está ativo. Esses auxiliares NÃO substituem `1CF440`. O HUD de chegada trata zero como perda por eliminação em `1E0B3F..1E0B52`.

**CONFIRMADO:** `1DF010` chama `1CF440` em `1DF01A` a cada update. `1CFC50` o chama novamente antes de pagar. Sobrescrever `entry+14` apenas no hook não garante posição correta no pagamento síncrono; ela pode ser recalculada dentro da mesma cadeia de chegada.

## Contagem de chegada e perigo do HUD

```asm
002693C0 mov eax, [0x0053F2E0]
002693C5 mov eax, [eax+0x4C]      ; NumberHumans, sem filtro por entrada
001DAD00 mov eax, [ecx+0x1250]    ; NumberHumansFinish
001E0744 mov al, [edi+0x9C]      ; entry+4
001E074C jnz 0x001E0C22
001E0752 mov byte ptr [edi+0x9C], 0x01
001E0759 inc [esi+0x1250]
001E0BFE call 0x002693C0
001E0C03 mov ebx, [esi+0x1250]
001E0C0B cmp ebx, eax
001E0C0D setz cl
001E0C10 add ecx, 0x06           ; estado7 somente quando igualdade
```

O finish comum continua lendo HUD sem checar nulo (`1E0AEB..1E0AF6`: `GetPlayer`, `player+58`, `HUD+50`). Não chamar `1E06E0`/`vt+64` para remoto sem HUD, nem o forfeit `1CC710`, que o chama em `1CC72C`.

`CompleteRemoteRacers` atual protege contra essa callback: zera voltas, põe CP inválido/bitmaps completos, marca DNF/finished e contabiliza a vaga remota uma vez. Isso permite a chegada local com2 humanos sem HUD remoto, mas o remoto continua presente como DNF no ranking. Restaurar `finished=0`, voltas ou checkpoints para “reentrar” ressuscitaria a callback insegura. Presença de rede não autoriza essa restauração.

O gate de vitória tardia já controla esse mesmo contador e só deve esperar resultado de peer que efetivamente entrou na mesma corrida. Não somar/subtrair `1250` em um caminho paralelo sem reconciliar quem possui a contagem e em que fase está o gate. Estado7 e pagamento são síncronos à última chegada; corrigir contador depois não reproduz pagamento/ReportRaceResult.

## Por que não reduzir contadores ou compactar arrays

`1CD380` aloca `race+65C` para `NumberHumans + race+18` em `1CD39E..1CD3B2`. Insere progress humanos, strideB8, em `1CD3D0..1CD3E7`; insere IAs, stride60, APÓS o número de humanos em `1CD400..1CD421`. O índice humano precisa continuar coincidindo com `playerMgr+8+i*4`, slots e demais tabelas.

`1CE550` calcula número de IAs como `racerMgr+28 - playerMgr+28`; `1CF6F0` usa a mesma diferença para rubber band. `1CFC50` paga com `n = racerMgr+28 - playerMgr+28 +1`. Alterar apenas a extensão do ranking altera os índices/diferenças e o prêmio; retirar só a posição final remove uma IA porque os humanos estão no início. Zerar um ponteiro em `race+65C` quebra o `mov [ecx+14],1` sem teste de nulo.

`setup slot+26!=0` faz `IsPlayerActive(2BD920)` retornar false, mesmo havendo player. `1DFE20..1DFE4C` então pula checkpoints e coloca CP/métricas inválidos; auxiliares/rubber band também o ignoram. **Candidato parcial apenas:** não exclui o ranking normal, nem altera `NumberHumans`, e tem consumidores adicionais. Não adotado nem testado neste estudo.

## Contrato de presença recomendado para o código do mod

Proposta de protocolo/lifecycle, não asm nem funcionalidade implementada por este documento:

1. A presença deve carregar um token da conexão/sessão, serial de largada do host, identidade da corrida (`city`,`race_idx`, modo/tipo e, idealmente, assinatura do setup/layout), revisão crescente e fase local (`menus/garagem/rua/carregando/correndo/resultado`). Não basta o peer repetir a serial recebida no `OnStart`: ainda pode estar na garagem.
2. Só declarar presente quando a cópia do peer realmente vinculou o objeto carregado à mesma identidade/serial. O VA do objeto serve apenas como âncora local, nunca como identidade entre PCs; também há reutilização do mesmo VA entre corridas de série.
3. Mensagem de outra corrida/serial/sessão não pode fornecer pose ao racer remoto daquela corrida, reativar vitória, prêmio, gate ou ranking terminal. Rejeitar revisão/sequence antiga antes de mutar estado. Novo handshake invalida presença antiga mesmo que serial volte a1.
4. Na ausência, manter as guardas locais sem callback, remover o peer da classificação que o próprio MOD apresentar e evitar que pose de outro mundo dirija o proxy da corrida. Isso NÃO remove a linha do HUD nativo; relatar essa limitação.
5. Ao regressar à mesma corrida comprovada, reabilitar somente os fluxos de rede permitidos e restaurar campos especificamente possuídos pelo mod quando a âncora local continua a mesma. NÃO limpar `finished`, guardas CP, `race+0D/+0E`, nem contadores para imitar uma nova largada.
6. Ao trocar objeto/serial/identidade, descartar snapshots antigos sem escrevê-los no novo objeto. Um snapshot deve registrar VA+vtable+token local de carga+identidade, bitmap de campos possuídos e valores esperados; restaurar por comparação somente no mesmo contexto. Não restaurar um bloco B8 inteiro, pois voltas/tempos/flags podem ter sido atualizados pelo jogo.
7. Para reentrada depois da própria chegada/timeout definitivo, metadata pode corrigir classificação se o protocolo admitir, mas não reabrir pagamento, prêmio ou vitória. A política de crédito é separada da presença atual.

## Pendência real

O produto pode bloquear largadas independentes e rejeitar estados de outra corrida sem depender de um novo campo guest. Já a exclusão integral do humano ausente da classificação NATIVA, preservando todos os contadores e pagamentos existentes, exige outra solução comprovada. As escritas `+7=0`, `+26=1`, `DNF=1` isoladamente não a entregam. O que este estudo confirma é o limite; não houve teste ao vivo nem tentativa de contornar esse limite com ponteiros/vtables fabricados.

## Implementação parcial em 19/10

O protocolo6 acrescenta `race::World` de8bytes aos estados de posição: tipo de mundo, cidade, índice da corrida e serial do host. `race::Context()` exige mundo de carreira, arquivo completo do setup, largada consumida e objeto de corrida vinculado; um pedido recebido ainda não é presença na corrida nova. Garagem/menu/carga sem vínculo publicam indisponível.

A recepção e a aplicação na thread do jogo conferem o contexto. Mudança de contexto descarta interpolação/relógio sem apagar CarCfg, revisão ou sequência da configuração. F8 também restringe o destino ao mesmo contexto. `RaceTick` preserva a proteção contra callback de humano sem HUD, mas recusa vitória, prêmio e metadata terminal quando o mundo carregado não corresponde à identidade ativa.

Pedidos de próxima corrida e seus setups ficam separados da identidade ativa. Receber o pedido durante outra corrida não reinicializa seu gate, resultado ou prêmio; isso acontece quando uma largada da rua pode consumir a autorização. Uma nova geração de transporte limpa serial e pendências, com cancelamento das escritas emprestadas no próximo hook.

`WorldPoseTests()` e `WorldCreditTests()` passaram em RAM sintética. A linha do ausente no ranking nativo continua pendente; o descarte de posições não remove nem realoca entradas nativas. Não foi implementado o pacote de decisão pós-corrida nem uma autorização de próxima etapa de torneio.
