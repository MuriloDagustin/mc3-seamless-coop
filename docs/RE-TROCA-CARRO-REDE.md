# Troca integral do carro remoto: rede, factories virtuais e coleções

Análise estática em 19/10/2026, pasta principal `C:\dev\mc3-coop`. Complementa `RE-TROCA-CARRO-FACTORY.md` e `RE-TROCA-CARRO-RECURSOS.md`. Não executei jogo, runner, launcher, funções guest, rede, UAC ou firewall. Somente este documento foi criado nesta subtarefa. Evidência: asm do índice `out/xref.json`, buscas delimitadas nos recompilados e leitura de tabelas/dados de `assets/default.xbe`.

**Conclusão:** os caminhos adicionais examinados não fornecem um pedido de dados consumido durante a condução que reconstrua apenas o veículo do humano remoto. A mensagem nativa `MCNET_PLAYER_CAR_CONFIG` atualiza o setup NEXT. A factory humana tem métodos virtuais de componentes, mas o construtor completo não é um desses métodos nem um trabalho persistente encontrado numa fila. As coleções de veículos recebem produtos já construídos e atualizam seus componentes.

Isso delimita caminhos concretos; não prova inexistência em todo o binário. Não implementar `rebuild=1`, trocar ponteiros por objetos de IA, modificar filas de componentes ou assumir que alterar TypeName provoca a transação completa.

## 1. Mensagem nativa de configuração de carro: tipo44

**CONFIRMADO:** a string `MCNET_PLAYER_CAR_CONFIG` fica em4669AC. O resolvedor de nomes1AFBB0 usa a tabela1AFD10 para os tipos40..68; entrada44 aponta a1AFBE7, que retorna4669AC.

O construtor24220 monta a mensagem:

```asm
00024226 mov byte ptr [packet],44
0002422D mov word ptr [packet+02],08
0002423A call 00271D20                ; CarCfg embutido em packet+08
0002423F mov word ptr [packet+02],0114
00024245 mov byte ptr [packet+111],00
```

Campos demonstrados: tamanho0x114, CarCfg de0x108 bytes em+8, índice do slot byte+110 e flag adicional byte+111. Não presumir o significado completo do flag111 só a partir deste construtor.

**CONFIRMADO:** o dispatcher1AF470 lê o byte do tipo. A entrada44 da tabela1AF6F4 aponta para1AF509, que chama `session.vt+1A0(packet,source)`. A sessão concreta tem vtable449100 (instalada24331 e publicada em `[557EFC]` em24348); sua entrada+1A0, em4492A0, aponta para26370. A busca de referência absoluta ao alvo26370 no XBE encontrou essa entrada.

O emissor25D20 prepara a mensagem na pilha e copia o CarCfg por272010 em25D97. Em25DC2..25DEA, uma condição de destino local permite despachar diretamente no método virtual+1A0; o outro ramo envia pelo transporte. Isso é uma chamada nativa do próprio fluxo de rede, não um pedido de reconstrução por dados.

## 2. O recebedor só instala NEXT

**CONFIRMADO:**26370 verifica o tamanho0x114 em263A6..263BC. Parte do caminho do host pode redistribuir a mensagem e, se uma tela local estiver ativa (`[[[50F148]+C]+8C]`, consultada263E9..26407), devolver a configuração atual do NEXT ao remetente. Depois o destino efetivo é:

```asm
00026507 movzx eax,byte ptr [packet+110]
0002650E mov ecx,[0053F2E4]            ; NEXT, não CURRENT
00026514 imul eax,eax,2C
00026517 mov ecx,[eax+ecx+120]         ; CarCfg desse slot no NEXT
0002651E lea edx,[packet+08]
00026521 push edx
00026522 call 00272010
```

272010 é um salto para271E20, o CopyFrom nativo. Ele copia TypeName(+E3), ProfileName(+F9), campos de configuração e marca `cfg+8=3FFF` em271E82. Não constrói vehicle/car/phInst/body e não publica `player+14`.

**CONFIRMADO:** o restante de26370 encerra os CarCfg temporários em26527..26547. Não há chamada à factory humana2CBC40 ou ao construtor de jogador2BFE50 nesse recebedor. A mensagem original preserva o funcionamento “selecionar/preparar a configuração para a próxima carga”; ela não é evidência de hot swap em mundo carregado.

**Limite importante:** não importar uma mensagem para a rede nativa nem forjar a sessão/filas como alternativa. Além de não resolver a reconstrução, os índices e estados pertencem ao sistema online original, distinto do transporte do mod.

## 3. A virtualidade da factory não fornece um Build enfileirável

**CONFIRMADO:**2CB270 instala a vtable476940 da factory humana. Os16 slots são:

| Offset | Função |
|---|---|
| 00 | 2CBAB0, destrutor |
| 04 | 2CBDE0, construção do wrapper |
| 08 | 2CB320 |
| 0C | 2CB390 |
| 10 | 2CB5C0 |
| 14 | 2CB680 |
| 18 | 2CB8B0 |
| 1C | 2CB710 |
| 20 | 2CB2D0 |
| 24 | 2CB6C0 |
| 28 | 2CBE90 |
| 2C | 2CBF70 |
| 30 | 2CB820 |
| 34 | 2CB9D0 |
| 38 | 2CBA20 |
| 3C | 2CBA60 |

O build completo2CBC40 chama esses métodos na sequência, cria/aplica customização, registra o produto e o devolve. **2CBC40 não é um slot dessa vtable.** O build de frontend2CBAE0 também não é um deles. A busca de referências absolutas DWORD ao endereço2CBC40 no XBE não encontrou entrada de tabela/callback; a chamada direta conhecida é2C0059, no construtor do jogador. Esse resultado de busca não exclui endereços calculados dinamicamente, mas não há evidência de tal consumidor nos caminhos examinados.

**CONFIRMADO:** o descritor base376600 só copia o nome parafactory+4 e zera+44/+48;361B30 guarda o CarCfg em+54 e zera outros campos. Não registra um trabalho de streaming. A factory humana é construída na pilha de2BFE50, usada em2C0059, publica `player+14` em2C005E e termina antes de retornar do construtor.376630 apenas restaura a vtable base e retorna; não transforma o descritor em um pedido persistente.

Assim, localizar uma função virtual de construção de componente não demonstra um consumidor que possa ser ativado por um byte enquanto o player está rodando. Escrever uma factory falsa na RAM e instalar seu callback seria forjar contexto/execução guest, fora da restrição de somente dados.

## 4. Coleção de veículos é produto registrado, não fila de construção

**CONFIRMADO:**3308C0 retorna o global `[5638E4]`. FF0D0 o cria porFEDC0, com vtable454F04;FEDA0 destrói e zera o global. Portanto o ponteiro consultado no fim do build2CBC40 não é, por si só, um canal de pedido de rede.

2CBD67..2CBDCB obtém os componentes do produto já construído, consulta esse manager e chama seu método+4 com o produto em2CBDBD. O cadastro base35D500 começa assim:

```asm
0035D500 mov eax,[manager+04]
0035D503 mov edx,[manager+08]
0035D507 mov esi,[esp+08]              ; produto pronto
0035D50B mov [edx+eax*4],esi
0035D50F inc [manager+04]
0035D512 mov eax,[esi+08]              ; conjunto de componentes já existente
```

Ele distribui componentes em arrays/counts distintos (+0C/+10, +14/+18, etc.).35D700 remove correspondências desses arrays por35D680. Alterar um único ponteiro/count do cadastro não instala nem remove todos os vínculos.

**CONFIRMADO:**FEF80 chama o update base35DA80 e percorre coleções de componentes. O trechoFEFA0..FF030 copia a matriz do veículo existente e encaminha `vehicle+64` aF96C0, o aplicador incremental já mapeado.35DA80 percorre arrays de objetos e chama métodos de update(+8,+C,+10,+18); os loops examinados não alocam uma nova factory/player nem consomem um tipo de modelo para isso.

Esta distinção importa para streaming: uma instância visual, shader ou textura paginada não é o objeto humano completo. PageIn/dirty mask do desenho não recria corpo rígido, collider, áudio, controlador, feedback, referências de corredor nem o vínculo do player. Não foi localizado neste cadastro um trabalho com `(slot,novoCarCfg)` que complete essa transação.

## 5. Outras construções encontradas e por que não resolvem

**CONFIRMADO:** a busca de chamadores diretos dos construtores centrais encontrou:

| Construção | Chamador / destino |
|---|---|
| Humano2BFE50 | 2BE100, constrói a coleção de jogadores |
| Build humano2CBC40 | 2C0059, dentro desse construtor |
| Manager jogadores2BE2C0 | 294F6 (carga de gameplay) e150F35 (construtor de garagem) |
| Build frontend2CBAE0 | 13C969, buffers de showroom |
| Factory de IA1FA970 | 1ED31A, inicialização de driver/oponente |
| WrapperFF160 | 2CBDF4 (humano) e1FAE25 (IA) |

O chamador150F35 não é um pedido novo de manutenção de slot: faz parte de150DE0, que constrói o objeto grande da garagem, instala vtable45B860 e também cria configurações/bancos/veículos de exposição.13C8E0 publica o veículo de showroom em `menu+714+4*buffer`, não no player remoto, conforme o mapa anterior.

A factory de IA também é temporária:1ED316 prepara a pilha,1ED31A chama1FA970,1ED323 chama1FB040 e1ED328 publica o produto no driver+20.1FAE10 constrói outro wrapperFF160, usa flag de IA `vehicle+5E=1` em1FAE57, instala seu phInst e publica o produto na factory+44. Isso não fornece ownership de humano ou um caminho para reparar as referências de `player+14`.

**Não é solução:** reutilizar um carro de IA/tráfego já existente. O modelo requerido pode não existir nem ter recursos residentes nessa cópia, e tomar posse de um objeto de outro sistema não executa a transação de construção humana. Não é uma alternativa completa para TypeName arbitrário.

## 6. Impedimento delimitado e alternativa que exigiria mudar o escopo

Não foi encontrada uma receita segura apenas de memória nos caminhos nativos de rede/factory/cadastro adicionais. O mod deve continuar guardando a identidade incompatível para a próxima construção normal. Alterações compatíveis do mesmo modelo têm o consumidor incremental já implementado; isso não satisfaz troca integral de modelo.

**PALPITE de arquitetura, não implementação nem garantia:** se o runner oferecesse uma bridge explícita, seria possível investigar uma operação nativa de troca por slot. Isso exigiria muito mais que autorizar um `call` para2CBC40:

1. Resolver guest VA↔função recompilada, ABI x86/thiscall, registradores, stack/retorno e contexto de emulação. Uma chamada host C++ com ponteiro numérico guest não satisfaz isso.
2. Demonstrar ponto seguro na thread/fiber nativa, reentrância, waits/yields de IO e vida útil de temporários através das construções. O gancho de XInput não foi provado um ponto seguro para uma carga/destruição arbitrária.
3. Preparar recursos do modelo arbitrário e bancos/escopos corretos, com falha/rollback. O nome não garante que recursos estejam residentes ou que o heap da garagem seja adequado ao mundo.
4. Mapear teardown e registro do veículo humano completo: arrays do manager, phInst/collider/body/grade física, desenho/customização, áudio, controlador/feedback, player, racer, câmeras e referências inversas. Não liberar o antigo enquanto referências nativas o usam.
5. Construir o novo veículo com configuração/local slot corretos e reinstalar todos os bindings; validar preservação da corrida, pose, progresso, autoridade do mod e contabilização humana. A factory original está dentro do construtor do player, não é uma operação de substituição pronta.
6. Testar por build do runner, primeiro com memória/lifecycle instrumentados e depois em execução isolada autorizada. Permanecem desconhecidos os efeitos da construção fora da fase original de carga.

Não há evidência aqui de que o runner fechado exponha tal bridge. Não foi chamada/injetada nenhuma função e não se recomenda enfraquecer guardas atuais em troca de uma promessa de reconstrução. Caso o escopo continue somente dados, a entrega correta é informar este impedimento e preservar a configuração para a próxima recarga nativa.
