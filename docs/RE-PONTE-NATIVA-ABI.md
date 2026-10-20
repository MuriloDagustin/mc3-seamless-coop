# Ponte nativa: ABI do runner atual, validação estática

19/10/2026. Arquivo examinado **somente por leitura de bytes**: `D:\dev\mc3-coop-data\xrun\MC3Runner.exe`. Usei Python com `pefile` e `capstone`; nenhuma função do PE foi executada e o arquivo não foi carregado com LoadLibrary/LoadLibraryEx. Não abri jogo, processos do runner, rede ou firewall e não alterei fontes do mod. O mapa antigo `RE-CORRIDA.txt`, seção native-abi, foi apenas referência; seus endereços não servem para esta versão.

**Resultado:** tabela guest→native, globals de registradores, troca de contexto e dois helpers de chamada foram localizados no arquivo atual. Existe um wrapper que preserva o estado guest no retorno normal. Isso **não valida uma operação de troca de veículo**, nem prova que factory/IO/yield seja seguro dentro do proxy. Permanecem sem teste: identidade da thread/fiber em execução, execução aninhada, não retorno/exception/longjmp, margem real de stack e transação de lifecycle do veículo. O inspector deve manter `abi_validated=false` para autorização de execução.

`CONFIRMADO` abaixo significa evidência estática no arquivo identificado, não validação ao vivo. `NÃO PROVADO` indica a parte que ainda impede tratar esta análise como executor pronto.

## 1. Identidade do arquivo e endereçamento

| Campo | Valor |
|---|---|
| Tamanho do arquivo | 63.648.088 bytes |
| SHA-256 | `51354a042caf24ffc73a989a2beb93a867401b6f2f4b61c0a5d0d2565696be8d` |
| PE TimeDateStamp | `6AD54432` |
| ImageBase | `140000000` |
| SizeOfImage | `043AE000` |
| DllCharacteristics | `8120`, sem DYNAMIC_BASE |
| Relocation directory | tamanho0 |

| Seção | RVA | Raw offset | Raw size | Virtual size | Characteristics |
|---|---|---|---|---|---|
| .text | 00001000 | 00000400 | 02412A00 | 02412841 | 60000020 |
| .rdata | 02414000 | 02412E00 | 00323000 | 00322E1C | 40000040 |
| .data | 02737000 | 02735E00 | 00121800 | 0081E8D0 | C0000040 |
| .pdata | 02F56000 | 02857600 | 00094E00 | 00094D10 | 40000040 |

VAs host nas tabelas abaixo são ImageBase+RVA. Raw offset precisa ser calculado pela seção; não aplicar um delta global. Grande parte dos globals está na cauda virtual de .data, sem bytes correspondentes no arquivo. Seus valores atuais **não** foram observados; somente os acessos e endereços foram demonstrados pelo código.

**CONFIRMADO:** não há diretório de exports no PE. Os helpers encontrados são internos; não são uma API exportada comprovada. Não usar os VAs com outro SHA/build.

## 2. Tabela guest→native e resolver

**CONFIRMADO:** formato24 bytes, tabela ordenada por start:

```text
+00 u32 guestStart
+04 u32 guestEnd
+08 u8  midEntryOK
+09..0F padding
+10 u64 nativeHostVA
```

| Item | VA / RVA / raw |
|---|---|
| Count u64,20302 (`4F4E`) | VA142416818 / RVA02416818 / raw02415618 |
| Primeira entrada | VA142416820 / RVA02416820 / raw02415620 |
| Fim exclusivo da tabela / início hash u16 | VA14248D770 / RVA0248D770 |
| Exact lookup | VA142148610 / RVA02148610 |
| Mid-entry lookup | VA142148680 / RVA02148680 |
| Resolver indireto | VA1403B1E30 / RVA003B1E30 |
| HLE/host-thunk resolver | VA140002B00 / RVA00002B00 |

Seeds revalidados:

| Guest start | Guest end | mid | Native host VA | Entrada VA |
|---|---|---|---|---|
| 002BD910 | 002BD91B | 0 | 1416B5EC0 | 142460C98 |
| 001B3B90 | 001B3BFE | 0 | 142147C10 | 142441768 |
| 002CBC40 | 002CBDDB | 0 | 142148370 | 1424620F0 |
| 00084C00 | 00084C89 | 0 | 14076FBA0 | 1424241D0 |
| 001E2D40 | 001E2D41 | 0 | 14110EC30 | 1424474E0 |

**CONFIRMADO:**142148610 calcula `guest*9E3779B1 >>16`, lê índice u16 em14248D770, compara o start e avança com colisão no hash. Se coincide, devolve o qword+10 da entrada24.142148680 faz busca binária para um interior e consulta byte+8; quando ele é0 chama14240C850, o caminho fatal correspondente. Não aceitar um endereço interno só porque cai no intervalo. Existem sobreposições de ranges; o start exato continua sendo o critério seguro de resolução.

1403B1E30(guestVA,midOut) zera midOut e consulta, nesta ordem:140002B00,142148610,142148680. Se só o último resolve, guarda o VA guest emmidOut. A construção dessa resolução não garante que o alvo seja seguro para invocar naquele contexto. Alguns targets são thunks de despacho por CPU; não saltar diretamente para uma variante interna.

Prefixo GetPlayer1416B5EC0, útil como evidência adicional de build:

```text
44 8B 0D 71 1C 23 01 4C 8B 05 EA 08 5B 01 8B 15 54 1C 23 01
```

Ele lê ESP1428E7B38, guest base142C667B8 e ECX1428E7B28; escreve EAX1428E7B20 e avança ESP em8. O entrypoint native é uma função Win64; os argumentos e registradores **guest** não são os argumentos Win64 dessa função.

## 3. Arquivo global de registradores e scheduler

**CONFIRMADO:** globals simples, não um arquivo de registradores TLS por thread. O scheduler1403C5320 salva os campos no registro atual, instala outro registro, atualiza o ponteiro global e só então chama SwitchToFiber. Prefixo:

```text
56 48 83 EC 20 48 8B 05 34 8E 8A 02 48 39 C1 0F 84 AB 03 00 00
```

| Estado | Host VA | Tamanho | Offset no guest-thread record |
|---|---|---|---|
| eax | 1428E7B20 | 4 | 00 |
| ecx | 1428E7B28 | 4 | 04 |
| edx | 1428E7B30 | 4 | 08 |
| esp | 1428E7B38 | 4 | 0C |
| ebx | 1428E7B24 | 4 | 10 |
| esi | 1428E7B34 | 4 | 14 |
| edi | 1428E7B2C | 4 | 18 |
| ebp | 1428E7C70 | 4 | 1C |
| FS/KPCR base | 1428E7B84 | 4 | 20 |
| outro estado de segmento | 1428E7B88 | 4 | 24 |
| lazy/flags A | 1428E7B14 | 4 | 28 |
| lazy/flags B | 1428E7B18 | 4 | 2C |
| estado de flags C | 1427F55D0 | 4 | 30 |
| lazy/flags D | 1428E7B1C | 4 | 34 |
| estado de flags E | 1427F55D4 | 4 | 38 |
| resume PC | 1428E7C60 | 4 | 3C |
| estado adicional | 1428E7C68 | 8 | 40 |
| x87 bank,8 doubles | 1428E7B40..1428E7B7F | 64 | 48..87 |
| x87 top | 1428E7B80 | 4 | 88 |
| FCW emulado | 1427F55DC | 2 | 8C |
| xmm0..7 | 1428E7C78..1428E7CF7 | 128 | 90..10F |
| outro banco, provável MM | 1428E7C20..1428E7C5F | 64 | 110..14F |
| MXCSR emulado | 1427F55D8 | 4 | 150 |

Os nomes dos estados adicionais/lazy/MM são **NÃO PROVADOS** nesta análise; os endereços, tamanhos e cópias são confirmados. Preservar o conjunto completo evita depender desses rótulos.

Outros objetos confirmados:

```text
guest memory base QWORD = [142C667B8]
current guest-thread record QWORD = [142C6E160]
thread pool base =142C67B60,64 registros, stride198
record+158: fiber host principal
record+160: fiber alternativo quando presente
record+168: estado do agendador,1 durante execução
record+190: slot alocado/ocupado
```

SwitchToFiber wrapper140405A40 faz `mov rcx,[rcx]; jmp [IAT142414240]`. A troca normal ocorre em1403C5696. O runtime também tem criação140405990 e entrada1403C50E0. Isso confirma uma organização cooperativa com arquivo global de registradores; não autoriza manipular os globals de uma thread de rede enquanto o guest roda.

## 4. CallGuest baixo nível14039EFA0

**CONFIRMADO, contrato Win64:** `rcx=guestVA`, `rdx=const uint32_t* args` **host**, `r8d=count`. O array host contém argumentos em ordem normal, e o helper os empilha em ordem inversa. Não é um objeto request. Retorna EAX host1 após retorno normal do alvo,0 em rejeições/ausência de resolução. O retorno guest fica nos globals, mas o helper não o devolve como seu resultado host.

Prefixo32 bytes:

```text
41 56 56 57 55 53 48 83 EC 30 48 89 D6 45 85 C0 0F 95 C2
48 85 F6 41 0F 94 C1 31 C0 41 84 D1 0F
```

Sequência demonstrada:

1. Rejeita argsNULL se count!=0. Calcula `bytes=4*count+4`, subtrai do ESP global e rejeita borrow/underflow.
2. Chama1403C7EB0(lowESP,bytes,1), um verificador de intervalo/memória guest. Isso não testa o limite lógico da stack da thread nem a profundidade futura do callee.
3. Resolve o guest por1403B1E30, incluindo o mecanismo de mid-entry. Há ainda ramos especiais para HLE/kernel. Uma bridge específica deve restringir starts/alvos permitidos em vez de expor essas generalizações.
4. Em14039F050..14039F072 diminui ESP em4 por argumento e escreve o último argumento primeiro através de1403B7240 (tradução guest→host).
5. Em14039F07A..14039F088 empilha DWORD0 como endereço de retorno.
6. Se midOut!=0, instala resume PC1428E7C60; chama o target em14039F09C/F0AA; limpa resumePC do caminho mid.
7. Restaura o **ESP inicial** em14039F0AC e retorna host1 em14039F0B2. Não restaura outros registradores/flags/FPU.

Não usar este helper sozinho como substituto de preservação de contexto. Não foi executado.

## 5. Wrapper full-save1403AD270

**CONFIRMADO:** contrato dos mesmos três argumentos Win64; corpo RVA003AD270..003AD68E. Prefixo:

```text
41 57 41 56 56 57 53 48 81 EC 80 01 00 00 31 C0 85 C9
0F 84 F7 03 00 00 45 85 C0 41 0F 95
```

Guardas anteriores à chamada:

- guestVA!=0;
- count não éFFFFFFFF e não excede3FFFFFFE;
- args host nãoNULL se count!=0;
- cálculo `ESP-(count*4+4)` não sofre underflow;
-1403C7EB0 aceita esse intervalo para escrita.

1403AD30B..1403AD4B2 salva emstack host os globals da seção3, incluindo ESP,ECX,EAX,resumePC, bancos, FCW e MXCSR emulado.1403AD4B9 captura também o MXCSR físico da thread host comSTMXCSR.1403AD4D7 chama14039EFA0.1403AD4E2..1403AD679 restaura os globals e o MXCSR físico;1403AD67D devolve o sucesso host.

**Melhoria frente ao mapa antigo:** este wrapper já implementa a preservação extensa. Não é necessário inventar um snapshot parcial por conta própria para chamadas que caibam no contrato dele.

**Limites importantes do contrato:**

1. O wrapper restaura EAX/EDX/x87 guest. O retorno guest de um allocator/build **não está disponível nesses globals após retornar**. EAX host é só sucesso, não o ponteiro do veículo.
2. Não existe parâmetro `this`. Uma chamada thiscall consome ECX guest1428E7B28. Se o chamador o ajustar antes de entrar, o wrapper salvará/restaurará o valor já ajustado. Seria necessário preservar/restaurar o ECX original externamente e garantir o mesmo contexto durante toda a operação.
3. Saídas em memória guest continuam existindo. **CONFIRMADO do mapa da factory:**2CBDE0 publica o produto emfactory+44,carData+4C, e2CBC40 mantém os resultados após o retorno. Isso oferece um caminho estático para ler outputs de uma factory temporária, sem depender de EAX; não valida o lifecycle nem a criação em qualquer fase.
4. O wrapper trabalha no ESP fornecido pelos globals. Não reserva scratch separado, não baixa ESP com red zone, não prova que a área descartada pelo HLE possa ser reutilizada e não instala limites de stack para as chamadas encadeadas.
5. Não há teste direto de `GetCurrentThreadId` ou comparação de currentguestthread no corpo. A presença do registro global não prova que quem chamou é sua thread/fiber atual.

### Callback anterior à execução

**CONFIRMADO:**1403AD4BD lê o callback opcionalQWORD142C127E8. SeNULL ou se o callback retorna EAX!=0, executa14039EFA0; se retorna0, não chama guest e devolve falha após restaurar o contexto. O setter interno1403ADAB0 apenas grava esse pointer. Em14031B04D..14031B054 o runtime instala14031B690; outros caminhos o zeram durante limpeza.

**CONFIRMADO: há aquisição de mutex global e ela pode esperar indefinidamente.** Não é apenas uma consulta TLS ou uma comparação de thread:

```text
14031067B lock cmpxchg byte ptr [1427E9EE8],bl ; tenta adquirir, esperado=0
140310683 jne 1403106D9
1403106D9 lea rcx,[1427E9EE8]
1403106E0 call 1423ED770                      ; caminho de contenção
1403106C2 lea rax,[1427E9EE8]                 ; retorna objeto global adquirido

1423ED790 pause                              ; espera ativa inicial
1423ED7D6 xchg byte ptr [rsi],al              ; marca estado2 / contendida
1423ED7EC mov r9d,FFFFFFFF
1423ED7F2 call rbx                           ; IAT1424146F8: WaitOnAddress
```

O objeto retornado emRAX pelo1403105B0 é o mesmo endereço global1427E9EE8 em todos os caminhos de retorno normais examinados. A inicialização usa auxiliares relacionados a TLS/panic, mas isso **não** torna o mutex nem o contexto retornado locais à thread. O caminho de contenção não testa identidade do dono nem oferece prazo finito.

14031B690 guarda esse endereço emRSI, lê objeto+0xD8 e, quando necessário, chama140334F60 com o objeto+8 e uma estrutura derivada de[+D8]. Mantém o mutex até14031B709 ou14031B779, onde faz `xchg byte ptr [rsi],0`; se o valor anterior era2, chama1423ED830, thunk de `WakeByAddressSingle` (IAT1424146F0). O resultado normal do processamento esperado tem discriminante9; outros resultados podem emitir diagnóstico e retornar0, impedindo a chamada guest.

**Limite decisivo / ABI ainda não validada para o gancho:** uma chamada reentrante enquanto a mesma thread detiver esse mutex pode ficar emWaitOnAddress sem que seu proprietário consiga prosseguir. Não se demonstrou que o caminho XInput tenha esse mutex adquirido no instante do proxy, portanto seria incorreto afirmar que o deadlock é inevitável; também não há prova de que o mutex esteja livre ou que o callback possa processar todas as operações com segurança durante o HLE interrompido. O retorno exato140306984, a presença de currentguestthread e o full-save não provam essa propriedade. O helper faz a chamada guest diretamente após o callback; ele não enfileira automaticamente uma solicitação para um ponto futuro seguro. **Atualização da seção 8:** a cadeia direta do input não referencia nem chama a aquisição desse mutex; a reentrância do callback durante o HLE continua não provada.

**Não substituir, zerar ou contornar esse callback** para obter sucesso de chamada. Uma rejeição dele é resultado do runtime a respeitar. Sem contrato de reentrância/locks e lifecycle demonstrado, não implementar executor no proxy. Não há callback do mod instalado por esta análise.

### Retorno normal, falhas e unwind

**CONFIRMADO:** `.pdata` do wrapper aponta unwindRVA0271B6EC, bytes iniciais `01 0E 07 00`; flagsEH/UH=0. O helper baixo nível usa unwindRVA0271AEAC, `01 0A 06 00`, também sem handlerEH/UH. A restauração observada está no caminho de retorno normal, não em um finally/handler de limpeza.

Portanto **NÃO PROVADO:** recuperação após exception do guest/HLE, longjmp, fatal do resolver, abort, nonlocaljump ou um caminho que não retorne. Mesmo uma capturaSEH externa não demonstra restauração correta da máquina guest. Salvar o contexto não reverte alocações/registros de veículo já efetuados antes de uma falha.

## 6. Stack guest e possível scratch: limites demonstrados

**CONFIRMADO:**1403C58B0 cria o registro de thread e chama1403D5A40 com dois ponteiros de saída. O primeiro fornece ESP inicial; o segundo é a baseguestA usada como FS/KPCR.1403C5A64..1403C5A72 grava ESP emrecord+0C eA emrecord+20.

1403D5A40 aloca a stack comumS com0x10000 bytes; no sucesso1403D5BEF..1403D5BFE devolve primeiroS+10000 e segundoA.1403D5420 inicializa as estruturas de controle; trechos relevantes:

```text
FS/KPCRguest = A = [1428E7B84] = currentRecord+20
[A+28] = A+400                   ; ponteiro KTHREAD criado nesse caminho
KTHREAD+1C = StackBase/top
KTHREAD+20 = StackLimit/bottom
```

Evidência:1403D559B..1403D55A4 instala o ponteiroA+400 emA+28;1403D55B5..1403D55F9 escreve o limite superior emKTHREAD+1C;1403D55FD..1403D5605 escreveS emKTHREAD+20. Para uma thread comum o topo éS+10000; há um ramo específico da stack de boot usando os globals142855734/142855738. Não fixar64KiB para todas as threads.

**Não usar `[KPCR+4]` como topo da stack:** no inicializador ele recebe outro campo do layout (1403D557E..5581), relacionado à região do controle/TLS. O topo usado para a stack de execução é o campo KTHREAD+1C acima. Os nomes derivados da disposiçãoXbox não substituem a evidência de cada acesso.

Receita de **validação futura**, sem execução nesta etapa:

- Verificar build/prefixes e contexto exato do proxy; currentRecord alocado e emexecução, FS/KPCR do registro igual ao global, KTHREAD e seus campos mapeados/coerentes.
- Exigir `StackLimit < ESP <= StackBase`, aritmética sem overflow, destino em memória guest normal e espaço abaixo deESP para o scratch, red zone, frame de argumentos **e profundidade demonstrada do callee**.
- Reservar scratch acima do ESP temporariamente rebaixado usado para os calls, para o próprio callee não sobrescrevê-lo ao crescer a stack para baixo. O workspace de factory+CarCfg não pode sobrepor o frame nativo interrompido nem as áreas que o HLE ainda lê.
- Preservar/restaurar o ESP/ECX originais fora do wrapper se foram rebaixados/alterados antes de entrar. O full-save só restaura o snapshot que viu na entrada.
- Só ler outputs na mesma vida útil e antes de devolver o scratch à stack original. Não armazenar pointer para um CarCfg temporário emvehicle+64 após o hook retornar.

**NÃO PROVADO:** um tamanho mágico de red zone (inclusive100h), apenasMapped ou somente esses limites é suficiente parafactory2CBC40. O build faz chamadas aninhadas e pode entrar emIO/yield; a profundidade e a vida útil das configurações/bancos ainda precisam ser auditadas.

## 7. Contexto de input atual e limite de autorização

**CONFIRMADO estático:** IAT `xinput1_4.dll` ordinal2 em1424146D8; único `call [rip+IAT]` em14030697E, bytes `FF 15 54 DD 10 02`, retorno140306984. A função envolvente1403068D0 aplica backoff, caminho de nativecontrollers e polling. Não usar o endereço antigo1404E35C4.

O guest84C00 resolve para14076FBA0 e chama oHLE140300D00 em14076FC4A depois de publicar ESP e registradores no arquivo global. O caminho de input nativo ainda pode não consultar o proxy; chamadas da UI/overlay ao mesmoDLL não são automaticamente contexto guest. A guarda de retorno exato existente continua necessária.

Isso demonstra onde o gancho entra no fluxo, mas **não transforma input em ponto universal de construção segura**. Não há nesta análise prova de que o wrapper com factorypodebloquear/trocarfiber e reentrar sem violar o código interrompido. Nenhuma rotina guest foi chamada para testar leitura, alocação, build ou destruição.

**Próximo passo autorizado possível é offline:** acrescentar ao inspector leitura das assinaturas/contratos acima e testes de aritmética/ranges, mantendo execução desabilitada. Antes de qualquerexecutor ou teste ao vivo ainda é necessário fechar lifecycle por slot, recursos/rollback, bindings e uma política comprovada para contexto/yield/reentrância. Lookup validado não equivale a ABI/lifecycle autorizados ou testados.

## 8. Extensão estática: mutex do callback no caminho XInput (19/10, mesma build)

Método: `tools/inspect_native_bridge.py` lê o PE como bytes, usa `.pdata` para delimitar funções, procura referências RIP-relativas (`disp32`) e monta o grafo de chamadas diretas (`E8 rel32`). Nada foi carregado, executado ou aberto como processo. A saída reproduzível fica em `out/native-bridge-current.json` (privado).

**CONFIRMADO nesta build (`51354a04…`, timestamp `6AD54432`):**

- As únicas referências RIP-relativas ao mutex do callback (`1427E9EE8`) estão em `31067F`, `3106C5` e `3106DC`, todas dentro da função de aquisição `3105B0` (tamanho 354). O callback `31B690` chama `3105B0` em `31B6A1` e libera o mutex em `31B709`/`31B780`.
- O caminho do input é `300D00` (HLE de `84C00`) → `3068D0` (retorno exato `306984` usado pelo proxy) e `306BB0` (poll de controle). Nenhuma dessas três funções contém referência RIP ao mutex.
- Subindo somente chamadas diretas a partir de `300D00`, foram examinadas 13 funções ancestrais; nenhuma referencia o mutex nem chama `3105B0`.
- Os registros de unwind de `3AD270` (full-save) e `39EFA0` (call baixo nível) continuam com versão 1, flags 0, sem handler de exceção/unwind (`271B6EC` e `271AEAC`).

**Consequência delimitada:** a hipótese específica de que o próprio caminho XInput já deteria o mutex do callback não é sustentada pela cadeia direta. Isso remove esse deadlock como *inevitável* nesse ponto, mas **não** prova que o callback `31B690` possa ser reentrado com segurança enquanto a chamada HLE `84C00` está ativa. Também não cobre chamadas indiretas nos ancestrais, retorno anormal/exceção, profundidade de stack para a fábrica, vida útil da CarCfg, capacidade dos buckets nem perfil de habilidade. `execution_safe` e `native_calls_enabled` continuam false. Nenhuma função guest foi chamada.

**Testes:** `python -m unittest discover -s tools/tests -p test_native_bridge.py -v` passou 12 casos (9 anteriores + `.pdata`/unwind, referência RIP-relative e grafo de chamadas por dono). `python tools/inspect_native_bridge.py D:\dev\mc3-coop-data\xrun\MC3Runner.exe` passou e recusaria a build se qualquer assinatura, limite de função, referência ao mutex ou unwind mudasse. A cópia `testgame` tem outro SHA/timestamp (`6AD5001B`) e não tem perfil de runtime; o inspetor continua apenas com a descoberta de tabela para ela.
