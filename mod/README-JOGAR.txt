MC3 CO-OP - VERSÃO DE TESTE
===========================

Co-op online da campanha do Midnight Club 3 DUB Edition Remix, por cima do xrevive.
Cada um joga com o carro da própria carreira; o carro do parceiro aparece no seu mundo.

O QUE PRECISA
- O xrevive com o Midnight Club 3 DUB Edition Remix instalado e funcionando (sua própria cópia).
- Os dois jogadores com a mesma versão do pacote.

COMO JOGAR
1. Extraia o zip inteiro numa pasta em que você possa gravar arquivos (ex.: Documentos\MC3Coop).
   Não é necessário alterar a pasta do jogo. Não abra o executável de dentro do zip.
2. Feche manualmente o jogo e o xrevive, se estiverem abertos, e abra MC3Coop.exe.
   O lançador verifica os caminhos dos processos e avisa se encontrar outra cópia aberta.
   Na primeira vez ele procura a instalação. Se precisar, selecione a pasta
   "Midnight Club 3 DUB Edition Remix" que contém extracted e SaveData.
   O caminho fica salvo em MC3Coop.ini ao lado do lançador.
   O executável criado pelo xrevive pode se chamar "Midnight Club 3 DUB Edition Remix.exe"
   ou, nas versões antigas, ".xrevive-<código>.exe". O lançador prepara a cópia MC3Runner.exe.
3. Na primeira execução ele oferece criar uma regra de entrada UDP no Firewall do Windows.
   Só se você escolher Sim, o Windows pede administrador para executar o netsh que cria a regra.
   O jogo continua sem elevação. Você pode recusar ou cancelar e continuar jogando.
   A regra permite UDP apenas para o MC3Runner.exe desta pasta, em redes privadas.
   Depois de mudar a pasta do mod, execute novamente MC3Coop.exe --firewall.
   Para repetir a oferta, crie um atalho para MC3Coop.exe e acrescente --firewall ao Destino.
   Se sua rede for pública, revise as permissões do firewall para essa rede manualmente.
4. No jogo, aperte F10 para abrir o painel CO-OP.
   - Pela internet: um cria a sala em "Create online room" e manda o código;
     o outro digita o código e clica em "Join by code".
   - Ou por IP: um clica em "Host"; o outro digita o IP do host e clica em "Join IP".
     Para conexão direta pela internet, libere/encaminhe UDP 45410 no roteador do host.
     A regra do Firewall do Windows não configura o roteador.
5. Entrem os dois na carreira. O carro do parceiro aparece como mais um corredor.
   F8 (ou "Take me to my partner") leva você até o parceiro.

CONTROLES
- O teclado fica habilitado por padrão. Também pode ser usado um controle compatível com XInput
  (como um controle Xbox no Windows). Outros tipos de controle ainda precisam de validação.
- A sincronização e a largada do parceiro foram testadas com o teclado habilitado junto ao controle
  interno de teste. O lançador seleciona o modo de controles necessário para essa sincronização.
- Se você já definiu XRECOMP_KEYBOARD=0 no Windows, o lançador respeita essa opção.
  Remova essa variável para voltar a habilitar o teclado.

SE FALTAR COOP-ONLINE.TOML
- O jogo abre e o co-op por IP continua disponível (Host / Join IP).
- As salas por código precisam de coop-online.toml e do SDK EOS do pacote.
  Extraia novamente o pacote recebido para recuperar esses arquivos.
- coop-online.toml contém credenciais: não publique o pacote nem adicione esse arquivo ao git.

COMO ENVIAR OS LOGS
- Crie um atalho para MC3Coop.exe. Em Propriedades > Destino, deixe, por exemplo:
  "C:\Users\SeuNome\Documents\MC3Coop\MC3Coop.exe" --logs
- Esse atalho não abre o jogo e funciona mesmo sem a instalação ou DLL do mod.
- Ele cria MC3Coop-logs-DATA-HORA-PID.zip na sua Área de Trabalho e mostra o caminho.
  Envie esse zip para quem está ajudando. Não envie o zip de instalação com credenciais.
- São coletados diagnósticos .log, .txt, .json e .dmp da pasta logs (incluindo logs\runner)
  e Log_File_For_Tera.log ao lado do exe, quando existir. Links/junctions são ignorados.
  coop-online.toml, saves e executáveis não entram no zip. Os logs podem conter caminhos locais e IPs.
- Se não houver logs ou algum arquivo estiver indisponível, o lançador avisa.
  Se a coleta falhar, confira espaço e permissão na Área de Trabalho ou envie a pasta logs manualmente.

ESTADO DESTA VERSÃO
- Pacote experimental. Os testes abaixo usaram duas cópias na mesma máquina, em corridas comuns.
- A largada do host levou o parceiro automaticamente à mesma corrida de carreira.
- A vaga remota não tem HUD nem câmera. Nas corridas convencionais ela é concluída internamente
  para não travar sua chegada; pode aparecer como concluída/desistente até receber o resultado real.
- Tela inteira, chegadas sem travar e retorno à ronda passaram nos circuitos testados.
- Vitória recebida antes da chegada local e prêmio para os dois passaram em um circuito.
- Na corrida comum com dois jogadores, quem chega antes pode aguardar até 15 segundos pelo resultado
  do parceiro. A vitória recebida com 2 segundos de atraso passou no teste local, incluindo complemento
  de prêmio zero para $ 800. Uma colocação inferior natural ainda precisa de teste específico.
- Foram sincronizados 48 carros de tráfego, inclusive após carregar uma corrida e voltar à ronda.
  Colisões após grandes deslocamentos, carros acidentados e caminhões ainda precisam de validação.
- A IA passou com cinco adversários: posições acompanharam o host e as cinco colocações finais
  permaneceram iguais nas duas cópias. Isso ainda exige corridas comuns e adversários compatíveis.
  As colocações dos jogadores humanos podem divergir entre as cópias.
- Perda de pacotes e latência de internet ainda não foram validadas; a IA pode voltar temporariamente
  ao controle local se deixar de receber o estado do host durante a corrida.
- Corridas de eliminação, pontos, perseguição e outras modalidades precisam de validação específica.
- Se o resultado do parceiro não chegar dentro dos 15 segundos, vale seu resultado local.
  Uma vitória recebida depois dessa decisão não concede crédito retroativo. O timeout foi verificado
  em memória simulada; o teste ao vivo desse caso ainda está pendente.
- A espera usa uma segunda passagem local pela chegada. Pode repetir o aviso de chegada e incluir
  a espera no tempo registrado da corrida; não foi observado novo recorde falso de melhor volta.
- Troca do carro remoto ainda exige recarregar o mundo (garagem/menu).
- Nos fontes atuais, alterações compatíveis do mesmo modelo têm aplicação incremental implementada;
  pintura/peças ainda precisam de confirmação visual no jogo. Outro modelo continua pendente para recarga.
- Os fontes integrados usam protocolo 7; o ZIP dc25ed3 já entregue usa protocolo 4.
  Não misture builds: ambos os jogadores precisam usar a mesma versão.
- Nos fontes atuais, o parceiro tem bloqueio de largadas próprias na rua e recebe aviso se o host
  largar enquanto ele está na garagem. Posições de outra corrida são descartadas. Esses novos
  comportamentos passaram em memória simulada; ainda precisam de confirmação no jogo.
- Nos fontes atuais, o parceiro espera a decisão do host no resultado. O retorno à rua e o avanço
  de série usam os comandos dos menus nativos, com identificação da próxima corrida e nova serial.
  Se a carreira local estiver em outra etapa, o caminho previsto é sair para a rua e seguir o host.
  Isso passou em memória simulada e compilou; menus, tempos de carga e séries ainda exigem teste no jogo.
  Menus não reconhecidos ficam aguardando; use F10 para desconectar se necessário.
- O parceiro ausente ainda pode aparecer na classificação nativa como concluído/DNF.
- Salas EOS entre dois PCs ainda precisam do teste com o amigo; criar sala localmente não prova a conexão.

OBSERVAÇÕES
- O jogo usa o modo gráfico D3D12 do xrevive (o painel F10 é desenhado nele).
- Seus saves são os mesmos do xrevive normal.
- Para testes, MC3COOP_GAME força a pasta informada e desliga a busca automática.
- O pacote compilado fica em out\MC3Coop-test.zip dentro do workspace. Uso local; não publicar.
