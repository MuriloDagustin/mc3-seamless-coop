# Pendências menores — carro remoto e caminhões

Análise estática de 18/10/2026 em `C:\dev\mc3-coop`. Esta subtarefa não executou jogo, runner, testes em memória de processos ou comandos de encerramento. **As propostas deste documento não foram implementadas nem validadas no jogo.**

VAs são do Xbox, com host = base + VA. CONFIRMADO indica evidência estática em comentários asm ou dados do XBE; PALPITE indica efeito ou integração que exige ensaio.

## 1. Carro remoto sem recarregar

### Causa do comportamento atual

**CONFIRMADO no mod:** `SetPartnerCar` guarda o CarCfg recebido; `WriteSlot` copia seus bytes `+4..+0x107` para o CarCfg pertencente ao setup. Essa cópia inclui `cfg+8`, a máscara de alterações. `Tick` move o veículo já construído e não reconstrói seu modelo.

**CONFIRMADO no jogo:** existe aplicação incremental de customização durante a atualização normal. Não é correto afirmar que toda alteração do CarCfg só pode aparecer após carregar outro mundo:

```asm
000FF021 mov ecx, [ecx+0x64]      ; vehicle+64 = CarCfg*
000FF024 push ecx
000FF025 mov ecx, esi             ; objeto de customização/desenho
000FF027 call 0x000F96C0

000F96D7 mov ecx, [ecx+0x2F0]
000F96DD push edx                 ; índice do jogador
000F96DE push 0x00                ; forceAll=false
000F96E0 push eax                 ; CarCfg*
000F96E1 call 0x000F32A0
```

`0xFEF80` percorre os veículos e copia também a matriz de phInst para o modelo antes desse trecho. O construtor da fábrica humana liga a configuração ao veículo:

```asm
002CBE2B mov ecx, [edi+0x54]      ; configuração da fábrica
002CBE30 mov [esi+0x64], ecx      ; configuração observada pelo veículo
```

O caminho incremental usa bits de alteração; não compara o conteúdo integral do CarCfg:

```asm
000F32B2 mov eax, 0x3FFF
000F32B7 jnz 0x000F32BC           ; forceAll=true
000F32B9 mov eax, [edi+0x08]      ; caso incremental: máscara do cfg
000F32BC and eax, 0x3F7F
000F32C7 jz 0x000F431D           ; nenhum bit: nada a aplicar
...
000F4308 mov eax, [edi+0x08]
000F430F and eax, 0xFFFFC080      ; consome os bits aplicáveis
000F4314 mov [edi+0x08], eax
```

O CopyFrom nativo marca novamente as alterações, algo que o memcpy do mod não reproduz:

```asm
00271E82 mov dword ptr [ebx+0x08], 0x3FFF
```

**Consequência confirmada:** se a configuração recebida tiver `+8=0`, copiar seus dados não aciona esse aplicador incremental. **PALPITE condicionado:** esse é um motivo para pintura/peças não atualizarem ao vivo; confirmar a máscara efetivamente recebida no teste. O sintoma geral também inclui mudanças de modelo, que possuem outra limitação.

### Proposta mínima para o mesmo modelo — não implementada

1. Comparar a configuração recebida com a última configuração instalada, ignorando a máscara transitória `+8` na identidade da mudança.
2. Validar `vehicle+0x64`, o CarCfg do setup e a identidade do veículo; usar somente ponteiros locais e preservar a vtable do CarCfg.
3. Se o TypeName for o mesmo, copiar os dados atualizados uma vez e marcar `cfg+8 |= 0x3F7F`. Evitar copiar o payload inteiro a cada frame, porque isso pode repor uma máscara limpa antes de o jogo consumi-la ou forçar trabalho de customização repetido.
4. Deixar a atualização normal do jogo consumir os bits. O mod não precisa chamar funções guest para esse ensaio.

**Limite:** a aplicação de bits foi confirmada, mas ainda não foi provado que todos os níveis de desempenho e todos os tipos de peça possam mudar com segurança durante qualquer estado de corrida. Começar por pintura e peça visual válida para o mesmo modelo. Não misturar configuração de outro modelo com o objeto antigo.

### Modelo diferente — ainda sem receita segura só de dados

TypeName ocupa `CarCfg+0xE3`, com comprimento de 0x16 bytes. `0x272F70` copia o nome obtido na tabela de veículos para esse campo; `0x270AE0` resolve esse nome para índice. A construção do carro envolve alocação e recursos:

```asm
002CBC6C push 0x3138
002CBC75 call 0x001B3B90         ; alocação de mcCarCustom
002CBC9B call 0x000F4430         ; construtor
002CBCBA call 0x000F32A0         ; aplicação inicial da configuração
```

**CONFIRMADO:** o aplicador incremental mascara `0x3F7F`, excluindo o bit `0x80`. Marcar simplesmente `0x3FFF` não faz esse aplicador trocar o modelo básico. `0x20260` compara os índices de modelo dos setups em `0x20543..5D` e pode solicitar reconstrução de recursos; não demonstra uma troca isolada do carro remoto sem recarregar o mundo.

**Não resolvido:** um pedido por dados para o jogo reconstruir somente esse veículo, mantendo corretos modelo, áudio, corpo, collider, registros e referências reversas. Trocar apenas ponteiros de modelo/carro por objetos de garagem ou copiar memória de outro processo não é uma solução demonstrada. Até achar esse caminho, a mudança de TypeName deve permanecer pendente para a próxima carga normal; não anunciar troca de modelo ao vivo como concluída.

### Testes propostos

- Registrar configuração recebida, máscara `cfg+8`, TypeName e identidade do veículo antes/depois.
- Alterar somente pintura, depois uma peça visual válida, com o modelo mantido. Conferir que o jogo consome a máscara e que o objeto do veículo permanece o mesmo.
- Repetir recebimento do mesmo payload: não reiniciar aplicação continuamente.
- Receber um segundo TypeName: confirmar que a proposta não aplica suas peças incompatíveis ao modelo antigo e que a próxima carga constrói o modelo correto.
- Repetir em cruise, corrida e desconexão. Avaliar desempenho e estabilidade antes de incluir alterações de motor/transmissão.

## 2. Caminhões com reboque

### Limitação atual

**CONFIRMADO no mod:** `traffic::Tick` exclui slots com `slot+0xEC != 0` antes de `Apply`. O formato de snapshot atual não contém a pose articulada do reboque. Portanto esses caminhões continuam locais; remover apenas esse teste não implementa sua sincronização.

**CONFIRMADO no jogo:** `+0xEC` é um ponteiro de objeto, não um interruptor. O init verifica tipo6 e cria o reboque de 0x174 bytes:

```asm
00071B4D cmp dword ptr [ebx+0x294], 0x06
00071B55 jnz 0x00071B89
00071B57 push 0x174
00071B5C call 0x001B3B90
...
00071B6A call 0x000819D0
00071B7B mov [esi+0xEC], eax
00071B81 movss [eax+0xA8], xmm0
```

Não zerar `+0xEC`: os caminhos de atualização/desenho usam esse objeto e ele tem seu próprio ciclo de vida.

### Pose e atualização articulada

**CONFIRMADO:** a matriz do reboque fica em `[slot+0xEC]+0x13C..0x168`, com linhas de orientação em `+0x13C`, `+0x148`, `+0x154` e posição/engate em `+0x160`. A rotina `0x76980` lê e reescreve esses campos a partir do movimento do cavalo:

```asm
00076980 mov edx, [ecx+0xEC]
00076989 test edx, edx
0007698B jz 0x00076C27
...
00076BFB movss [edx+0x150], xmm0
00076C03 mov eax, [ecx+0x58]
00076C06 mov [edx+0x160], eax
00076C0C mov eax, [ecx+0x5C]
00076C0F mov [edx+0x164], eax
00076C15 mov eax, [ecx+0x60]
00076C18 mov [edx+0x168], eax
00076C22 jmp 0x00072B40
```

`0x72B40` combina a matriz do cavalo com a do reboque e escreve as matrizes das submalhas. Copiar apenas a matriz principal em `slot+0x34` deixa a articulação local com sua própria história.

### Por que remover links de estrada não basta

O tratamento atual de carros comuns remove `+0xFC/+0x100`. Para caminhões, o despawn permanece habilitado pelo objeto de reboque:

```asm
0006A5C4 mov eax, [ecx+0xFC]
0006A5CA test eax, eax
0006A5CC jnz 0x0006A5E6
0006A5CE cmp dword ptr [ecx+0xC4], 0x01
0006A5D5 jz 0x0006A5E6
0006A5D7 mov eax, [ecx+0xEC]
0006A5DD test eax, eax
0006A5DF jnz 0x0006A5E6
0006A5E1 xor al, al
0006A5E3 ret 0x04
0006A5E6 call 0x00079AB0
```

`0x80C10` examina o ponto frontal contra os focos locais e pode solicitar esse despawn. Em `0x79AB0`, o jogo pode retirar o objeto do renderer e devolver o slot ao pool. Logo o mesmo caminhão pode desaparecer no parceiro mesmo com um snapshot recente do host.

### Candidato para o ensaio — estado scriptado C4

**CONFIRMADO nos ramos examinados:** `slot+0xC4==1` impede o despawn por foco e faz o despawn comum seguir um ramo que preserva o objeto:

```asm
00080C33 cmp dword ptr [esi+0xC4], 0x01
00080C3A jz 0x00080CF7           ; não despawn por foco

00079AC5 cmp dword ptr [esi+0xC4], 0x01
00079AD2 jz 0x00079B94           ; não remove renderer/devolve pool nesse ramo
```

A atualização normal possui caminho que continua propagando pose, reboque e localização no nível físico:

```asm
00078AA9 call 0x000767B0         ; matriz do cavalo a partir da âncora/direção
00078AB0 call 0x00076980         ; articulação do reboque
00078AB5 mov si, [esi+0x2C]
00078AB9 cmp si, 0xFFFF
00078ABE jz 0x00078ACF
00078AC0 mov ecx, [0x005580F0]
00078AC6 mov edx, [ecx]
00078ACC call [edx+0x20]         ; UpdateObjectLocation
```

**PALPITE de integração, não implementado:** usar a receita de estado scriptado terminado já descrita em `RE-TRAFEGO.txt` (`+0xC4=1`, `+0xAC=0`, âncora `+0xC8`, direção `+0xD4`, velocidade positiva mínima para entrar no caminho de atualização), conservando o objeto do reboque. Isso pode preservar registro físico e impedir reciclagem melhor que o caminho de links zerados. Ainda faltam ensaios da ordem exata do gancho, restauração e colisões; não tratar a receita como validada para caminhões.

Para autoridade do host, transmitir a pose articulada junto da identidade do slot/tipo/variante e instalar somente em objeto local compatível. A rotina nativa `0x76980` pode reescrever a articulação depois do gancho; medir esse erro antes de decidir se a cópia do reboque é suficiente ou exige outra fase de aplicação.

**Limites do protocolo:** acrescentar 48 bytes a todos os registros ultrapassa o tamanho atual de datagrama com 34 carros por pacote. Reduzir a quantidade por pacote ou criar mensagem específica de caminhões. Não enviar o ponteiro `+0xEC`. A variante `+0xB5` escolhe a geometria de colisão no init; validar a mesma variante nos dois lados, sem apenas sobrescrever esse byte em execução.

Esconder caminhões é alternativa prevista no item4, mas também precisa tratar cavalo e reboque nos dois lados e confirmar a retirada efetiva das colisões. Esconder só a matriz do cavalo não demonstra que o reboque e a grade física acompanharam.

### Plano de teste C4/reboque

1. Identificar um slot tipo6 com render handle válido, reboque mapeado e variante igual nas duas cópias. Registrar âncora, matrizes e campos de posse antes de qualquer escrita.
2. Em cópias descartáveis e isoladas, ensaiar `C4=1/AC=0` inicialmente com o caminhão parado; confirmar cavalo, reboque, colisão e contadores do pool.
3. Sincronizar percurso em reta e curva. Medir erro de pose do cavalo e das três linhas/posição do reboque separadamente, inclusive antes/depois do tick nativo.
4. Afastar os jogadores para testar foco/despawn e retorno. Verificar que o handle continua coerente e que não se reutiliza a posse de uma instância antiga.
5. Testar contato com os dois corpos. Ao aparecer `slot+0x13C` de física de acidente, não presumir que o caminho leve ainda comanda o caminhão.
6. Testar pacote ausente, desconexão, saída da corrida e recarga de cidade; restaurar campos somente no mesmo objeto/mundo, sem gravar ponteiros antigos. Conferir que o tráfego volta ao ciclo normal sem caminhões presos nem vazamento de slots.

## Fontes

Busca delimitada em `generated/game/x86_recomp.*.cpp` e consultas `tools/xref.py asm/callers` ao índice `out/xref.json`. Funções centrais: `0xFEF80`, `0xF96C0`, `0xF32A0`, `0x271E20`, `0x272F70`, `0x270AE0`, `0x2CBC40`, `0x2CBDE0`, `0x20260`, `0x71AC0`, `0x76980`, `0x72B40`, `0x78940`, `0x6A5C0`, `0x79AB0`, `0x80C10`. Mapas de apoio: `RE-MAPA.txt`, `RE-REMOTO.txt`, `RE-TRAFEGO.txt`. Nenhum arquivo recompilado completo foi lido.
