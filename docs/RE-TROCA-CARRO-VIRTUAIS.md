# Troca do modelo remoto: virtuais, rede nativa e seleção de veículo

Auditoria estática de2026-10-19 em `C:\dev\mc3-coop`, complementar a `RE-TROCA-CARRO-FACTORY.md` e `RE-TROCA-CARRO-RECURSOS.md`. Nenhum jogo, launcher, processo, firewall ou função guest foi executado. Somente este documento foi editado nesta investigação. VAs são do Xbox e as vtables foram lidas do `assets/default.xbe` como dados; o índice e os comentários de asm vieram de `tools/xref.py`/`out/xref.json`.

**Resultado:** o despacho virtual do jogador e do wrapper não contém um método oculto de substituição de modelo. A rede nativa atualiza o veículo existente; sua troca de jogador observado também exige veículo já construído. A seleção de carro possui dados persistentes reais, porém o seu bit de dirty indica save, e a opção nativa RestartNewCar termina em preparação de NEXT e transição global de carga. Nenhum desses caminhos fornece uma transação individual de hot swap acionada somente por dados.

Isso fecha os caminhos específicos abaixo; não é prova de inexistência de qualquer mecanismo em todo o jogo. Não implementar `rebuild=1`, pointer swap, escrita de vtable ou chamada a um VA com base nesta auditoria.

## 1. Todas as entradas virtuais do wrapper humano

**CONFIRMADO:** `FF160` constrói o wrapper de6C bytes e instala `454F40`. Sua tabela contém quatro métodos; os bytes seguintes já são dados, não outros ponteiros de código.

| Slot | Destino | Trabalho observado |
|---|---|---|
|0|100D70|destrutor com liberação opcional; chama FFBA0|
|4|FFC50|reset dos componentes já existentes e parâmetros de física|
|8|FFD90|update/recuperação/reset e consumo de dirty80|
|C|FFA70|encaminhamento do callback de colisão a componentes|

`FFC50` lê `vehicle+64` (CarCfg), calcula um valor a partir de cfg+E1 e chama `103A50`; no caso E1==FF chama `103C50`. Em seguida atualiza componentes existentes, chama resets virtuais e rotinas físicas. Não aloca uma factory, não lê TypeName para abrir um recurso e não publica outro wrapper em `player+14`.

`FFD90`, inclusive seus fragmentos até `1007EF`, não é um loader de modelo. O ramo dirty80 é explícito:

```asm
00100654 mov eax,[vehicle+64]
00100657 mov cl,[eax+08]
0010065A test cl,cl
0010065C jns 00100741           ; sem bit80: pula
00100662 movsx ecx,byte [eax+E1]
00100689 mov ebx,[eax+08]
00100690 and ebx,FFFFFF7F
00100697 mov [eax+08],ebx       ; consome80
0010069A mov ecx,[vehicle+4C]
0010069E call 00103A50
001006A6 cmp byte [cfg+E1],FF
001006B4 call 00103C50          ; só no casoFF
001006CA call 00121970
001006DB call 000FF220
0010073C call 00105450
```

**CONFIRMADO:** bit80 tem consumidor real, mas esse consumidor não é a reconstrução de modelo procurada. `vehicle+5C` também não é um pedido: o update o limpa em `FFDA0` e o marca durante as fases de reset em `FFEDE`/`1002CF`. Os caminhos de recuperação manipulam o mesmo car/body/componentes.

O destrutor `FFBA0` libera diversos componentes e remove o wrapper do sistema obtido por `3308C0`, depois chama a destruição base. Ele não agenda substituto. Forçar a execução desse destrutor por dados não cria um pedido de reconstrução e deixa referências pendentes.

## 2. Virtuais do jogador e ownership de player+14

**CONFIRMADO:** `2BFE50` instala a vtable humana `476498`, cujas oito entradas são:

| Slot | Destino | Uso |
|---|---|---|
|0|2C1BB0|destrutor com liberação opcional|
|4|1EC420|retorno vazio|
|8|2C8500|ponteiro para velocidade do corpo do veículo existente|
|C|2C8530|consulta física do veículo existente|
|10|2C8510|consulta física do veículo existente|
|14/18|131100|retorno sem reconstrução|
|1C|2C19B0|reset/reposicionamento do jogador/veículo existente|

`2C8500` segue `[player+14]→[vehicle+8]→[car+68]→[objeto+10]` e retorna esse último ponteiro+44. Os outros getters consultam `A2450`. Nenhum deles recebe uma nova configuração e constrói um veículo.

O método1C chama `2C8690`, que usa o wrapper existente:

```asm
002C8693 mov eax,[player+14]
002C8696 test eax,eax
002C869A call 002C8610          ; posicionamento
002C869F mov ecx,[player+14]
002C86A2 mov eax,[ecx]
002C86A4 call [eax+04]          ; FFC50 reset
```

`2C19B0` ainda restaura transform/rumo a partir de career data quando é cruise na mesma cidade e reinicializa câmera/input/efeitos. Não chama `2CB270`, `2CBC40`, `F4430` ou outra factory para substituir o veículo.

A base do jogador é construída por `2C8420`, que zera+14; o humano grava o produto da factory em `2C005E`. A destruição base `2C8480` mostra por que a propriedade de+14 não é intercambiável:

```asm
002C849A mov ecx,[player+14]
002C84A3 mov eax,[ecx]
002C84A5 mov edi,[ecx+68]       ; guarda custom
002C84A8 push 1
002C84AA call [eax]            ; destrói wrapper
002C84AC mov ecx,[player+10]
002C84B6 call 001057B0          ; remove dados do manager de modelos
002C84BF mov edx,[custom]
002C84C5 call [edx]            ; destrói custom separado
```

Logo, mesmo conhecer todos os quatro métodos do wrapper não fornece uma operação de adoção/publicação de outro veículo. Um substituto teria de ajustar também car data, custom, registros de física/modelos e referências de input/áudio/câmera.

## 3. A rede nativa não materializa um modelo novo no update

**CONFIRMADO:** `2C1520` desvia para `2C0540` no caminho de jogador remoto da rede nativa. `2C0540` consulta `557EFC` por método virtual+18, usa o estado de rede e player+BC, calcula correções e escreve no corpo do **mesmo** player+14. Por exemplo:

```asm
002C055A mov ecx,[557EFC]
002C0564 call [eax+18]
002C0567 mov ebp,[eax+60]       ; parâmetros da correção de rede
002C056A mov al,[player+BC]
002C0578 mov ecx,[player+14]
; ...interpolação/limiares de transform/velocidade...
002C0BC9 mov eax,[player+14]
002C0BCC mov ecx,[eax+08]
002C0BCF mov ecx,[ecx+68]
002C0BD7 call 002A35A0          ; corrige transform
002C0BE4 mov eax,[player+14]
002C0BE7 mov ecx,[eax+08]
002C0BEF mov edx,[ecx+68]
002C0BF2 mov ecx,[edx+10]
002C0BF5 call 003432B0          ; corrige corpo/velocidade
```

Não há consulta de TypeName/CarCfg, factory ou publicação de novo player+14 nessa rotina. O byte+BC habilita uso do estado recebido; não solicita um carro novo.

Outro candidato examinado foi `2BDEC0`, atualização do manager de humanos. No ramo de rede, se o participante observado deixa a máscara de jogadores, a busca `2BDF53..2BDF91` só escolhe outro slot com ponteiro de jogador **e player+14 não zero**. Chama então `2C3FF0`:

```asm
002BDF76 cmp dword [candidatePlayer+14],0
002BDF7A jnz 002BDF86
002BDF86 mov [manager+04],index
002BDF91 call 002C3FF0
002C3FF7 mov [camera+58],index
002C4001 call 002BD910
002C4006 mov [camera+04],eax     ; seleciona jogador observado
```

Portanto esse caminho troca a observação/câmera, não a construção do veículo. Zerar slot ou+14 não passa a criar um jogador tardio.

O próprio ctor humano recebe a decisão de não construir veículo de um byte de setup: `269430` lê `CURRENT+13A+i*2C`, repassado por `2BE100` ao último argumento de `2BFE50`; o ctor usa esse argumento para pular a construção. Isso é seleção **no ctor**, não manutenção posterior por frame.

## 4. ChangeVehicle: dados reais de seleção, mas sem pedido de hot swap

**CONFIRMADO:** a string `CM_ChangeVehicle`, VA45E3C8, é ligada ao callback genérico de menu `163830` com argumento0B, tanto em `16B531..16B56E` como em `16C8BD..16C8FA`. A tabela de `1628C0` para esse argumento leva a `162C5D`, que abre o menu armazenado em `frontend+30` por método virtual+D4. Isso é transição de UI, sem publicação em player+14. Não tratar a string como nome de uma função de troca física.

A seleção no menu `18BA00` chama `155280`. Existem campos persistentes reais:

```text
53F2FC+i         byte: origem/coleção do carro selecionado
53F2FE+i*2      WORD: índice escolhido
53F80D          byte: índice do carro da coleção local
557648          byte: dirty de perfil/save
```

`1550B0` grava a origem/índice em `15516D/155177`; `155280` também altera esses índices e marca557648. **O significado desse último byte é demonstrado por seu consumidor**, não pelo nome da UI:

```asm
000197F0 mov al,[557648]
000197F7 jnz 00019808
000197F9 mov eax,[557F0C]
000197FE mov cl,[eax+20]        ; dirty da carreira
00019803 jnz 00019808
00019805 xor eax,eax
00019808 mov eax,1
```

Os callers diretos de197F0 são `16D4A0` e a entrada de autosave `1C33E0`. Esse campo diz que há dados a salvar. Não existe evidência de que seja um rebuild request de veículo.

`155920` usa a seleção e copia o CarCfg para o destino fornecido ou `NEXT+120+slot*2C`. `268E50` percorre as vagas e também copia a configuração selecionada para o setup. Novamente, publicar cfg no setup não prova a reconstrução de player+14 antes de uma carga.

Um caminho de retorno de personalização, `18FBF0`, copia a configuração atual para a coleção local e para NEXT, marca dirty/save e fecha UI por `161C30`. Ele não substitui wrapper. Não extrapolar a mudança de aparência compatível do carro atual para uma troca de TypeName.

## 5. RestartNewCar confirma a transição global de corrida

**CONFIRMADO:** `CM_RestartNewCar` em4629F8 é ligado a `1880B0` em `18BE7E..18BEB5`. Esse callback prepara NEXT por `132B90`, ajusta clima/hora, adversários, flags e pede estados globais8 e6:

```asm
001880D1 mov byte [50A1B3],1
001880D8 call 00132B90          ; prepara NEXT e configura modo/setup
0018811E mov ecx,[taskData+18]
00188121 call 0028DEB0          ; prepara adversários para o setup
00188163 mov eax,[gameSM]
00188165 push 8
00188167 call [eax+04]
0018816A mov ecx,[557ED0]
00188170 mov edx,[ecx]
00188172 push 6
00188174 call [edx+04]
```

O caminho normal de carga por `21170` chama `20260(true)` antes de continuar. O comparador de setups e loader de categoria3 já documentados então podem destruir/recriar humanos, OppMgr, lista de corredores e corrida. O load faz `294F6→2BE2C0` e `2952E..29534→1C13D0`, ou seja, reconstrói o conjunto e inicializa a corrida. Isso não preserva a corrida em execução como uma troca individual.

**Limite:** o que acontece com recursos de cenário depende das diferenças entre os setups e das categorias que o comparador decide descarregar. Não foi provado que cada mudança de carro reabra o mapa inteiro, mas foi provado que esse caminho não se limita ao wrapper remoto e reinicializa gameplay/corrida. Não oferecê-lo como hot swap sem recarga da sessão.

## 6. Hipótese concreta para uma alternativa arquitetural

**PALPITE de implementação futura, sem execução nesta etapa:** uma bridge autorizada que consiga executar uma transação nativa na thread guest poderia criar o produto pela factory correta, registrar componentes e só então publicar o veículo no jogador, com teardown do produto antigo e correção das referências. O ponto de construção confirmado continua sendo `2CB270→2CBC40`; ownership e destruição precisam respeitar os vínculos demonstrados por `2C8480`/`FFBA0`, não só trocar player+14. Primeiro seria necessário verificar ABI/callback guest e suporte do runner atual; VAs guest não são funções host chamáveis por `base+VA`.

Esta hipótese **amplia** o mecanismo atual de leitura/escrita de dados e depende de decisão do usuário e de uma prova técnica própria. Documentos antigos de native ABI não são executáveis nem validação da build atual. Nenhuma bridge ou chamada foi implementada aqui.

Sob a restrição atual, o que permanece sustentado é atualizar configurações compatíveis do mesmo modelo e preparar um TypeName diferente para a próxima construção nativa. O objetivo de trocar o modelo remoto imediatamente, preservando o mundo/corrida, segue sem uma receita por dados demonstrada após esta auditoria adicional.
