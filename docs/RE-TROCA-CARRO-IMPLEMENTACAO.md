# Configuração do carro remoto sem reconstruir o mundo

18/10/2026, principal `C:\dev\mc3-coop`, branch `coop-noite`. Continuação autorizada após integrar a worktree do tráfego. Nenhum jogo foi aberto, nenhuma chamada guest feita, nenhum processo alheio encerrado e nenhuma regra de firewall alterada.

## Resultado e limite

**IMPLEMENTADO no mod e validado em RAM:** receber e instalar alterações de configuração do **mesmo modelo** uma vez, acionando o consumidor incremental nativo. O teste comprova os dados/máscaras e as proteções; não comprova a aparência/peças/física no jogo.

**NÃO IMPLEMENTADO:** reconstruir outro modelo de carro sem recarregar o mundo. Não foi encontrado um comando por dados que o jogo consuma para fazer essa reconstrução isolada. A investigação foi feita em paralelo nos caminhos de factory/player e de recursos. Não se afirma que seja impossível em todo o jogo; os caminhos examinados não fornecem a receita necessária.

Os documentos [factory](RE-TROCA-CARRO-FACTORY.md) e [recursos](RE-TROCA-CARRO-RECURSOS.md) contêm asm, cadeias e limites. Zerar `player+14`, mudar o booleano `loader+4`, trocar ponteiros ou carregar a categoria3 não atende o objetivo. Essa categoria destrói corrida, jogadores, OppMgr e tráfego; o caminho individual da garagem pertence somente ao showroom.

## Mudanças no código

- `SetPartnerCar` valida um TypeName terminado e separa identidade de conteúdo da vtable/máscara transitória. Cada alteração real recebe uma revisão. `OnPartnerCar` recusa configurações antigas/duplicadas por sequência própria; `Remove` reinicia o estado para a nova sessão.
- O gancho da thread do jogo chama `car::Tick` antes da lógica de corrida. A thread de sessão apenas recebe/guarda poses e configurações; não faz mais o tick de escrita do carro. Isso também evita disputar a máscara com `0xF32A0` na thread nativa.
- São acompanhados o gerenciador, jogador, veículo, CarCfg, recurso de customização e objeto de corrida. A alteração da identidade invalida o registro de instalação. Uma configuração observada ainda incompleta pode tornar-se válida no mesmo endereço depois.
- O modelo construído é acompanhado independentemente do pedido remoto. Quando há `mcCarCustom` conhecido, seu nome de construção (`vehicle+68 -> custom+102C`, vtable454ADC) prevalece sobre metadados possivelmente defasados do CarCfg. Esse nome não é escrito pelo mod.
- CURRENT, NEXT e o CarCfg realmente usado pelo veículo (`vehicle+64`) recebem a atualização compatível uma vez por revisão/instância, inclusive quando são objetos diferentes. Aliases do mesmo alvo não causam cópias repetidas. O jogo pode consumir/normalizar seus dados sem que o próximo frame reponha o payload inteiro.
- A cópia preserva a vtable local e os bits locais existentes. Não importa a máscara do remetente. Usa `3F7F` para a atualização incremental e a máscara agregada nativa `3FFF` ao preparar configuração completa sem veículo ou no NEXT pendente. **Bit80 não é comando de reconstrução**: `271E00` o relaciona a E0/E1.
- A identidade de recursos inclui TypeName, o booleano E2 e a string `ProfileName` em F9 (13 bytes). `20260` compara os três antes de decidir recarregar a categoria3; `F32A0` não tem consumidor incremental comprovado de E2/F9. A comparação de F9 vale mesmo com E2=false. Mudanças nesses campos ficam pendentes como mudanças de modelo.
- Quando essa identidade difere do veículo construído, não copia peças/dados para ele nem altera a base CURRENT. Somente NEXT independente pode ser preparado para a próxima carga normal. Se NEXT compartilha/encobre CURRENT, o CarCfg vivo ou outra vaga em qualquer setup, a escrita é recusada.
- Aliases com a configuração do jogador local, ponteiros inválidos, TypeName truncado/inválido e rede nativa são protegidos. HUD remoto permanece -1 e há apenas uma tela.
- Diagnósticos `remote_cfg`, `car_model_pending`, `cfg_applied` mostram o estado sem alterar memória guest.

## Evidência do consumidor incremental

`0xFEF80` percorre veículos e obtém `vehicle+64` em `0xFF021`; chama `0xF96C0`, que chega a `0xF32A0` com `forceAll=false`. Esta última lê `cfg+8`, mascara `3F7F` (`F32B9..C7`) e consome os bits (`F4308..14`). `0x271E20` faz a cópia nativa de configuração preservando a vtable e marca `3FFF` em `271E82`.

O código anterior copiava o pacote inteiro a cada frame, inclusive a máscara já consumida no outro PC. Isso podia apagar a máscara antes de o consumidor local usá-la e misturar TypeName novo com o veículo antigo ainda construído. A instalação agora preserva a relação entre modelo e dados e deixa o consumidor terminar seu trabalho.

## Validação

`python tools/test_coop_memory.py`: passou com os fontes reais, incluindo todos os testes anteriores de IA, chegada e tráfego e a nova `car_config_tests.inc`:

- mesma configuração repetida não repõe bits consumidos; vtable/máscara remota não geram revisão;
- segunda alteração real marca a máscara uma vez, conservando bits locais e a configuração do jogador local;
- CarCfg do veículo e dos dois setups tratados separadamente; alias não duplica a instalação;
- outro modelo preserva CURRENT/veículo e prepara apenas NEXT independente;
- proteção de aliases/sobreposição com jogador local, veículo e CURRENT;
- nova alocação nativa recebe a última configuração; pendência desaparece quando o veículo correto existe;
- nome real do recurso tem precedência; configuração que termina de inicializar no mesmo endereço é reconhecida;
- TypeName vazio/inválido, strings sem terminação, ponteiros não mapeados e modo nativo de rede são recusados; ProfileName vazio é permitido;
- configurações fora de ordem/duplicadas, wrap de sequência e reconexão.

Compilação completa por `mod/build.cmd`, artefatos em `out/mod`, log privado `out/build-integracao-car-config.log`. Não gerar/distribuir novo ZIP como prova de hot swap. O ZIP entregue anteriormente é protocolo4; os fontes integrados agora são protocolo5 e exigem versões iguais nos dois PCs.

## O que falta

1. Testar pintura e uma peça visual do mesmo modelo no jogo, conferindo consumo de `cfg+8` e estabilidade do veículo. Desempenho/tuning exigem validação própria.
2. Descobrir um pedido nativo individual completo de reconstrução e carregamento de modelo. Sem esse caminho não há troca integral sem recarga sob a restrição de somente dados.
3. Só depois ensaiar outro modelo em ronda, transições e desconexão, conferindo física, collider, desenho, áudio e referências. A nova fila/reconstrução não está simulada nem anunciada como existente.

A suspensão dos ensaios ao vivo do handoff continua em vigor enquanto não houver caminho silencioso comprovado, sem avisos de firewall/UAC.

## Resultado ao vivo (20/10/2026): release impede a troca em mundo carregado

A ponte nativa foi validada (`native: probe PASS`, cinco chamadas, `mismatch=0`) e a construção do modelo diferente foi exercitada. O ctor `2CB270` e o build `2CBC40` executam, mas `105B80` não obtém carData: no caminho de entrada livre ele chama `105AA0`, que tenta criar o recurso e o binário **release** responde com `Cannot make car resource on RELEASE - build on Beta/debug or ask Yomal why they are not in the assets!` e retorna 0. `2CBC40` continua com produto nulo e o runner encerra em `[ICALL] unresolved target` (visto em `2CBD56/2CBD5D/2CBD64/2CBDCB`).

O pool `[50A08C]` tem 16 entradas; na ronda só o modelo em uso estava em modo 0. Entradas em modo 1 (Eclipse/Montecarlo/GS430) pertencem ao pool de garagem/showroom e `105B80` não as reutiliza no modo offline. Não há, nesta build, uma chamada de dados ou de ponte que crie um recurso de carro novo em runtime. A implementação final recusa antes do construtor e mantém o carro antigo.

**Caminho suportado, sem reconectar:** a configuração diferente continua em NEXT e é aplicada no próximo carregamento normal (corrida/garagem). Teste: parceiro trocou a garagem para Eclipse, host ainda mostrava tC; após o host carregar uma corrida, `live slot 1` passou a Eclipse, com as duas cópias conectadas. Não é troca instantânea no mundo atual.

O teste usou o comando de diagnóstico `localcar`, não a UI da garagem. Nenhum novo ZIP foi gerado.

## Correção de escopo (20/10)

A criação de recurso novo no release está bloqueada, mas a reutilização de recurso já carregado (modo 1) não foi demonstrada nem descartada. `105B80` tem o ramo `entry+0x0A != 0` → `105BD6`, que libera/rebinda `entry+0x0C` com `210630`/`105A40`; é reutilização de carData existente, não criação de arquivo. O factory offline passa modo 0, então não encontra as entradas modo 1. Forçar/copiar entradas exigiria provar ownership, registro, compatibilidade e rollback; nada disso foi feito. Portanto: troca instantânea **não demonstrada**; próxima carga normal validada.
