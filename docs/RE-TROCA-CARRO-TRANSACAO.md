# Ponte nativa: transação por vaga para trocar o veículo

Análise estática de2026-10-19 em `C:\dev\mc3-coop`. O usuário autorizou investigar a ponte, sem teste ao vivo. Não foram chamados guest, jogo, runner, processos ou firewall. Este documento é o único arquivo editado nesta subtarefa. Fontes: asm de `tools/xref.py`, fragmentos delimitados dos recompilados e vtables do XBE como dados. A prova de ABI/ponte da build atual pertence a uma investigação separada; endereços guest abaixo não são funções host chamáveis por `base+VA`.

**Conclusão:** há blocos nativos suficientes para descrever construção, registro e aposentadoria de um produto completo sem destruir o jogador. **Ainda não há uma transação de hot swap validada.** A factory registra o candidato antes de retornar; seus arrays não verificam capacidade. É necessário provar folga, isolamento dos callbacks e rebind de estados do jogador antes de executar. O primeiro escopo investigável é humano remoto sem HUD/câmera, mundo estável, mesma classe de habilidade e mesmo contexto de setup. A troca em corrida continua dependendo de comprovação de que nenhum consumidor guarda referências diretas ao veículo antigo.

## 1. Grafo de propriedade e referências

| Referência | Evidência / função |
|---|---|
|`[558814]+8+4*slot → player`|manager humano; preservar objeto e índice|
|`[558838]+4*k → player`|lista de corredores, montada em2C8710; humanos são os mesmos objetos do manager|
|`player+10 → carData`|2C0061/2C0068 copiam factory+4C; não é o wrapper|
|`player+14 → wrapper`|2C005E publica produto de2CBC40|
|`wrapper+04 → input`|2CB320 constrói e2C89F0 associa ao produto|
|`wrapper+08 → car`|2CB3C9; car+04 aponta de volta ao wrapper em2CB3CE|
|`wrapper+0C → draw`|2CB64D; draw+04 aponta ao wrapper em2CB658|
|`wrapper+10 → audio`|2CB964; audio+04 aponta ao wrapper em10BA50; audio+24 recebe slot em2CB961|
|`wrapper+1C → phInst`|2CBE56; inst+40 aponta ao wrapper em2CBE53|
|`wrapper+4C → carData`|2CBE16; deve coincidir com o novo player+10|
|`wrapper+64 → CarCfg`|2CBE30; cfg deve continuar vivo depois que a factory/scratch terminar|
|`wrapper+68 → custom`|2CBE76; custom é transferido da factory, que zera+50 em2CBE7C|
|`draw+2F0 ↔ custom+1800`|FBC42/FBC33; vínculos específicos do modelo|
|`input+30 → [carData+18]`|ponteiro lido em2CB379/2C89FF e guardado por35F2C0|
|`input+34 → car`|2C89FC/2C8A03 e35F2C0|
|`input+50 → wrapper`|2C8A0F; entrada não pode continuar ligada ao car antigo|
|`player+54 → camera`, `player+58 → HUD`|objetos do jogador, não construídos pela factory de wrapper|
|`player+C8 → estado de habilidade`|2BF920 escolhe perfil/tuning conforme modelo e setup|

**CONFIRMADO:** manter o endereço de player preserva as referências do manager humano e da lista de corredores. Não é necessário reconstruir essas listas só para publicar outro wrapper. Isso não prova que todos os consumidores da corrida/HUD tenham apenas referências indiretas; esse inventário ainda precisa ser fechado.

Os progressos humanos residem em `race+98+slot*B8`, separados do wrapper, com volta/checkpoint/ranking/chegada. A transação proposta não chama inicialização de corrida nem modifica esses blocos. A preservação efetiva durante uma substituição em corrida ainda exige validar caches e efeitos de callbacks.

## 2. Construção do produto e duração dos argumentos

O trecho nativo do ctor humano `2C0021..2C005E`, recomposto inclusive dos pequenos fragmentos intermediários, chama:

```text
2CB270(factory, TypeName, 0, slot, CarCfg*, optional184Flag, inputIndex)
2CBC40(factory) -> wrapper
player+14 = wrapper
player+10 = factory+4C
2CB2C0(factory)
```

Convenção descrita em termos guest: `this=ECX`, seis argumentos na pilha; `2CB270` termina em `ret18`. A estrutura alcança+EC, portanto reservar pelo menosF0 bytes. Não reutilizar offsets relativos à pilha do ctor como se fossem um objeto persistente.

| Campo da factory | Valor após2CB270 |
|---|---|
|`+04`|cópia inline de TypeName feita por376600|
|`+44`|produto, retornado também por361B80|
|`+4C`|carData adquirido no manager de modelos|
|`+50`|custom durante build; transferido e zerado ao instalar no wrapper|
|`+54`|ponteiro CarCfg, preservado no wrapper+64|
|`+E4`|segundo argumento, zero no ctor humano examinado|
|`+E8`|slot|
|`+E1`|optional184Flag, byte|
|`+EC`|inputIndex|

**Correção de nomenclatura:** o quinto argumento não está provado como “soundFlag”. `2CB390` o lê em2CB46F para decidir construir o componente `car+184` por110F90/111040. O ctor humano o tornafalse em tipos de setup15h/12h. O áudio **não** usa esse byte para decidir se existe; ver seção5.

`2CBC40` cria custom3138 bytes porF4430, adquire carData e chama os métodos da factory nesta ordem:

```text
+20 2CB2D0   adquirir carData por105B80
+04 2CBDE0   wrapper6C + phInst44 + custom/cfg
+10 2CB5C0   draw49C e vínculos com custom/carData
             reaplica custom ao produto
+0C 2CB390   car1AC e seus componentes
+08 2CB320   input (pode ser ausente para índiceFF no offline)
+14 2CB680   componente wrapper+14
+24 2CB6C0   wrapper+24
+28 2CBE90   wrapper+28
+2C 2CBF70   componente opcional wrapper+20
+1C 2CB710   mapper/estado em wrapper+18, condicionado ao slot ter pad
+30 2CB820   coleção wrapper+38/+3C
+34 2CB9D0   wrapper+40
+38 2CBA20   wrapper+2C
+3C 2CBA60   wrapper+44
+18 2CB8B0   áudio em wrapper+10
             ligação extra para determinados tipos físicos
             REGISTRA produto no manager global
             produto.vt+04 = FFC50 reset
```

Alguns construtores verificam alocação e depois dereferenciam o resultado sem uma saída de erro completa. Um retorno falso da bridge não garante rollback seguro de construção parcial; não tratar exceção/acesso inválido como cancelamento recuperável.

**CONFIRMADO:** a destruição da factory não libera o produto. `2CB2C0→361B70→376630` só troca as vtables e retorna. Em uma futura ponte cujo helper preserve/restaure EAX, os outputs relevantes podem ser lidos em factory+44/+4C ainda dentro do scratch guest válido; não depender do EAX host depois da chamada.

## 3. Registro global: candidato não nasce isolado

`3308C0` retorna `[5638E4]`, manager de veículos com vtable454F04:

| Slot | Método |
|---|---|
|+4|FEEA0, registrar wrapper e componentes|
|+8|FEF10, remover wrapper e componentes|
|+10|FEF80, update|

`2CBC40` executa o registro automaticamente em `2CBDA1..2CBDBD`, **antes** de devolver o produto. FEEA0 chama35D500 para os buckets da base e acrescenta ainda wrapper+14/+0C/+10 aos arrays próprios. Esses buckets já serão consumidos pelo update normal.

```asm
0035D500 mov eax,[manager+04]   ; count wrappers
0035D503 mov edx,[manager+08]   ; array
0035D50B mov [edx+eax*4],new
0035D50F inc [manager+04]       ; sem comparar capacidade
; registra car, componentes, rodas, input, collider etc em outros buckets
000FEED1 mov ecx,[manager+90]   ; draw count
000FEEDD mov [drawArray+ecx*4],draw
000FEEE0 inc [manager+90]       ; também sem guarda de capacidade
```

`35D200` aloca os arrays a partir de manager+7C; alguns buckets usam múltiplos desse limite. Nenhuma vaga de transação extra é reservada automaticamente. Para construir antes de destruir, validar capacidade **de todos os buckets afetados**, incluindo número de rodas/componentes do novo modelo, e não apenas count wrappers. Falta fechar a fórmula de capacidade de cada bucket para um preflight executável.

**CONFIRMADO:** a remoção é por ponteiros concretos, não por número de slot. FEF10 remove seus arrays próprios e chama35D700 para os buckets base.35D680 busca a igualdade do ponteiro e desloca as entradas seguintes para fechar o buraco; preserva a ordem relativa. Se não encontra, incrementa um contador de diagnóstico e retornaFF. Não chamar Remove duas vezes nem editar só um array.

Consequência: a estratégia inicial mais simples só pode usar o builder completo com folga comprovada, sem retornar ao jogo entre o registro do candidato e a conclusão/rollback. Uma versão com registro adiado precisaria orquestrar as etapas nativas anteriores ao tail de2CBC40, com prova própria; não basta pular instruções arbitrariamente. Desregistrar o antigo antes de criar poupa capacidade, mas amplia a transação e exige tratar o fato de que o destrutor depois tentaria desregistrá-lo de novo. Não é fallback aprovado por este mapa.

## 4. carData: pool de16 entradas e release separado

**CONFIRMADO:** o manager `[50A08C]` mantém16 entradas de10 bytes em105B80. Ele procura um modelo/modo compatível ou um slot marcadoFFFFFFFF; se todos estão ocupados, `105BCE..105BD3` devolvemzero. Os chamadores da factory não tratam essa falha como uma transação limpa.

```asm
00105BBA jl 00105BA0           ; busca até16 entradas
00105BC0 cmp dword [entry],FFFFFFFF
00105BC9 cmp index,10h
00105BCC jl 00105BC0
00105BD0 xor eax,eax           ; pool cheio
; reutilização:
00105C34 inc cl
00105C37 mov [entry+09],cl      ; refcount byte
00105C3A mov eax,[entry+04]     ; carData
```

`1057B0(manager, carData)` é o release correto: procura carData em entry+4, decrementa entry+9 e só quando zera libera recurso/modelo e torna a entrada livre. `103C00/103930` fazem a destruição efetiva das partes do carData. Não chamar esses destrutores diretamente para aposentar uma referência individual compartilhada.

O custom também contém referências compartilhadas: `F25E0` decrementa os WORD refcounts em custom+1804/+1808/+180C e libera os recursos quando zeram. Clonar bytes ou liberar esses blocos por um alocador host quebraria ownership.

A construção bem sucedida precisa adquirir as referências do candidato antes de o release antigo ocorrer. Os recursos/carData do modelo novo devem existir, TypeName deve resolver por26A6A0 e deve haver margem de memória. O caso de recurso ausente, pool cheio ou alocação parcial permanece bloqueio técnico.

## 5. Áudio, input e cleanup por callbacks

**CONFIRMADO:** `2CB8B0` sempre aloca o objeto de áudio84 bytes e chama10AC20, depois10BA30. Não lê factory+E1. A distinção de áudio remoto usa `2694A0`/slot local; outra flag depende do número de humanos (offline>=2 ou native-network>=4). O objeto recebe `audio+04=wrapper` em10BA50 e `audio+24=slot` em2CB961.

Portanto optional184Flag=false não elimina o áudio do candidato. O construtor10BA30 começa zerando audio+24 e guarda seu owner em+4; a factory grava o slot real em+24 **depois** da inicialização. Nos trechos completos de10AC20/10AF30/10B7C0 examinados não há consulta desse campo de slot. O construtor entrega aos subobjetos handles/resources e o destrutor `10C980→10B7C0` os encerra por seus próprios handles, chamando61020 no manager de áudio. Não foi encontrado nesses caminhos um registro global por slot cuja limpeza antiga apagaria automaticamente o novo objeto. Isso favorece candidate-first, mas os callbacks dos subobjetos não foram todos auditados: **coexistência temporária não está provada segura nem foi demonstrada como conflito inevitável.**

O input é associado por `2C89F0`/35F2C0 ao car e carData novos, além do wrapper eminput+50. O destrutor humano `2CA620` libera o mapper deinput+64 por2CA810 e sua memória; a base35F280→3313C0 finaliza o descritor. Não reaproveitar input antigo sem rebind, nem forçar um pad físico ao remoto. Um protótipo deve reproduzir o inputIndex e as guardas do slot remoto existente.

O car destructor `121DB0→120A20→A22F0` remove componentes e, quando o índice de car+68+8 é válido, pede remoção ao sistema físico `[5580F0].vt+14`. O phInst destructor `1FAEB0→35E090→2A4A80` libera sua associação de física por2A4B70. São mais ownerships que um simples registro no manager de veículos; não omitir essas destruições nativas nem reutilizar seus índices manualmente.

## 6. Aposentar o antigo sem destruir o jogador

**CONFIRMADO: NÃO chamar2C8480 como cleanup de veículo.** Essa função é o destrutor da base do próprio jogador: muda sua vtable para476738, remove o actor do manager `[5587D8]` por2B7330 e termina em2B7540. Também não chamar o destrutor humano2C1460/2C1BB0, que libera câmera/HUD/efeitos/estados do jogador.

O trecho de2C8480 que demonstra as três responsabilidades separadas é:

```asm
002C849A mov ecx,[player+14]     ; guardar oldWrapper antes do commit
002C84A5 mov edi,[ecx+68]       ; guardar oldCustom ANTES do wrapper dtor
002C84A8 push 1
002C84AA call [wrapper.vt+0]     ; 100D70 -> FFBA0
002C84AC mov ecx,[player+10]     ; usar oldCarData salvo, nunca o novo campo após commit
002C84B6 call 001057B0
002C84C3 mov ecx,oldCustom
002C84C5 call [custom.vt+0]      ; F3280 -> F25E0
```

Uma futura transação deve executar somente essas responsabilidades de folha usando os **ponteiros antigos salvos**, sem chamar o destrutor do player. A ordem nativa é wrapper, release carData, custom. O wrapper não dá release de carData nem destrói o custom; fazer só `wrapper.vt0(1)` vaza esses recursos.

`FFBA0` destrói partes auxiliares, remove o wrapper do manager porFEF10, e passa a35DD70/376570 para destruir os componentes restantes, inclusive car/draw/audio/input/phInst. `vehicle+60` não zero faz o destrutor pular a remoção, mas não foi identificado um produtor nativo que valide usar esse byte como API geral de detach. Não escrever esse flag para encobrir uma remoção duplicada sem uma nova prova da sequência.

## 7. Estado do jogador que o wrapper novo não reconfigura

`2BF920(player+C8, player)` não é um setter idempotente. Ele aloca um timer em+C8+5C, escolhe perfil em+C8+30 por `269BA0/269BE0/269C20`, carrega o descritor de tuning embutido em+C8+E4 e considera setup type15h/12h, setup+1C e quantidade de humanos. O perfil pode mudar **entre dois carros**, não só carro↔moto. Em offline com dois humanos, o ramo single-player não aplica, mas a seleção de perfil por modelo continua existindo.

`2BF840` é reset e não escolhe perfil novamente; o update continua acessando o veículo conforme o perfil antigo. Não chamar novamente2BF920 sobre o estado vivo, nem assumir que permanecer em “carros” basta. Limitar o primeiro protótipo à **mesma classe/perfil de habilidade confirmado para os dois modelos**, mantendo os demais condicionantes iguais, ou antes encontrar teardown/reconfigure idempotente desse estado.

Câmera/HUD, caso presentes, também ficam fora do wrapper. O primeiro escopo sem esses objetos reduz o inventário, sem provar o caso geral. Preservar o player, seus links de actor, racer-info, resultados, configuração de vaga, índices de HUD/pad e carreira; modificar apenas referências cujo rebind foi demonstrado.

## 8. Plano de transação e condições de parada

O plano abaixo é **proposta verificável**, não implementação nem receita já segura:

1. **Preflight sem mutação:** validar geração do mundo/slot remoto/player/race/setup, modelo e perfil de habilidade, ausência de transição/garagem/resultado, contexto correto da thread guest, recursos e folga em todos os pools/buckets. Snapshot de player+10/+14, pose/velocidades, progresso e pointers que devem permanecer constantes. Não copiar estruturas físicas de outro modelo.
2. **Preparar argumentos guest persistentes:** CarCfg independente com duração até o fim do wrapper; factoryF0 e TypeName válidos. Não fornecer ponteiros host no campo guest nem armazenar cfg na pilha temporária que será liberada depois da chamada.
3. **Construir candidato com o antigo ainda válido:** reproduzir os argumentos nativos e chamar ctor/build apenas quando a bridge atual estiver provada. A factory registra automaticamente o candidato; não ceder um frame nesse intervalo. Inspecionar factory+44/+4C e o grafo completo de vínculos/registro antes de publicar.
4. **Validar e preparar física do novo:** confirmar wrapper/car/draw/inst/custom/cfg e referencias inversas; colocar pose e velocidades por campos/rotinas compatíveis com a física nova após o reset nativo. Massa/inércia/geometria são do novo modelo. Provar que áudio e callbacks não alteraram o player/veículo antigo por slot.
5. **Commit curto:** reler todas as identidades; publicar novo carData emplayer+10 e wrapper emplayer+14, com cfg/setup coerentes e caches do mod invalidados. Preservar manager/lista de corredores/player, race e seu progresso; não chamar BeginRace/Init/2C8710/1C13D0 nem reset geral do jogador. Garantir que nenhum observador acesse um par incompleto.
6. **Retire:** somente após rebind/callbacks comprovados, executar as três responsabilidades de folha da seção6 sobre os ponteiros antigos salvos. Confirmar que os arrays perderam apenas os componentes antigos, que os novos continuam registrados e que refcounts/pools voltaram ao equilíbrio esperado.
7. **Rollback do candidato completo:** antes do commit, o antigo continua publicado; limpar somente o candidato pelo mesmo ownership completo e manter a serial/configuração anterior. Isso só é formulável se a construção terminou íntegra. Falha no meio do builder, callbacks assíncronos, recurso ausente e crash guest não têm rollback provado.

**Pendente antes de executar:** capacidades exatas de cada bucket e broadphase, interleaving/render/áudio da construção, colisão temporária do candidato, callbacks por slot, recursos inválidos/OOM, todos os caches externos ao wrapper e publicação atômica sob o runner atual. Uma pausa só na thread do jogo não prova automaticamente ausência de leitores de render/áudio.

| Dependência | Estado e próximo critério |
|---|---|
|Ownership wrapper/carData/custom|CONFIRMADO por ctor/dtor; usar ponteiros salvos e release separado|
|Player/racer preservados|CONFIRMADO nas listas mapeadas; caches externos ainda PENDENTES|
|Registro/detach de componentes|CONFIRMADO porFEEA0/FEF10/35D500/35D700; falta preflight completo de capacidade|
|Factory/argumentos/outputs|CONFIRMADOS em asm; execução pela ponte atual PENDENTE|
|Perfil de habilidade|Ctor/reset não idempotentes CONFIRMADOS; whitelist de perfis compatíveis PENDENTE|
|Áudio de mesmo slot|Handles por instância CONFIRMADOS; callbacks internos/coexistência ainda PENDENTES|
|Physics/broadphase|Cleanup nativo identificado; capacidade/ativação do candidato sem colisão PENDENTES|
|Construção parcial/exceção|Sem rollback provado; BLOQUEIA habilitar executor do hot swap|
|Recursos/heap/refcounts|Pool16/refcounts identificados; preflight de recursos e memória PENDENTE|
|Render/áudio/atomicidade|PENDENTE provar ausência de observadores durante commit/retire|

O caminho está suficientemente delimitado para decidir a próxima investigação da bridge, mas não para prometer troca funcionando nem executar um construtor diretamente num mundo do usuário. Nenhum executor/hot swap está habilitado por esta auditoria; lifecycle, exceção, capacidade e perfil continuam condições de parada. Um teste futuro precisa primeiro provar ABI e uma chamada inofensiva, depois um candidato controlado com todos esses invariantes, em ambiente isolado explicitamente autorizado.
