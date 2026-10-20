# Troca integral sem recarga: decisão de arquitetura

19/10/2026. Proposta para revisão, **não implementada nem autorizada para execução**. O objetivo é substituir somente o veículo remoto, preservando mundo, corredor, progresso e jogador local.

**Atualização de autorização:** o usuário respondeu “Investigar a ponte nativa”. A investigação dessa alternativa está autorizada; a suspensão dos testes ao vivo permanece. A pergunta de arquitetura abaixo está resolvida. Não pedir novamente autorização para a mesma investigação.

## Impedimento da abordagem atual

O mod atual escreve dados no gancho XInput. Ainda não foi identificado consumidor nativo por slot que transforme uma configuração diferente em novo veículo durante o mundo carregado. A atualização de customização, os resets virtuais e o pacote nativo de configuração de carro não fornecem esse consumidor. Evidência nos documentos `RE-TROCA-CARRO-FACTORY.md`, `RE-TROCA-CARRO-RECURSOS.md` e nas auditorias posteriores de virtuais/rede.

A fábrica humana `2CBC40` constrói o conjunto de componentes, não apenas a aparência. Não basta trocar TypeName nem reutilizar um mesh. O novo carro pode não existir em nenhum pool local. A recarga da categoria3 cria os componentes, mas também destrói/recria jogadores, corrida, adversários e tráfego; não satisfaz este objetivo.

O handoff vigente determina: “não chamar funções guest”. A alternativa abaixo amplia essa restrição; não é uma alteração de dados que já esteja autorizada, nem exige editar as instruções do jogo. Não se presume que ela funcione só por permitir chamadas.

## Alternativa concreta para investigar

Uma ponte para executar as rotinas nativas de construção e destruição na thread do jogo, com suporte do runner ou ABI comprovada para a versão utilizada. Sem patch em instruções, vtables ou tabelas de despacho. O mapa antigo da seção native-abi em `RE-CORRIDA.txt` é de outra versão e não pode ser ativado por endereços fixos na versão atual.

Etapas necessárias, nesta ordem:

1. Provar resolução e ABI na versão atual: identificação exata do build, contexto do gancho, estado completo de registradores/FPU/SSE, pilha guest, retorno, recursão e comportamento de suspensão por IO. A fábrica carrega recursos; ainda não está demonstrado que possa ser chamada de dentro do poll de input sem suspender/reentrar de forma problemática. Se isso exigir API do runner indisponível, a alternativa também fica bloqueada.
2. Mapear uma transação por slot: construção da nova fábrica/configuração, alocação e registro do veículo novo, conclusão dos recursos, publicação das referências e descarte do antigo. `2CB270` prepara a fábrica e `2CBC40` constrói; **esses dois endereços sozinhos não são uma receita de troca**. Falta comprovar o ciclo de attachment e ownership completo.
3. Preservar jogador e progresso: mapear todas as referências a vehicle/car/inst/body nos sistemas de corrida, física, áudio, efeitos e gerenciamento. Manter o objeto antigo válido até a publicação completa, preservar pose/velocidade e impedir descarte com consumidores ainda vinculados. Sem copiar ponteiros do outro processo.
4. Implementar fila com revisão da configuração e identidade do mundo/jogador. Cancelar antes da publicação se ocorrer recarga/desconexão ou pedido mais novo. Limitar inicialmente ao remoto em ronda, sem corrida nem garagem. Erro deve manter o veículo antigo; não prometer rollback depois de destruição parcial.
5. Testar protocolo e fila offline. Para comprovar funcionamento, serão necessários ensaios no jogo: modelos diferentes, carro/moto, trocas repetidas, colisão, áudio, conexão e transições. A suspensão de testes ao vivo/firewall permanece; este plano não a revoga.

**Vida útil da configuração:** apenas o descritor da factory pode ser temporário. O wrapper conserva `vehicle+64 = CarCfg*`; colocar a configuração na pilha guest e devolvê-la ao retornar do gancho criaria ponteiro pendurado. Usar NEXT como armazenamento tampouco basta sem provar que ele não será reaproveitado pela próxima transição. Ainda falta demonstrar propriedade/alocação e descarte da configuração persistente ou um rebind seguro para a configuração owned pelo setup. Ler o produto em factory+44 contorna o retorno EAX restaurado, mas não resolve essa vida útil.

## Critério de entrega

Trocar o modelo no parceiro e observá-lo na outra cópia sem transição de mundo, mantendo a mesma identidade de jogador/corrida, física e colisão consistentes e sem vazamento acumulado em trocas repetidas. Só depois ampliar de ronda para corrida. Pintura/peças do mesmo modelo não contam como prova de troca integral.

Não foi alterado runtime para simular sucesso. O novo modelo continua pendente no caminho atual até uma carga nativa normal. O usuário escolheu investigar a ponte nativa acima; essa autorização permite pesquisar sua implementação, sem comprovar a viabilidade.

## Primeira ferramenta da investigação autorizada

`python tools/inspect_native_bridge.py <runner.exe>` inspeciona bytes do PE, sem carregar DLL/EXE, executar código ou acessar processos. Localiza a tabela imutável de funções com seeds exatas, valida cada entrada e count, exige destino executável e tabela única. Imprime SHA256, timestamp, RVAs e prefixos de alvos. `abi_validated` e `native_calls_enabled` permanecem false: localizar a tabela não resolve ABI/fibers/IO nem ownership.

Testes: `python -m unittest discover -s tools/tests -p test_native_bridge.py -v` passou9 casos com PE sintético, incluindo tabela ambígua/corrompida, targets não executáveis, count errado, duplicatas, truncamento e caches mutáveis. Inspeção dos dois arquivos de runner locais passou: atual timestamp6AD54432, SHA25651354a042caf24ffc73a989a2beb93a867401b6f2f4b61c0a5d0d2565696be8d, tabelaRVA2416820; antigo6AD2B76C, tabelaRVA2577870. Ambos têm20302 entradas. Saídas privadas em `out/native-bridge-current.json` e `out/native-bridge-old.json`.

## Atualização 19/10: primeiro item da etapa 1 (contexto/mutex)

O inspetor estático foi ampliado (`tools/inspect_native_bridge.py`) e confirmou, na build `6AD54432`, que o caminho direto do input (`300D00` → `3068D0`/`306BB0`) não referencia nem chama a aquisição do mutex do callback (`3105B0`), assim como 13 ancestrais diretos. O callback só adquire esse mutex quando invocado pelo wrapper. Isso elimina a hipótese de deadlock por mutex já detido **nesse caminho direto**, mas não prova reentrância do callback durante o HLE, não cobre chamadas indiretas, exceção/longjmp, stack scratch, CarCfg persistente, buckets ou perfil. `execution_safe` permanece false.

**Ainda bloqueiam um executor:** semântica de reentrância do callback `31B690` com `84C00` ativo; margem e vida útil do scratch para `2CB270`/`2CBC40`; capacidade dos buckets do manager de veículos; compatibilidade de perfil de habilidade entre os dois modelos; rollback de construção parcial; validação ao vivo isolada. Nada disso foi implementado nem testado no jogo nesta etapa.

## Resultado final 20/10: bloqueio do release e caminho suportado

A etapa 1 foi validada ao vivo: o wrapper full-save chamou getters guest no gancho com `mismatch=0` e sem deadlock. A etapa de construção foi exercitada com `2CB270`/`2CBC40`; `105B80` não obteve carData e `105AA0` registrou a limitação do próprio binário release: `Cannot make car resource on RELEASE ... not in the assets!`. Com produto nulo, `2CBC40` continua e o runner encerra em ICALL não resolvido. O pool só tinha o modelo em uso no modo 0; os demais estavam no modo garagem/showroom e não são reutilizados pelo modo offline.

**Conclusão:** trocar o modelo em um mundo já carregado é impossível nesta build release, sem recarregar o mundo. Não é uma limitação da ponte (a ABI funciona), mas da ausência dos assets/criador de recurso de carro no release.

**O que funciona sem reconectar:** a configuração diferente fica em NEXT e é aplicada no próximo carregamento normal (corrida/garagem). Testado com Eclipse: o host o viu na vaga 1 depois de carregar a corrida, com a sessão ainda conectada. O código final recusa a construção no mundo atual e preserva o veículo antigo.
