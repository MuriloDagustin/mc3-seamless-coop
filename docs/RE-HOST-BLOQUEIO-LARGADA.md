# Host decide a largada: bloqueio de rua no parceiro

Análise estática de 19/10/2026 na principal `C:\dev\mc3-coop`, recebida em `780a698`. Nenhum jogo, runner, lançador ou alteração de firewall executado. Esta subtarefa não editou runtime. VAs do Xbox; host = base + VA. CONFIRMADO indica evidência estática; PALPITE indica integração ainda não validada no jogo.

## Resultado

**CONFIRMADO:** zerar `cruise+0x1250` sozinho é insuficiente: ele só protege o marcador e é rearmado pelo update. Hookman/rival tem máquina própria e ignora esse byte.

**CONFIRMADO:** todos os chamadores diretos encontrados de `mcPgCareer::StartRace 0x277990` são alcançados pelo update da rua e têm guarda anterior de `cruise+0x0D==0`. Esse byte é o indicador comum de resultado já reportado. Não é um campo dedicado a permissão de iniciar.

**PALPITE de implementação com consumidor completo identificado:** emprestar `cruise+0x0D=1` somente enquanto um parceiro conectado está em cruise livre e sem largada autorizada; isso fecha os quatro caminhos examinados, inclusive desafio já ativo. Restaurar o byte antes de preparar a largada recebida do host, desconectar ou sair do contexto. Exige propriedade explícita da escrita e identidade do mundo; não reutilizar o endereço depois de recarga.

## Contexto permitido

Antes de qualquer escrita de bloqueio, conferir pelo menos:

- cópia conectada como parceiro, não host e não sessão nativa;
- `[0x53F2D4]==2`, carreira; garagem é 5 e front end é 3;
- setup CURRENT `[0x53F2E0]` válido, `setup+0x18==1`, `setup+0x108==0`;
- corrida atual `[[0x533E24]+4]` válida, vtable exatamente `0x46A7D8`;
- race state machine `[0x557EEC]`, estado de condução anterior a 7;
- carreira `[0x557F0C]` e seus dados `+0x0C` válidos; não apagar/alterar um pedido nativo `career+0x2C==4` depois do disparo;
- nenhuma fase de armamento pertencente à serial/record id autorizada pelo host.

Guardar como identidade de posse: ponteiros de corrida/holder, setup, carreira/dados, cidade e geração/serial de sessão. Para restaurar, conferir novamente ponteiros/vtable/contexto, além de o byte ainda conter o valor que o mod emprestou. Se o contexto já virou corrida ou outra geração, descartar a posse sem tocar o objeto antigo. Não restaurar um `+0D=1` observado antes da posse: ele pode ter sido produzido pelo jogo.

## Marcador: farol e flag de armado

**CONFIRMADO:** a função `0x1D1550` só aceita cruzamento se o desenho do carro local tiver `+0x132 != 0`, estado dos faróis:

```asm
001D1557 mov eax, [ecx+0x14]     ; player -> vehicle
001D155A mov edx, [eax+0x0C]     ; desenho
001D155D mov al, [edx+0x132]
001D1563 test al, al
001D1565 jz 0x001D15AA
```

Depois do cruzamento o update `0x1D8F20` exige armado e dispara:

```asm
001D910D mov al, [esi+0x0D]
001D9110 test al, al
001D9112 jnz 0x001D91B5         ; já reportado: não dispara
...
001D9124 call 0x001D1550
001D9131 mov al, [esi+0x1250]
001D9137 test al, al
001D913E jz 0x001D91B5
...
001D9168 call 0x001D6750         ; Prepare_To_Race, resultados locais, estado7
001D916F mov cx, [ebx+0x04]      ; UniqueId
001D917A call 0x00277990
001D9188 mov byte ptr [esi+0x0D], 0x01
```

Sem nenhum marcador cruzado, o mesmo update rearma automaticamente:

```asm
001D91D7 mov al, [esi+0x1250]
001D91DD test al, al
001D91DF jnz 0x001D91E8
001D91E1 mov byte ptr [esi+0x1250], 0x01
```

**Consequência:** zerar armado por tick depende da frequência do gancho e não cobre desafio de IA. Filtrar apenas o botão de farol também não desliga farol já ligado nem o bypass existente no driver.

## Hookman/rival: máquina do driver

**CONFIRMADO:** o driver de classe vtable `0x46BC48`, tipo `driver+8==2`, tem estado de desafio em `+0x1C0`:

| Estado | Consumidor |
|---|---|
| 0 | livre, procura nova aproximação |
| 1/2/3 | `0x1E5420` retorna true; desafio/caminho de corrida até largada |
| 4/5 | `0x1E5490` retorna true; seguir rival |
| 6 | `0x1E54F0` retorna true; início direto |

Os nomes dos estados são inferência funcional; os grupos numéricos são confirmados. `0x1E5570` move 1→2→3 e 4→5, além de detectar novos desafios e escrever 1/2/4/6. Por isso só impedir novo input não resolve um desafio já existente.

Novo desafio exige:

```asm
001E5940 mov edx, [ebp+0x0C]
001E5943 mov al, [edx+0x132]     ; farol do humano
001E5949 test al, al
001E594B jnz 0x001E5957
001E594D mov al, [esi+0x254]     ; bypass: não depende do botão/farol
001E5953 test al, al
001E5955 jz 0x001E59CE
001E5957 mov al, [0x005341A0]    ; token global
001E595C test al, al
001E595E jz 0x001E59CE
...
001E5974 movss xmm0, dword ptr [esi+0x250]
001E597C ucomiss xmm0, [0x00448990] ; zero
001E5984 test ah, 0x44
001E5987 jp 0x001E5AF8           ; cooldown diferente de zero: não inicia
```

**Alternativa PALPITE:** renovar cooldown positivo `driver+0x250` em drivers validados impede novas abordagens. O update decrementa esse float e o limita a zero (`0x1E559A..1E55BA`). Há uso nativo como cooldown ao aproximar a polícia (`0x1E55F4..1E55FC`). Contudo isso não cancela estados 1..6 já iniciados; não é substituto do bloqueio do update de cruise. Não zerar `driver+0x1C0` arbitrariamente: o reset nativo `0x1E52D0` tem várias chamadas e efeitos de rota, habilidade e desenho que uma escrita isolada não reproduz.

## Todos os caminhos para StartRace encontrados

Consulta `callers 0x277990` encontrou `0x1D65C0`, `0x1D6680`, `0x1D8A10`, `0x1D8F20` e fragmentos duplicados deste último (`0x1D905A`, `0x1D909D`).

1. **Marcador:** guarda `+0D` em `0x1D910D`, chamada em `0x1D917A`.
2. **Hookman em disputa até a largada:** `0x1D9330` coleta estados dos drivers e só chama `0x1D65C0` se `+0D==0`:

```asm
001D94B1 mov al, [esi+0x0D]
001D94B4 test al, al
001D94B6 jnz 0x001D96E7
...
001D94D0 call 0x001D65C0
```

3. **Início direto do rival:** o restante do mesmo update também é guardado antes de chegar a `0x1D8A10`:

```asm
001D94E2 mov al, [esi+0x0D]
001D94E5 test al, al
001D94E7 jnz 0x001D96E7
...
001D96E2 call 0x001D8A10
```

4. **Chegada ao marcador durante seguir rival:** o update `0x1D8F20` só alcança `0x1D6680` com `+0D==0`:

```asm
001D9284 mov cl, [esi+0x0D]
001D9287 test cl, cl
001D9289 jnz 0x001D92E3
...
001D92DE call 0x001D6680
```

As quatro funções finais não são pontos de polling apropriados para veto posterior: já produzem HUD, contadores e mudança de estado antes/depois de `StartRace`. Bloquear antes é preferível a tentar apagar `career+2C=4` depois.

## Limitações do empréstimo de +0D

**CONFIRMADO:** `+0D` tem significado comum mais amplo. `0x1D1810` o usa no reporte de resultado e o torna 1; `0x1CFD10` pula processamento quando já é 1; reset `0x1D1870` o torna 0. Portanto a proposta só vale no cruise exato e fora da transição, nunca nos objetos de corrida normais ou na tela de resultado.

**CONFIRMADO:** o menu em jogo `0x2C0C60` e sua guarda `0x2C0C10` não consultam `cruise+0D`. Eles usam estados globais de jogo/frontend, flags da janela/menu, pad e modo de carreira. Assim a análise não mostra bloqueio da entrada manual na garagem por esse byte. **PALPITE:** preservação completa da garagem deve ser conferida no teste real; limitar escrita a modo2 garante que nada seja aplicado ao mundo da garagem.

O empréstimo não limpa a IA que já estava num desafio: seu veículo ainda pode seguir sua rota local. O que a guarda comprovada impede é esse desafio iniciar a corrida por conta própria. Para eliminar também todas as mensagens de abordagem pode-se combinar cooldown/input, mas não reutilizar reset parcial de driver.

## Liberação para o host

O caminho existente `kRaceStart → OnStart → PartnerTick` usa o próprio marcador, arma `+1250`, limpa `+1258/+125C`, mantém farol e faz teleporte temporário. Ele testa `cruise+0D` antes de preparar a corrida.

Requisitos de integração:

1. `kRaceStart` deve ser aceito exclusivamente do host da sessão atual.
2. Restaurar o empréstimo antes de executar `PartnerTick` para uma serial pendente válida. Não usar apenas `g_have_start` como liberação: esse bool continua true depois que a serial já foi consumida.
3. Durante `g_phase` autorizada, não repor o bloqueio; conferir o UniqueId, cidade/RaceIdx e a identidade da serial.
4. Quando a largada ocorreu, o `+0D=1` passa a pertencer ao jogo; não restaurar esse valor durante estados7..12/carregamento.
5. Se o parceiro está na garagem, reter o pedido e avisar; não escrever ponteiros/flags da corrida antiga para forçar sua saída.
6. Ao voltar a outro cruise sem armamento autorizado, adquirir novo empréstimo após validar sua geração e contexto.

## Verificação necessária

- RAM: acquire/renew/release idempotentes; não escrever host/offline/native net/garagem/frontend/corrida normal; mesma VA com outro vtable/geração; largada autorizada restaura antes do teste; serial consumida não libera novamente; desconexão restaura só propriedade válida.
- Jogo: parceiro parado no marcador com farol, passando no marcador, abordando hookman/rival e conectando com desafio já ativo; nada deve iniciar localmente.
- Jogo: host larga e ambos entram; repetir após resultado; parceiro abre/sai da garagem; host começa enquanto parceiro está na garagem e pedido permanece pendente.

## Implementação em 19/10

`race.cpp` agora implementa `StreetGuard`: empréstimo restrito de `cruise+0D`, liberado antes de uma autorização pendente e ao desconectar. A posse inclui cruise, setup, carreira/dados, cidade, máquina e veículo. Garagem/frontend com o mesmo mundo preservam a posse suspensa sem escrever nele; isso permite restaurar o byte quando a mesma rua retorna. Uma identidade diferente descarta a posse sem restaurar em memória de outra geração.

`PartnerTick` recusa armamento/teleporte fora do modo2 e publica o aviso no F10 e HUD. As fases de marcador possuem serial e identidade próprias; pedido novo, mudança de mundo e nova sessão cancelam a fase anterior na thread do jogo. Timeout não publica largada bem-sucedida. Os dados necessários são validados antes de emprestar campos do registro.

`HostStreetTests()` e `HostMarkerPhaseTests()` passaram contra o runtime real em RAM sintética. Cobrem ciclo de posse, retornos de garagem, substituição de serial, restauração do registro, ausência de teleporte indevido e reconhecimento de pedido4 verdadeiro. A eficácia completa dos consumidores nativos e a experiência da garagem continuam sem validação ao vivo nesta etapa.
