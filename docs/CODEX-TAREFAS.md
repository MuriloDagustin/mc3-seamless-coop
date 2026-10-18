# Tarefas para o Codex

Abra o Codex na pasta **`C:\dev\mc3-coop-codex`** (worktree do branch `codex`; o Claude trabalha em `C:\dev\mc3-coop`, branch `coop-noite`). Cole UMA tarefa por sessão.

---

## Tarefa 1 — Mapear a IA dos adversários para sincronizar (só análise, não roda o jogo)

```
Você está ajudando num mod de co-op online para Midnight Club 3 DUB Edition Remix (Xbox original) que roda dentro do runner fechado do xrevive. O mod só lê/escreve a memória do jogo (o Xbox fica mapeado em host = base + VA) e tem um gancho que roda na thread do jogo (o proxy de XInputGetState). Não há como alterar o código do jogo.

REGRAS FIXAS: não rode o jogo nem nenhum executável do jogo; não feche nem mate processos; não mexa em D:\Jogos; trabalhe só dentro de C:\dev\mc3-coop-codex; não faça commit/push (eu peço depois); responda em português.

RECURSOS (somente leitura):
- C:\dev\mc3-coop\generated\game\x86_recomp.*.cpp: todo o código do jogo recompilado, cada função começa com "X86_FUNC(__imp__sub_XXXXXXXX)" e cada instrução x86 original está num comentário "// <VA> <asm>". São ~195 MB: use busca (rg/grep), não leia arquivos inteiros.
- python C:\dev\mc3-coop\tools\xref.py callers|callees|refs|asm 0xADDR e str "texto" (rode a partir de C:\dev\mc3-coop; o índice já está em out\xref.json).
- C:\dev\mc3-coop\docs\RE-MAPA.txt, RE-REMOTO.txt, RE-CORRIDA.txt: mapas já verificados (carro do jogador, setups de corrida, IA: OppMgr [0x534074], entradas 0x43C bytes, drivers 0x1ED2F0, rubber band 0x1EB540, lista de corredores [0x558838], corrida [[0x533E24]+4]).

OBJETIVO: no co-op o HOST é a autoridade da IA. Cada cópia roda a mesma corrida de carreira; no parceiro, os carros da IA devem ser "puppets" que seguem o estado enviado pelo host. Mapeie com endereços e evidência em asm (marque CONFIRMADO ou PALPITE):
1. Para cada adversário de IA: onde está o carro (cadeia de ponteiros a partir de OppMgr ou da lista de corredores), a matriz/posição (phInst +0x10, como no jogador?), o corpo rígido (velocidade +0x44, rotação +0x50), os comandos (car+0x6C..0x78) e o progresso na corrida (volta, checkpoint, posição, terminou).
2. Um ID estável para casar o adversário i do host com o i do parceiro (a ordem vem do arquivo da corrida? é igual nas duas cópias?).
3. Como desligar o "cérebro" da IA no parceiro só escrevendo dados (um campo de driver, um tipo, throttle limit 0...), mantendo o carro desenhado, com colisão e contando na corrida.
4. Uma receita só de dados para o parceiro: o que escrever por adversário a cada frame para seguir o host sem brigar com a lógica local, e o que fazer quando o host termina a corrida (posições finais iguais).
Grave o resultado em C:\dev\mc3-coop-codex\docs\RE-IA.md.
```

---

## Tarefa 2 — Lançador amigável para o amigo

```
Você está ajudando num mod de co-op online para Midnight Club 3 DUB Edition Remix que roda dentro do runner do xrevive. O lançador é C:\dev\mc3-coop-codex\mod\launcher\launcher.cpp (Win32, C++20, compilado por mod\package.cmd com o clang-cl do Visual Studio): acha a pasta do jogo do xrevive (MC3Coop.ini, busca ou pergunta), copia o runner .xrevive-*.exe para a própria pasta como MC3Runner.exe e o inicia com o mod (xinput1_4.dll ao lado). MC3COOP_GAME=<pasta> força uma pasta e desliga a busca (os testes usam isso).

REGRAS FIXAS: NUNCA abra o jogo de verdade nem rode o MC3Coop.exe/MC3Runner.exe sem MC3COOP_GAME apontando para D:\dev\mc3-coop-data\testgame\Midnight Club 3 DUB Edition Remix; não feche nem mate processos que você não abriu (e nunca pelo nome); não mexa em D:\Jogos; não roube o foco nem mande teclas para janelas; trabalhe só em C:\dev\mc3-coop-codex; não faça commit/push; responda em português. O zip gerado contém credenciais (coop-online.toml): nunca o coloque no git nem o publique.

O QUE FAZER (só em mod\launcher\launcher.cpp, mod\README-JOGAR.txt e mod\package.cmd):
1. Mensagens em português (com acentos, UTF-16) e um texto curto do que fazer em cada erro.
2. Se o xrevive ou outro MC3Runner já estiver aberto, avisar e não abrir outro (checar pelo caminho do executável, sem fechar nada).
3. Na primeira execução, oferecer (com confirmação do usuário) criar a regra de firewall de entrada UDP para MC3Runner.exe (netsh, pedindo elevação só para isso).
4. Um atalho "MC3Coop.exe --logs" que junta a pasta logs ao lado do exe e o log do runner num zip na Área de Trabalho, para mandar quando der problema.
5. Se coop-online.toml faltar, abrir mesmo assim (o co-op por IP funciona) e explicar no README.
Compile com mod\package.cmd e teste o que der SEM abrir o jogo de verdade (por exemplo --logs e as mensagens). Diga o que testou e o que não.
```
