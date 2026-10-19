# Troca de carro remoto: recursos e limites do carregador

Análise estática de 18/10/2026. Nenhum jogo, runner, lançador ou operação de firewall foi executado. VAs são do Xbox; host = base + VA. CONFIRMADO significa evidência estática; não significa teste ao vivo. Esta subtarefa só editou este documento.

## Resultado para implementação

**CONFIRMADO:** a personalização do mesmo modelo tem consumidor normal de `CarCfg+8`. **Não foi encontrado, nos caminhos examinados, pedido persistente de dados que reconstrua somente o modelo remoto.** O carregador de categoria 3 não resolve essa lacuna: também destrói/recria jogadores, adversários e corrida. O seu byte `+4` indica recursos carregados, não um pedido de troca.

**PALPITE descartado como receita:** mudar TypeName, marcar `0x80` ou forçar `loader+4=0` não demonstra reconstrução segura. Tampouco há evidência de que isso preserve ronda/corrida. Manter modelo novo pendente até um caminho de reconstrução completo ser demonstrado.

## Personalização e TypeName

**CONFIRMADO:** na atualização `0xFEF80`, o jogo obtém `vehicle+0x64` e chama `0xF96C0`, que encaminha ao aplicador `0xF32A0`:

```asm
000FF021 mov ecx, [ecx+0x64]
000FF024 push ecx
000FF025 mov ecx, esi
000FF027 call 0x000F96C0
000F96D7 mov ecx, [ecx+0x2F0]
000F96DE push 0x00              ; forceAll=false
000F96E0 push eax               ; CarCfg
000F96E1 call 0x000F32A0
000F32B9 mov eax, [edi+0x08]
000F32BC and eax, 0x3F7F
000F430F and eax, 0xFFFFC080
000F4314 mov [edi+0x08], eax
```

Aplicar a configuração uma vez, preservando vtable e marcando os bits aceitos, permite ao jogo consumir personalizações. Deve haver guarda de modelo e identidade do objeto. Copiar o pacote por frame pode repor máscara limpa antes do consumo ou repetir trabalho indefinidamente.

**CONFIRMADO:** `0x272F70` copia o nome da tabela `[0x557EB0]` para `cfg+0xE3`, com tamanho 0x16; não marca bit de alteração. `0x270AE0` resolve esse nome por `0x26A6A0`.

**CONFIRMADO:** não chamar `0x80` de “pedido de modelo”. O setter identificado desse bit modifica outros dois bytes:

```asm
00271E00 mov al, [esp+0x04]
00271E04 mov [ecx+0xE1], al
00271E0A mov [ecx+0xE0], al
00271E10 or dword ptr [ecx+0x08], 0x80
```

O fato de `0xF32A0` excluir `0x80` não prova vínculo desse bit com TypeName. A semântica completa dos bytes E0/E1 não foi determinada aqui.

## Comparação de setups e recarga

**CONFIRMADO:** `0x20260` lê setups atual `[0x53F2E0]` e próximo `[0x53F2E4]`; apenas quando a transição pede próximo usa ambos. Ela compara diversas propriedades globais, contagens humanas e, finalmente, modelo de cada humano:

```asm
00020512 mov eax, [edi+0x4C]
00020515 cmp [esi+0x4C], eax
...
0002053D add ebp, 0x120          ; cfg da primeira vaga
00020547 mov ecx, [eax+ebp*1]
0002054A mov esi, [ebp]
0002054D call 0x00270AE0
00020552 mov ecx, esi
00020556 call 0x00270AE0
0002055B cmp eax, edi
0002055D jnz 0x000205BD
000205BD mov bl, 0x01
```

Com diferença BL, descarrega a categoria 3 e depois a carrega novamente:

```asm
00020613 push 0x03
00020617 call 0x0001FF30
00020620 call [edx+0x04]        ; loaded?
...
00020627 push 0x03
0002062B call 0x0001FFB0
00020630 push 0x03
00020634 call 0x0001FF30
0002063D call [edx+0x0C]        ; unload
...
00020834 push 0x03
00020838 call 0x0001FF30
00020841 call [edx+0x04]
...
00020855 call 0x0001FF30
0002085E call [edx+0x08]        ; load
```

Há ainda descarregamento em cascata das categorias posteriores por `0x20130`. Os chamadores diretos identificados de `0x20260` são `0x21170` (argumento 1) e `0x212E0` (argumento 0), ambos caminhos de transição. Isso não é um comparador executado por frame de condução nem pedido isolado por vaga.

## A categoria 3 é o conjunto de gameplay

**CONFIRMADO em código e tabela estática do XBE:**

| Item | Endereço / valor |
|---|---|
| Manager global | `[0x4CB6E8]`, retornado por `0x1FF20` |
| Resolver categoria | `0x1FF30`: `[manager+4+index*4]` |
| Construção manager | `0x20050` |
| Categoria 3 | `[manager+0x10] = manager+0x98` |
| Construtor categoria 3 | `0x291D0` |
| Vtable categoria 3 | `0x4496B4` |
| `+4` / loaded | `0x226E0`: `mov al,[ecx+4]` |
| `+8` / load | `0x29240`, continua em `0x29337` |
| `+C` / unload | `0x29690`, continua em `0x297B5` |

O unload não se limita ao recurso visual de um carro:

```asm
000296E5 mov ecx, [0x0050152C]
000296EB mov ecx, [ecx+0x08]
000296F2 call 0x0006C7F0        ; tráfego
00029712 call 0x001C11C0        ; corrida
00029726 mov ecx, [0x00534074]
0002972C call 0x001E45F0        ; OppMgr
00029736 call 0x002BD7F0        ; player manager
```

E o load também inicializa os conjuntos novamente:

```asm
000294E5 call 0x001E4390
000294EA mov ecx, [0x00534074]
000294F1 call 0x001E43C0        ; Init OppMgr
000294F6 call 0x002BE2C0        ; construção humanos
...
00029513 mov ecx, [0x00558814]
00029519 call 0x002BD8A0
00029523 mov ecx, [0x00558838]
00029529 call 0x002C8710
0002952E mov ecx, [0x00533E24]
00029534 call 0x001C13D0
...
0002967F mov byte ptr [ecx+0x04], 0x01
```

**CONFIRMADO:** o byte loaded começa 0 em `0x28D90`; é consultado por `0x226E0`; torna-se 1 ao concluir `0x29240`; limpeza base `0x28DD0` o torna 0. Nos caminhos examinados não há consumidor por frame que trate escrevê-lo como pedido de reconstrução. Forçar false sem liberar recursos quebra a descrição de estado do carregador.

## Recursos do modelo construído

**CONFIRMADO:** `0xF4430`, construtor de customização de 0x3138 bytes, copia o nome do recurso para `custom+0x102C`:

```asm
000F44E0 mov ecx, [esp+0x1CC]
000F44E7 push 0x40
000F44E9 lea eax, [ebp+0x102C]
000F44EF push ecx
000F44F0 push eax
000F4548 call 0x002DB2B0
```

O nome é usado para abrir recursos durante o construtor (`0xF4977`, `0xF4D34`, `0xF4D8A`). A busca no índice por referências literais a `+0x102C` só achou esse construtor e seu fragmento `0xF4CCD`; não achou comparação contínua com TypeName do CarCfg.

O desenho tem referências reversas e alocações específicas de modelo em `0xFBA90`, não só um ponteiro de mesh:

```asm
000FBAC6 mov [esi+0x18], ebp     ; recursos do modelo
000FBADC call 0x001B3B90
000FBB00 call 0x00352A70        ; dados da instância do modelo
...
000FBC33 mov [edi+0x1800], esi   ; custom -> desenho
000FBC42 mov [esi+0x2F0], edi    ; desenho -> custom
```

**Consequência:** trocar só o nome no objeto já construído ou trocar ponteiros entre objetos não reproduz as etapas de liberação/alocação/registro. Não há receita segura demonstrada nesse caminho.

## Escopo e próximo alvo

A conclusão é limitada aos caminhos de personalização e carregador acima. Não é prova de inexistência de qualquer mecanismo no jogo. Para implementar troca de TypeName sem recarregar a sessão ainda é necessário identificar um pedido de dados consumido pelo jogo que faça a reconstrução completa, ou demonstrar outro caminho nativo com sua vida útil e registros. A investigação de factory/player manager foi feita em subtarefa separada.

Consultas: `tools/xref.py asm/callers/callees`, buscas delimitadas nos recompilados e leitura apenas da tabela de vtable no `assets/default.xbe`. Nenhum binário foi executado, nenhuma memória de processo foi acessada, nenhum pointer swap foi implementado.
