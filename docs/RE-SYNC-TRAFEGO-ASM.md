# Tráfego, reboques e corpos de colisão — evidência estática

Análise em 2026-10-18. Recursos: `C:\dev\mc3-coop\generated\game\x86_recomp.*.cpp` e índice `out/xref.json`, consultados por busca e `python tools/xref.py asm 0x...`. Não executei o jogo, funções guest nem testes ao vivo. Não alterei o runtime nesta investigação.

**CONFIRMADO** significa que a cadeia ou operação aparece nas instruções citadas. **PALPITE / A TESTAR** identifica consequências que dependem da fase do hook ou do estado do motor. A revisão abrange `mod/src/traffic.cpp` e `traffic_physics.inc` da worktree `sincronizacao-trafego`; o código pode receber reforços enquanto este documento é escrito.

## 1. Cadeias locais: collider não é corpo

Use `s` para um slot de tráfego de tamanho `0x21C`.

| Objeto | Cadeia/offset | Estado |
|---|---|---|
| phInst principal | `s+0x24` | CONFIRMADO |
| matriz visual principal, 12 floats | `s+0x34` | CONFIRMADO |
| matriz anterior no slot | `s+0x74` | CONFIRMADO |
| collider principal `C` | `[s+0x13C]` | CONFIRMADO |
| corpo principal `B` | `[C+0x10] == C+0x80` | CONFIRMADO para os colliders de tráfego destas rotinas |
| dono do collider | `[C+0x78] == s` | CONFIRMADO |
| phInst do collider | `[C+4] == s+0x24` | CONFIRMADO |
| identificador de instância | `u16[C+8]`, corresponde ao handle local do phInst | CONFIRMADO |
| container de articulação `T` | `[s+0xEC]` | CONFIRMADO; não é collider |
| phInst do reboque | `T+0x12C` | CONFIRMADO |
| matriz visual do reboque | `T+0x13C` | CONFIRMADO |
| handle do reboque | `u16[T+0x134]` | CONFIRMADO |
| collider do reboque `TC` | `[T+0xA4]` | CONFIRMADO |
| corpo do reboque `TB` | `[TC+0x10] == TC+0x80` | CONFIRMADO |
| vínculo do collider do reboque | `[TC+4] == T+0x12C`, `[TC+0x78] == s` | CONFIRMADO |

Evidência principal:

```asm
0006FF1F mov [esi+0x78], ecx       ; collider principal: owner=s
0006FF2D lea ebx, [esi+0x80]      ; corpo embutido
00070002 lea ecx, [eax+0x24]      ; phInst principal
00070010 push ebx                ; body
00070011 push ecx                ; inst
00070014 call 0x002A4590
002A459E mov [esi+0x10], eax      ; collider.body
002A45AF call 0x002A3480
002A3349 mov [esi+0x04], eax      ; collider.inst

00073561 call 0x0006A3F0          ; segundo collider, para reboque
00073573 call 0x00070130          ; inicializa collider do cavalo
0007357B mov [esi+0x13C], ebx
00073581 call 0x00070480          ; inicializa collider do reboque
00073586 mov edx, [esi+0xEC]
0007358F mov [edx+0xA4], edi      ; T.collider do reboque
0007048D mov [esi+0x78], eax      ; owner continua sendo s
00070611 add ebp, 0x12C          ; phInst do reboque
00070721 lea eax, [esi+0x80]      ; corpo do reboque
00070727 push eax
00070728 push ebp
0007072B call 0x002A4590
```

**Regras de ownership:** validar memória, owner, phInst, handle, corpo embutido e aliases antes de qualquer escrita. Os ponteiros são exclusivamente locais. Não transmitir/copiar ponteiros, trocar `s+13C`, `T+A4`, ou escrever uma cadeia artificial para produzir um corpo inexistente.

## 2. Matriz visual, centro de massa e caches

**CONFIRMADO:** a matriz de corpo começa em `B+0x5C`; rotação são os primeiros nove floats e a translação/centro de massa fica em `B+0x80..0x88`. A matriz visual do phInst usa a origem visual, que pode ser diferente do centro de massa.

`bound=[C+0x0C]`; sua flag byte `+7` seleciona a representação:

```asm
002A338B mov cl, [eax+0x07]
002A3390 jnz 0x002A339F
002A3392 mov edx, [esi+0x10]
002A3395 add edx, 0x5C
002A3398 mov [esi+0x14], edx      ; bound+7=0: matrix=body+5C
002A339F mov ecx, [esi+0x04]
002A33A2 add ecx, 0x10
002A33A5 mov [esi+0x14], ecx      ; bound+7!=0: matrix=inst+10
```

Na segunda representação, `2A33A8..2A3450` soma o centro local `bound+2C/+30/+34`, rotacionado pela matriz, à posição de corpo. `2A37B0` faz a transformação inversa após integrar o corpo. Para vetores linha:

```text
COM[j] = visible_position[j] + sum(offset_local[i] * rotation[i*3+j])
```

Quando `bound+7=0`, a posição do corpo coincide com a posição visual. Portanto `C+14==B+5C` é um alias legítimo, não um erro de ownership. A condição de alias deve corresponder à flag do bound; aceitar qualquer um dos dois sem verificar essa relação perde uma invariável útil.

**CONFIRMADO:** `C+18..44` é a matriz anterior do collider, copiada de `[C+14]` no início da integração. `C+48..50` e `C+54..5C` são snapshots de velocidade linear e angular:

```asm
002A3916 mov ecx, [eax+0x44]
002A3919 mov [esi+0x48], ecx      ; início do snapshot de velocidade
002A392E mov ecx, [eax+0x50]
002A3934 mov [esi+0x54], ecx      ; início do snapshot angular
002A394B mov eax, [esi+0x14]
002A394E ...                     ; copia 48 bytes para collider+18
002A3995 ...
```

`79BB0`/`79C85` usa a matriz anterior para atualizar a pose anterior do slot. No estado nativo coerente, escrever a matriz visual também em `C+18` é compatível: se há deslocamento de COM, `C+14` aponta para o visual; se aponta para o corpo, COM e origem visual coincidem. O comentário “sempre matriz visual” seria impreciso sem explicitar essa invariável.

**A TESTAR:** aplicar estado depois do snapshot nativo pode deixar os caches de velocidade anteriores até o próximo `2A3910`. O trace prova seus offsets e o refresh, mas não prova que seja necessário sobrescrevê-los no hook atual. Não alterar flags de integração nem buffers por suposição.

## 3. Velocidades, massa, momentum e impulsos

| Campo de corpo | Significado | Evidência |
|---|---|---|
| `+04`, `+08` | massa e inverso da massa | `341540`, reciprocal em `34156E` |
| `+0C..14` | inércias principais | `341540`, `343300` |
| `+18..20` | inversos das inércias | `3415CB`, `3415EB`, `341602` |
| `+24`, `+28` | limites de velocidade e velocidade angular | `343910`, `343450` |
| `+2C..34` | momentum linear `P` | `3432B0`, `343910` |
| `+38..40` | momentum angular `L` | `343300`, `343450` |
| `+44..4C` | velocidade linear | `3432B0` |
| `+50..58` | velocidade angular | `343300` |
| `+8C..94` | acumulador de força | `341C40`, `341C90`, `342E20` |
| `+98..A0` | acumulador de torque | `341C90`, `342E20` |
| `+A4..AC` | acumulador de impulso linear instantâneo | `3420F0`, `342140`, `343910` |
| `+B0..B8` | acumulador de impulso angular instantâneo | `342140`, `343910` |

Todos os significados da tabela são **CONFIRMADOS**. Em particular `body+18/+24` não são caches de pose.

`3432B0` armazena a velocidade e recalcula `P=m*v`. `343300` armazena omega e recalcula `L` usando a rotação e a inércia principal. A fórmula equivalente com vetores linha e matriz cujas linhas são os eixos locais é:

```text
omega_local[i] = sum(omega_world[k] * R[i*3+k])
L_world[j] = sum(omega_local[i] * I[i] * R[i*3+j])
```

Essa é a convenção usada em `WriteBody` revisado. Escrever somente as velocidades deixa o momentum anterior e permite que a integração recupere valores incompatíveis.

Impulsos são distintos das forças:

```asm
00342144 movss xmm0, dword ptr [ecx+0xA4]
0034214C addss xmm0, [eax]        ; J recebido como argumento
00342154 movss [ecx+0xA4], xmm0
00342186 movss xmm0, dword ptr [edx]
0034218A subss xmm0, [ecx+0x80]   ; ponto - COM
; 342140 acumula também (ponto-COM) x J em body+B0..B8
0034394D movss xmm0, dword ptr [edx+0xA4]
00343955 addss xmm0, [edx+0x2C]
0034395A movss [edx+0x2C], xmm0   ; soma J ao momentum
; 343983 deriva velocidade com invMass; 3439CD limita velocidade
; 343A63..73 limpa impulso linear; 343AAE soma impulso angular a L
; 343AEB..FB limpa impulso angular
```

`3447E0` também integra esses buffers, com força multiplicada por dt separadamente. `343670` limpa forças, torques e impulsos; limpar tudo ao sincronizar pose descartaria contribuições do solver.

**PALPITE / NÃO IMPLEMENTADO:** uma mensagem validada e deduplicada poderia acumular J nesses seis floats do corpo já existente no host, na thread do jogo. Ainda falta comprovar a fase do hook frente a `343670`, atribuição do impacto e ausência de aplicação dupla com a colisão nativa. Não é receita pronta para produzir colisões cliente→host.

## 4. Articulação: dois corpos e ligações permanentes

**CONFIRMADO:** `T+4` é o corpo do cavalo e `T+8` o corpo do reboque. A ordem não é intercambiável:

```asm
0007361E add edi, 0x80           ; TB
0007362C push edi
00073641 add ebx, 0x80           ; B
00073647 push ebx
00073648 call [eax+0x04]         ; init da articulação em T
; AAD90 preserva ordem e repassa para 345080
000AADB5 call 0x00345080
00345080 mov eax, [esp+0x04]     ; arg1: B
00345084 mov [ecx+0x04], eax
00345087 mov edx, [esp+0x08]     ; arg2: TB
0034508B mov [ecx+0x08], edx
0034508E mov [eax+0x1A4], ecx    ; backlink B->T
00345097 mov [eax+0x1A4], ecx    ; backlink TB->T após reler [T+8]
000AAF46 mov eax, [esi+0x04]     ; solver lê body1 matriz/COM
000AAFD9 mov eax, [esi+0x08]     ; solver lê body2 matriz/COM
```

Guard recomendado, antes de escrever qualquer parte da pose física articulada: `[T+4]==B`, `[T+8]==TB`; reforço de ownership: `[B+1A4]==T` e `[TB+1A4]==T`. Validar, nunca “consertar” os ponteiros por escrita. O trace do solver em `AAF30` usa diretamente os dois corpos e aplica forças em ambos.

Só transmitir `T+13C` deixa o corpo do reboque em outra pose e o solver pode corrigir a junção usando o estado antigo. Transmitir também COM, velocidade e omega de `TB`, com momentum coerente, elimina essa inconsistência de dados; não prova estabilidade física em rede por si só.

## 5. Reboque visual e bounds compostos

**CONFIRMADO:** `72B40` atualiza todas as matrizes secundárias do bound composto:

```text
descriptor = [[s+28]+0C]
descriptor byte+4 == 0x10
count = u16[descriptor+4E]
matrices = [descriptor+5C]
para i=1..count-1: matrices[i] = trailer_world * inverse(main_world)
```

A rotina passa por `87B80` (inversa rígida) e `85930` (produto). O produto implementado em `WriteTrailer`, inclusive a translação `(trailer_position-main_position)*R_main^T`, corresponde à convenção de vetores linha. Gravar a mesma matriz relativa em cada secundária corresponde ao loop nativo; não é necessário inventar offsets por eixo/submesh.

Validar modelo, quantidade, faixa completa das matrizes e a presença de reboque antes da primeira escrita. Evitar aplicar um snapshot de modelo articulado a um slot não articulado.

## 6. Corpo ausente e fallback cinemático

**CONFIRMADO:** a criação envolve pool, registro de instância e estruturas do motor, não apenas um ponteiro:

```asm
000734E6 mov eax, [esi+0x13C]
000734EE jz 0x000734F7
00073504 call 0x0006A3F0         ; obtém collider do pool
0007353C call 0x0006FF10         ; inicialização/registro
00073541 mov [esi+0x13C], ebx
```

`6A3F0/70EE0` altera listas do pool (`collider+284..290`). `794D0` faz a liberação por `700E0`, ou `70400` e `707C0` para conjunto articulado, e zera `s+13C` em `79523`. `707C0` consulta `owner->T->handle`, remove objeto, limpa owner e devolve o collider ao pool. Não reproduzir essas operações manualmente.

Casos mínimos:

- Host e parceiro têm corpos válidos: aplicar estado nos corpos locais existentes e manter todas as ligações nativas.
- Host sem corpo, parceiro com corpo: derivar COM da pose visual e do bound local; não usar COM zerado de um snapshot sem corpo.
- Host com corpo, parceiro sem corpo: aplicar proxy visual/cinemático até o motor local criar corpo por seu fluxo normal. Isso não reproduz a massa, a junção nem a reação de um wreck físico.
- Um corpo local desapareceu ou mudou de dono: rejeitar a escrita física; nunca restaurar ponteiros antigos do pool.

No caminho cinemático `C4=1`, `AC=0` pode suspender o movimento autônomo sem retirar o objeto da estrutura de ruas. `72570` com AC=0 zera E0 e retorna; `78570` retorna. Mas há outras condições para manter atualização:

```asm
; 80D10: slot+13C != 0 -> 79BB0, caminho físico
; sem corpo, B1 deve permitir entrar no loop; B2 pode pular78940
; 78940 também tem saída alternativa se disabled14E ou damage14B>=7F
00078A54 cmp dword ptr [esi+0xC4], 0x01
00078A65 call 0x00072570
00078A6D call 0x00078570
00078AA9 call 0x000767B0         ; reconstrói pose por anchor/heading
00078AB0 call 0x00076980         ; reconstrói pose do reboque
00078ACC call [eax+0x20]         ; UpdateObjectLocation
; 80DDA:80C10 pode despawn; 80DE6:2B74B0 atualiza room/grid
```

`76250` ainda consulta os links locais `s+FC/+100` em um ramo de look-ahead mesmo com AC=0/A8=0. Preservar e validar esses links; não zerar ruas, A8 ou listas para “desligar a IA”. Salvar/restaurar C4, AC, B1 e B2 só enquanto identidade do mundo/slot/modelo/handle/reboque continuar a mesma.

**Limite confirmado:** o tick cinemático reconstrói a matriz principal e a do reboque; pode substituir roll/pitch e articulação recebidos. **A TESTAR:** com a ordem real do hook e a próxima renderização, medir quanto permanece visível e se `UpdateObjectLocation` mantém o broadphase consistente. Não prometer pose rígida completa quando o corpo local não existe.

`C+278` não é dano: `70DFF..70E11` soma dt, e as inicializações zeram o campo. É idade/tempo do estado físico. Não retransmitir como “damage”, nem zerar a cada frame.

## 7. Impacto causado pelo parceiro

Não achei um campo universal comprovado “último contato veio do humano local”. Os acumuladores do corpo agregam todas as fontes e não identificam o autor.

**CONFIRMADO, mas restrito:** `6AFC0` consulta `GetPlayer(0)` em `6AFC7/6AFCD`; o ramo exige estados do jogador `+F8==2` e `+F4==3`, portanto é um caminho especial de ram/ability. `6B1BE` marca tráfego+14F; `6B224..235` passa o corpo de tráfego para `2BE2F0`, ring de 16 pares ponteiro/tempo em `player+128`. Isso é cooldown/deduplicação desse efeito, não registro geral de contato, normal e impulso.

`777E0`/PrepareForActivation pode ativar tráfego através de contato genérico sem tag de jogador. `+AE`, `+14F`, idade do collider e força acumulada não bastam para atribuir colisão humana comum.

**PALPITE A TESTAR:** o carro remoto já existente fisicamente no host pode provocar a reação nativa do tráfego no host; sincronizar o wreck resultante para o parceiro é o caminho mínimo. A pose por rede pode atravessar objetos entre amostras. Reencaminhar também um impulso do cliente pode duplicar um contato já resolvido no host.

Estado suportado pelo trace e pelo desenho atual: **host→parceiro, pose/velocidades de corpos existentes e proxy quando ausentes**. Física completa de colisões parceiro→host continua pendente de identificação do evento, deduplicação, validação e teste da fase de consumo. Não há base para afirmar que está pronta apenas por sincronizar matrizes.

## 8. Revisão do runtime e validação restante

Não encontrei erro factual nas cadeias `ReadCrash`/`ReadTrailerBody`, no cálculo de momentum de `WriteBody` ou no produto relativo de `WriteTrailer` revisados. Os reforços de alias/bound e links da articulação acima foram comunicados ao agente principal. A invariável precisa ser testada nos dois casos de bound, com e sem deslocamento de COM.

Verificações úteis em harness, sem executar guest: rejeitar owner/handle/alias/joint incompatíveis sem nenhuma escrita parcial; recomputar COM com rotação não identidade; verificar momentum para inércias diferentes; aplicar snapshot de trailer com ambos os corpos e conferir links intactos; testar desaparecimento de corpo e troca de geração do slot; garantir restauração de C4/AC/B1/B2 apenas no proprietário original.

Testes ao vivo ainda necessários: corpos de carro comum e de caminhão com reboque; tombamento, repouso/liberação nativa, despawn e mudança de mundo; host com corpo/parceiro sem corpo e o inverso; perda de pacotes; reação ao carro remoto no host; comparação de matriz/COM/velocidades antes e depois de um tick nativo. Nenhum desses testes foi realizado por esta análise estática.
