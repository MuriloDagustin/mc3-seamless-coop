# IA dos adversários — sincronização com autoridade no host

Análise estática em 18/10/2026. Nenhum jogo, runner ou executável do jogo foi iniciado. Nenhum processo foi encerrado. VAs abaixo são do **Xbox**, de 32 bits; acesso no host = `base + VA`. Todos os ponteiros devem ser resolvidos novamente em cada mundo/corrida. `R32`, `W32`, `W8`, `F32` indicam tamanho; floats são IEEE-754 de 32 bits.

**CONFIRMADO** significa demonstrado nos comentários asm do código recompilado ou nos mapas verificados, não testado em execução. **PALPITE** significa proposta de integração/efeito que ainda exige validação no jogo. Um conjunto de campos conhecido não transforma uma receita de sincronização em receita confirmada.

## Resultado e limite da análise

- **CONFIRMADO:** a IA usa o mesmo veículo/carro físico do jogador. É possível localizar pose, velocidades, comandos e progresso de cada entrada do OppMgr; o índice da entrada é gravado no próprio objeto.
- **CONFIRMADO:** a ordem de construção segue os descritores carregados, com filtragem pelo setup. Carregar a mesma corrida não garante configurações de carreira/seleção de adversários iguais entre saves.
- **CONFIRMADO:** os caminhos examinados não têm um teste de `entry+0x56`, `entry+0x64` ou `kind==0` que pule universalmente o cérebro. `ThrottleLimit=0` não desliga navegação, recuperação, direção nem todos os comandos.
- **PALPITE / NÃO RESOLVIDO:** não encontrei um campo por adversário comprovado que desligue todo o cérebro preservando desenho, colisão e inscrição na corrida. Assim, a receita de puppets abaixo é um roteiro experimental, **não uma solução garantida para “sem brigar com a lógica local”**. Esse é o bloqueio técnico restante, além da fase exata do gancho de escrita.

## 1. Identidade, veículo e física

### Cadeia principal (CONFIRMADO)

```text
mgr     = R32(0x534074)
N       = R32(mgr+0x00)           // total de entradas, inclusive não competidores
Nc      = R32(mgr+0x04)           // contagem de competidores
array   = R32(mgr+0x08)
opp(i)  = array + i*0x43C         // 0 <= i < N
racer   = opp(i)+0x0C            // base embutida, NÃO desreferenciar +0x0C
vehicle = R32(opp(i)+0x20)        // = R32(racer+0x14)
car     = R32(vehicle+0x08)       // vehCar; não confundir com vehicle
input   = R32(vehicle+0x04)       // input de IA, não mcPlayerInput
inst    = R32(vehicle+0x1C)
coll    = R32(car+0x68)
body    = R32(coll+0x10)
driver  = R32(opp(i)+0x5C)
desc    = R32(race+0x2C) + i*0x88
race    = R32(R32(0x533E24)+0x04)
```

`OppMgr::Init` 0x1E43C0 aloca `N*0x43C` (0x1E440A), guarda o array em `mgr+8` (0x1E4440) e chama 0x1ED2F0 com o índice em 0x1E44CE. A cada iteração soma 0x88 ao descritor e 0x43C à entrada (0x1E4541/0x1E4547). O construtor 0x1EC2D0 chama a base racer com `lea esi,[edi+0x0C]` em 0x1EC2DA.

Evidência decisiva da inicialização:

```asm
001ED31A call 0x001FA970       ; factory do adversário
001ED323 call 0x001FB040       ; build
001ED328 mov [esi+0x20], eax   ; esi=opp, retorno=vehicle
001ED349 mov [esi+0x54], bx    ; identidade u16 = índice passado
001ED35B mov edx, [esi+0x20]
001ED35E mov ecx, [edx+0x08]   ; o car está dentro do vehicle
```

A factory 0x1FB040 usa o mesmo construtor de customização 0xF4430 e aplicação de CarCfg 0xF32A0 usados nos humanos. A entrada `opp+0x20` não é o bloco de comandos/física diretamente.

### Campos físicos e de controle (CONFIRMADO)

| Endereço relativo | Tipo/tamanho | Significado e evidência |
|---|---|---|
| `inst+0x10` | 12 floats / 0x30 bytes | Matrix34, quatro vetores de 3 floats; posição no último vetor |
| `inst+0x34..0x3C` | vec3 | Posição visual/mundo, lida para checkpoints de IA em 0x1E01A7..0x1E01C8 |
| `body+0x5C` | Matrix34 / 0x30 | Pose física; posição `body+0x80..0x88` |
| `body+0x44` | vec3 | Velocidade linear, **não posição** |
| `body+0x50` | vec3 | Velocidade angular, **não ângulos de rotação** |
| `body+0x2C` | vec3 | Momento linear `massa * velocidade`, setter 0x3432B0 |
| `body+0x38` | vec3 | Momento angular; setter 0x343300 usa pose e inércia |
| `body+0x04` / `+0x0C` | float / vec3 | Massa / inércia diagonal local; não importar valores de outro carro sem validar CarCfg |
| `coll+0x14` | ponteiro Matrix34 | Matriz usada pelo collider; pode diferir de `body+0x5C` devido ao centro de massa |
| `car+0x6C` | float | Direção |
| `car+0x70` | float | Acelerador |
| `car+0x74` | float | Freio/ré |
| `car+0x78` | float | Freio de mão |
| `car+0x7C` | u32 | Flags de input, formato do carro comum; não importar bits desconhecidos cegamente |
| `car+0xB0` | float | Velocidade escalar em cache |
| `opp+0xC0` / `+0xCC` | vec3 / float | Movimento/velocidade em cache da IA, atualizados por 0x1EE1A0 |
| `opp+0x54` | u16 | Índice da entrada no OppMgr |
| `opp+0x68` | u32 | Tipo de driver registrado na construção, não um seletor de execução universal |

O controlador escreve os comandos a cada atualização, inclusive após uma escrita do mod:

```asm
001FCAC4 movss xmm0, dword ptr [esi+0x114]  ; limite no input IA
001FCACC movss xmm1, dword ptr [esi+0x0C]   ; throttle calculado
001FCAD1 comiss xmm0, xmm1
001FCAD4 jbe 0x001FCAD9
001FCAD6 movaps xmm0, xmm1
001FCAD9 movss [ecx+0x70], xmm0
001FCADE mov ecx, [esi+0x34]                ; car
001FCAE1 mov edx, [esi+0x18]
001FCAE4 mov [ecx+0x6C], edx
001FCAEA mov ecx, [esi+0x14]
001FCAED mov [eax+0x78], ecx
001FCAF0 mov eax, [esi+0x10]
001FCAF6 mov [edx+0x74], eax
```

Há exceções antes desse clamp: `input+0x11C` aciona uma manobra que escreve acelerador=1 e freio de mão=1 (0x1FC984..0x1FC9DB); `input+0x11E` escolhe outra manobra com freio=1 (0x1FC9DC..0x1FCA44). Portanto `input+0x114=0` sozinho também não é desligamento.

**CONFIRMADO: escrever só a matriz do phInst é insuficiente para autoridade persistente.** 0x2A38A0 primeiro atualiza o corpo, chama 0x2A37B0 e copia a matriz do collider para `inst+0x10`. A renderização também copia o phInst para o modelo em 0xFEFA0..0xFF01B.

```asm
002A37B3 mov eax, [esi+0x10]  ; body
002A37BE mov ecx, [esi+0x14]  ; matriz do collider
002A37C1 add eax, 0x5C
002A37C4 cmp ecx, eax
002A37CC push eax
002A37CD call 0x00030330      ; cópia, depois ajuste pelo centro de massa
002A38B2 mov ecx, [esi+0x04]  ; inst
002A38BD mov esi, [esi+0x14]  ; matriz do collider
002A38C0 add eax, 0x10        ; destino: matriz do inst
002A38C7 mov ecx, [esi]
002A38C9 mov [eax], ecx      ; seguem os demais 11 dwords
```

**CONFIRMADO:** os setters físicos mantêm caches que não podem ser esquecidos. Em 0x3432B0, a velocidade é escrita em `+0x44..4C`, multiplicada por massa `+4` e gravada em `+0x2C..34`. 0x343300 grava spin em `+0x50..58` e calcula `L=I_mundo*spin` em `+0x38..40`. Copiar apenas velocidades deixa momentos anteriores ativos; aplicar só o snapshot da inst deixa o próximo passo físico desfazê-lo. Copiar momentos do host só é coerente se massa, inércia e orientação forem compatíveis; é preferível recalcular com os parâmetros locais como nesses setters.

### Caminho pela lista de corredores (CONFIRMADO, com restrição de ordem)

```text
list = R32(0x558838)
count = R32(list+0x28)
racer(k) = R32(list + 4*k)    // vetor começa em +0, não em +8
vehicle = R32(racer(k)+0x14)
```

0x2C8710 adiciona primeiro todos os slots humanos (inclusive NULL), depois `opp(i)+0x0C` **somente** se `desc(i)+9 != 0` (0x2C8806..0x2C8830). Logo não assumir `i=k-numHumanos` sem testar o conjunto de competidores. Para recuperar `i`, use `delta=racer(k)-(array+0x0C)`, exija `delta>=0`, `delta%0x43C==0`, `delta/0x43C<N` e confirme `u16(opp+0x54)==i`.

A tabela `race+0x65C` é diferente: 0x1CD3D0 insere os progressos humanos e 0x1CD400..0x1CD41C insere os progressos IA com stride 0x60, sem a filtragem desse trecho da lista. Em corridas normais, a lógica de competição pressupõe competidores no início do OppMgr (0x1E017C e 0x1CF725..0x1CF79A). **PALPITE:** o conteúdo stock satisfaz sempre essa organização. Valide o prefixo de `desc+9` e a associação de tabelas; se houver não competidores intercalados, não usar cegamente o índice da lista para progresso. Limite da lista: dez ponteiros; recusar contagens incompatíveis.

## 2. Progresso: volta, checkpoints, classificação e chegada

### Estrutura por IA (CONFIRMADO)

`P(i) = R32(race+0x658) + i*0x60`. A relação com o mesmo `i` do OppMgr aparece diretamente em 0x1DF1C0: o progresso usa `i*0x60` e, no final, o veículo usa `i*0x43C+0x20` (0x1DF24E..0x1DF25D). Nos humanos a estrutura começa em `race+0x98+i*0xB8`; não transportar o bloco humano para a IA.

| Offset em P(i) | Tipo | Significado |
|---|---|---|
| `+0x00` | ponteiro | Vtable 0x4698F8; nunca transmitir/copiar |
| `+0x04` | u8 | Terminou |
| `+0x05` | u8 | Preso; participa da classificação |
| `+0x06` | u8 | Desistência/eliminação; participa da classificação |
| `+0x07` | u8 | Flag de participação/sobrevivência, inicial 1; semântica completa varia por tipo |
| `+0x08` | u8 | Flag transitória; 0x1CFA4A zera a cada atualização, não usar como identidade |
| `+0x0C` | float | Timer/tempo restante comum; uso exato na IA depende do tipo |
| `+0x10` | float | Tempo de chegada |
| `+0x14` | i32 | Posição/classificação, **base 1**, recalculada |
| `+0x18` | i32 | Voltas restantes; chegada quando decrementa para zero |
| `+0x1C` | float | Tempo da última passagem de volta |
| `+0x24` | float | Tempo da última volta |
| `+0x28` | float | Melhor tempo de volta |
| `+0x30` | float | Tempo da última passagem por checkpoint |
| `+0x34` | i16 | Checkpoint corrente/alvo; -1 quando não há próximo; depende da modalidade |
| `+0x38` | float | Distância horizontal ao checkpoint ao quadrado, não metros |
| `+0x3C` | float | Métrica de ordenação/progresso usada pelo ranking |
| `+0x44` | i32 | Pontuação, usada nas modalidades por pontos (0x1CF515/0x1CF52D) |
| `+0x48` | u8 | Número de checkpoints contabilizados na volta |
| `+0x49` | u8 | Auxiliar de checkpoints, sem semântica completa confirmada |
| `+0x4C..0x58` | 4 u32 | Bitmap de checkpoints da volta (128 bits de armazenamento) |
| `+0x5C` | ponteiro | Nome próprio alocado no processo; nunca copiar entre cópias |

Não há um único campo “volta atual” independente confirmado: derivar a volta a partir de voltas totais (`i32 race+0x6C8`, classe comum; verificar modalidade) e restantes. A inicialização 0x1D1911 lê `race+0x6C8` e 0x1D1917 grava esse valor em `P+0x18`. O total de checkpoints é `race+0x24`. `race+0x20` é ponteiro de descritores alternativos, não número de voltas. Chegada e eliminação exigem tratar as flags, não apenas `lapsLeft==0`.

**Não escrever `P(i)+0x5D` como “venceu”.** Esse byte pertence ao ponteiro `P+0x5C`. O setter de nome 0x1CA410 libera o nome anterior, duplica a string com 0x20E170 e grava o ponteiro em 0x1CA43A. O byte de vitória `+0x5D` dos mapas anteriores pertence ao progresso HUMANO, de tamanho 0xB8.

### Evidência de volta e chegada (CONFIRMADO)

```asm
001DF1CF mov ecx, [edi+0x658]  ; edi=race, ebx=i
001DF1D5 lea esi, [ebx+ebx*2]
001DF1D8 shl esi, 0x05        ; i*0x60
001DF1DB mov eax, [esi+ecx*1+0x18]
001DF1F3 dec eax
001DF1F8 mov [esi+0x18], eax
001DF1FB movss [esi+0x1C], xmm0
001DF211 test eax, eax
001DF213 jnz 0x001DF235
001DF215 mov byte ptr [esi+0x04], 0x01
001DF219 movss [esi+0x10], xmm0
001DF21E mov ecx, [edi+0x1254]
001DF226 inc ecx
001DF227 mov [edi+0x1254], ecx
001DF230 call [edx+0x68]     ; efeito específico de chegada da IA
```

`race+0x1254` é contador de IAs que chegaram na classe comum; não é contador de humanos (`+0x1250`). Ao replicar flags sem evento nativo, atualizar o contador coerentemente, uma vez pelo conjunto, não incrementar a cada pacote.

Checkpoints da IA são verificados em 0x1E0170 / 0x1E04C0, despachados por 0x1E0680. Leem `vehicle->inst+0x34`, consultam bitmap em `P+0x4C`, incrementam `P+0x48` e, ao atingir `race+0x24`, chamam virtual `race+0x134` (0x1DF1C0 na classe comum). **Há também dependência do roteiro local:** `opp+0xE0` e `opp+0xE4` limitam as passagens aceitas (0x1E0257..0x1E026A e 0x1E0549..0x1E0556). Teletransportar a matriz até a linha não garante passagem se o roteiro local não estiver coerente.

0x1DF235 chama 0x1CD0A0 na volta intermediária; esse helper limpa `+0x34`, `+0x48/+0x49` e os quatro dwords do bitmap. Portanto não basta copiar apenas o próximo checkpoint.

### Classificação (CONFIRMADO, classe comum)

0x1CF440 reinicializa `P+0x14=1` e compara todos os participantes através de `race+0x65C`; roda por frame e de novo no pagamento 0x1CFC50. Os critérios incluem preso/desistência, tempos dos finalizados, voltas restantes, chegada e métrica `+0x3C`. Modalidades por pontos seguem o ramo que usa `+0x44`.

```asm
001CF460 mov edx, [ebx+0x65C]
001CF466 mov ecx, [edx+eax*4]
001CF469 mov dword ptr [ecx+0x14], 0x01
001CF600 movss xmm0, dword ptr [edi+0x10]  ; ambos chegaram
001CF605 comiss xmm0, [esi+0x10]
001CF60E mov eax, [esi+0x18]
001CF611 mov ecx, [edi+0x18]
001CF614 cmp ecx, eax
001CF6B2 movss xmm0, dword ptr [edi+0x3C]
001CF6B7 comiss xmm0, [esi+0x3C]
```

Consequência: impor só `+0x14` é temporário. Para o resultado ser coerente é necessário espelhar os **dados que originam** a posição, inclusive os humanos que competem com essas IAs. Empates perfeitos podem produzir posições iguais; validar contra o ranking do host.

## 3. ID estável entre host e parceiro

**CONFIRMADO: ID local durante uma corrida = `u16(opp+0x54)` = índice `i` do array criado pelo OppMgr.** Não usar VA, nome traduzido, ordem de chegada, modelo de carro ou posição na grade como ID único.

A origem é a ordem do parser do arquivo da corrida em 0x1D3AE0: percorre blocos de adversários em ordem, testa `setup+0x3D4`, pula os desabilitados em 0x1D3D96..0x1D3D9F e compacta descritores de 0x88 bytes. O ordinal bruto do arquivo não é preservado por `opp+0x54` após a filtragem. Há ainda remoção de policiais conforme setup em 0x1D4259..0x1D42A4 e limites de contagem em 0x1D42BE..0x1D42FB. OppMgr::Init pode substituir descritores de certos modelos usando `race+0x20/+0x1C` (0x1E4481..0x1E44BA).

**PALPITE condicionado:** a mesma versão do jogo, mesmo arquivo, mesma máscara/opções e mesma resolução de configurações de carreira produzem o mesmo `i`. Isso não foi testado com duas cópias e não se deduz só de “mesma corrida”. A escolha de CarCfg/adversário e nomes de carreira pode depender do save; o construtor de driver também consome RNG (0x1EBF05..0x1EBF41), portanto nem drivers com o mesmo índice simulam identicamente.

Receita de identificação proposta (**PALPITE de protocolo**):

1. Usar chave de corrida `{geração/nonce do host, City, nome do arquivo/RaceIdx, versão/build}`. O UniqueId da carreira é local ao save.
2. Antes da largada, host envia manifesto ordenado `{i, tipo desc+0, competidor desc+9, tipo driver opp+0x68, modelo CarCfg+0xE3, spawn desc+0x38..44, assinatura dos dados de rota e da configuração}` e máscara/setup necessários. Strings/dados de rota devem ser serializados; não hashear ponteiros brutos.
3. Parceiro confirma contagens e manifesto. Se divergirem, reconstruir o setup compatível na próxima carga ou recusar a sincronização de IA dessa corrida; não casar “o próximo carro parecido”.
4. Pacotes usam `{geração, i, sequência, tick do host}`. Rejeitar pacotes antigos, índices inválidos, NaN e matrizes/pointers incompatíveis. Cada lado resolve seus próprios ponteiros.

## 4. Desligar o cérebro com dados: o que foi provado e o que não foi

### Caminho de execução confirmado

```text
0x1EDB0 (frame normal)
  0x1EEC9 -> física 0x29D210
  0x1EED4 -> OppMgr::Update 0x1E4840
       para cada i < mgr->N: 0x1EE1A0 ou 0x1EDF00
       0x1EE404 -> 0x1ECA90 -> driver virtuais +0x3C, +0x0C, +0x40
       0x1EE66A -> 0x1ED9C0 -> cache de comando no input
       0x1EE674 -> driver virtual +0x44
  0x1EEF7 -> Player/VehicleManager (inclui inputs/modelos)
  0x1EF3E -> atualização da corrida/checkpoints
```

Há caminhos alternativos quando pausado/em outros estados; não assumir que essa ordem vale em todos os ticks. A mera execução do XInputGetState não demonstra que o mod está antes/depois de todas essas operações.

| Candidato | Veredito estático |
|---|---|
| `driver=0` em `opp+0x5C` | **CONFIRMADO perigoso:** dereferências sem teste em 0x1ECA93..0x1ECAA8 e 0x1EDF13; não usar |
| `opp+0x68=0` | **CONFIRMADO insuficiente nos caminhos vistos:** a seleção aloca um driver/vtable em 0x1ED2F0; o tick chama o ponteiro existente sem escolher de novo por kind |
| `opp+0x56=0` / `+0x64=0` | **PALPITE refutado como interruptor universal:** inicializados, mas não testados antes do dispatch em 0x1E4840/0x1ECA90; +0x56 também é usado por comportamento policial |
| `driver+0x24=0` (base throttle) | **CONFIRMADO insuficiente:** 0x1EB873 escreve throttle efetivo 1 abaixo da velocidade mínima; rubber band pode aplicar outros limites |
| `driver+0x28=0` / `input+0x114=0` | **CONFIRMADO insuficiente:** reescritos por 0x1EB540 / 0x1EDA95..0x1EDAA6; manobras especiais do input ignoram o clamp normal |
| `driver+0x128=0` | **CONFIRMADO insuficiente:** só força uma consulta a 0x210100; essa função retorna sempre `al=1`, não desliga o Update |
| `driver+0x168=0` (comportamento atual) | **CONFIRMADO perigoso:** 0x1E93D0 e 0x1EE38A têm usos sem teste; transição 0x1EAAA0 também exige callbacks e campos auxiliares |
| `setup+0x10D=0` antes da carga | **CONFIRMADO inadequado:** impede criação de adversários (0x1E43CB; também 0x1D42F1), não mantém puppets |
| Zerar `mgr+0` / `mgr+4` | **PALPITE inadequado para produção:** suprime laços globalmente e atualizações de progresso, quebra invariantes; 0x1E471C usa N para decidir destruir o array. Não é opção por carro |
| Alterar `desc+9`, retirar racer da lista ou forfeit | **CONFIRMADO inadequado ao objetivo:** muda participação/classificação; não é só suspensão do cérebro |

Um candidato adicional para investigação (**PALPITE**, não aplicar como receita) é `car+0x15C`: 0x120A00 retorna zero quando esse dword é não zero, e 0x1EE1EC..0x1EE1F6 pula o trecho principal do driver se o virtual `car->vt+0x2C` retornar zero. **Não confirmei a associação desse slot com 0x120A00 nos dados da vtable, nem que forçar esse estado preserve todos os efeitos físicos.** 0x120EE0 usa esse campo como máscara de estados/manobras e tem efeitos laterais; uma escrita bruta pula esses efeitos. Confirmar primeiro a vtable e todos os consumidores seria a próxima análise útil, antes de qualquer ensaio.

Não há neste relatório uma escrita “CONFIRMADA” que satisfaça simultaneamente os três requisitos de suspensão. Não confundir limitação de potência com neutralização. Também não proponho mudar vtables/bytes de instrução nem chamar funções guest, pois a receita pedida é apenas de dados.

## 5. Receita de replicação por frame — proposta, com condições explícitas

**PALPITE de integração, dependente de resolver a suspensão/fase do gancho.** Sem isso, o resultado é correção periódica sobre simulação local, sujeita a saltos, resets e impulsos divergentes. Colisão local mantida também gera eventos diferentes do host; a pose autoritativa precisa prevalecer no tick seguinte, e não torna a física das duas cópias determinística.

### Preparação

- Esperar mundo e corrida prontos; confirmar manifesto e tipos. Guardar geração, contagens e ponteiros locais; invalidar tudo quando `race`/`mgr`/array mudar, ao sair para menu ou ao recarregar.
- Manter veículo, input, drawable, phInst, collider e corpo registrados. Não retirar racer da lista e não desligar colisão/massa para “resolver” a IA.
- Exigir ponto de escrita conhecido na thread do jogo e uma única aplicação por tick. O gancho proxy só é contexto guest quando `_ReturnAddress()==0x1404E35C4` nessa build (ver RE-CORRIDA, seção native-abi). Pode ser chamado várias vezes ou deixar de ser chamado com native controllers/backoff. Essa checagem **não** o transforma automaticamente em gancho pós-física/pré-ranking.

### Estado mínimo proposto do host, por i

```text
pose física: body+0x5C (12 floats)
pose do collider/inst: inst+0x10 (12 floats), separada da anterior
velocidade e spin: body+0x44 e +0x50
comandos: car+0x6C..0x78; flags conhecidas de car+0x7C
progresso escalar: campos listados na seção 2, excluindo vtable/nome
estado de passagem: opp+0xE0 e +0xE4 (inteiros), necessário se deixar checkpoints locais ativos
seq/tick/geração, estado final, ranking autoritativo
```

Enviar ambas as poses evita confundir centro de massa com origem do desenho. Modelos/CarCfg diferentes invalidam a hipótese de offset/inércia compatível. Não enviar o bloco de driver ou o bloco de progresso inteiro: contêm ponteiros/vtables locais.

### Escritas propostas no parceiro

1. Consumir snapshot completo com sequência crescente na thread do jogo; não aplicar metade de um pacote. Interpolar só pose/velocidades; chegada/checkpoint/pontuação são estado discreto. Não interpolar através de reset/teleporte.
2. Aplicar Matrix34 do corpo em `body+0x5C`; aplicar spin e velocidade e recomputar `p=m*v` e `L=I_mundo*spin` com massa/inércia **locais**, como 0x3432B0/0x343300. Não copiar um quaternion sobre Matrix34. A consistência adicional dos caches/solver/contato após escrita bruta permanece **PALPITE**, não foi demonstrada uma operação completa de teleporte só com stores.
3. Aplicar a pose de origem do host em `R32(coll+0x14)` e `inst+0x10`, evitando escrita dupla incorreta caso aliasem o mesmo endereço do corpo. Se `coll+0x14==body+0x5C`, validar a compatibilidade da origem antes de usar duas poses diferentes. Atualizar `car+0xB0=|v|` e os caches da entrada coerentes somente se forem necessários para o caminho ativo.
4. Aplicar os quatro comandos e os bits conhecidos de flags; para impedir reescrita posterior, ainda é necessário um interruptor de driver/input confirmado ou um gancho após o último escritor. Escrever `driver+0x28=0` e `input+0x114=0` pode ajudar no experimento, mas **não cumpre essa condição**.
5. Aplicar separadamente os campos de progresso, bitmap e contagem de CP do host. Manter nome/vtable locais. `opp+0xE0/+0xE4` não são o mesmo que volta/CP no progresso; se a navegação local continuar, volta a reescrevê-los em 0x1EE379..0x1EE3A5. Não copiar ponteiros `driver+0x168/+0x178` do host.
6. Para ranking consistente, aplicar também os progressos humanos por identidade de jogador (remapear host/joiner para os slots locais), antes do ranking local. Compare o `+0x14` produzido com o host. Sem controlar a fase de 0x1E0680/0x1CF440, progresso local pode avançar ou terminar antes do pacote; isso continua sem solução garantida neste gancho.
7. Ao perder snapshots, não promover o parceiro a autoridade silenciosamente. Política proposta: extrapolação curta limitada, depois manter última pose e indicar perda da sincronização. Qualquer freeze que preserve colisão é experimental até validar interação com o solver.

### Quando o host termina: posições finais iguais

**CONFIRMADO:** chegada do humano e “corrida encerrada/resultados” são eventos distintos; se o host ainda está no estado 6 aguardando outro humano, continue recebendo a IA. Não finalizar tudo apenas porque o carro humano do host cruzou a linha.

**PALPITE de protocolo/receita final:**

1. Enviar snapshot final confiável com ACK/retransmissão, geração e progressos/posições de **todos** os participantes, inclusive tempo de chegada, voltas restantes, flags de desistência/preso, pontuação e métrica de ordenação. Usar tempos do relógio de corrida do host; não trocar por instantes de chegada de pacotes.
2. Instalar esses dados no parceiro **antes** do último evento humano que entra no estado 7 e calcula dinheiro/resultado. Para cada IA finalizada, `+4=1`, `+0x18=0`, `+0x10=tempoHost`; transportar também bitmap/contagem de CP finais coerentes para não disparar de novo o handler 0x1DF1C0. Fixar `race+0x1254` ao total confirmado de IAs chegadas, na classe comum; não incrementar novamente por pacote. Preservar DNF como DNF, não inventar tempo de chegada.
3. A classificação local em 0x1CF440 usa humanos e IA. Tempos/métricas/flags iguais com remapeamento correto são necessários para reproduzir a ordem. Persistir só posições não impede recálculo. Reaplicar `+0x14` depois do ranking pode corrigir o painel, mas não desfaz pagamento/resultado já processado.
4. Para concluir humanos por dados, há o mecanismo de desistência por timer já mapeado em RE-CORRIDA: `race+0x6F4` e timer humano `+0x0C<=0` fazem a rotina nativa percorrer o evento de fim. Isso muda o resultado para desistência e **não equivale a importar qualquer chegada do host**. Exige HUD válido do slot, estado de jogo correto e instalação prévia dos dados finais; seguir os guardas do mapa. Não escrever diretamente o estado da máquina para 7: isso pula EnterState e seus efeitos.
5. A escrita dos campos da IA não reproduz automaticamente o callback específico `race->vt+0x68` nem estatísticas/efeitos de modalidade. Tipos de eliminação, pontos, perseguição, tag e outros finais coletivos precisam de validação separada; não aplicar esta receita comum como universal.
6. Depois do resultado local efetivamente processado, manter a tabela final do host para apresentação até sair da corrida; não escrever ponteiros antigos durante teardown/recompensa/autosave. Resultados de carreira/recompensas do parceiro continuam sujeitos às condições de RE-CORRIDA; equalizar IA não equaliza automaticamente carreira/prêmio.

**Conclusão operacional:** o mapa de leitura e os escritores concorrentes estão identificados. Ainda não há receita só de dados **comprovada** para neutralização por carro + colocação física + finalização arbitrária sem interferência. Implementar essa promessa agora exigiria assumir comportamentos não demonstrados; os pontos acima tornam as hipóteses verificáveis sem apresentar um throttle zero como solução pronta.

## Fontes e reprodução

Consulta por buscas delimitadas nos comentários asm e no índice existente `C:\dev\mc3-coop\out\xref.json`, sem ler arquivos recompilados inteiros. Exemplos autorizados: `python tools\xref.py asm 0x1ED2F0`, `asm 0x1E4840`, `asm 0x1FC950`, `asm 0x1DF1C0`, `asm 0x1CF440`, `refs 0x534074` a partir de `C:\dev\mc3-coop` (somente leitura).

Localização de funções conferida com `rg`:

- `C:\dev\mc3-coop\generated\game\x86_recomp.54.cpp`: OppMgr::Init 0x1E43C0 e chegada IA 0x1DF1C0.
- `...\x86_recomp.55.cpp`: construção 0x1ED2F0 e atualização 0x1EE1A0.
- `...\x86_recomp.57.cpp`: input IA 0x1FBC50 / 0x1FC950.
- `...\x86_recomp.83.cpp`: lista de corredores 0x2C8710.
- `...\x86_recomp.52.cpp`: loader 0x1D3AE0 e ranking 0x1CF440.
- `...\x86_recomp.51.cpp`: nome IA 0x1CA410.
- `...\x86_recomp.78.cpp`: cópia de pose 0x2A38A0.
- `...\x86_recomp.98.cpp`: setters de velocidade/momento 0x3432B0 / 0x343300.
- Mapas de apoio: `C:\dev\mc3-coop\docs\RE-MAPA.txt`, `RE-REMOTO.txt`, `RE-CORRIDA.txt`; correções VERIFY dos mapas têm precedência sobre os resumos antigos (inclusive modo carreira e separação vehicle/car).
