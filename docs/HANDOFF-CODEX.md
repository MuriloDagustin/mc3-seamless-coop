# Handoff para o Codex — co-op do MC3 (18/10/2026, ~14h30)

Responda em português. Trabalhe em `C:\dev\mc3-coop` (branch `coop-noite`). Commits locais podem ser feitos ao fim de cada item testado. **Nunca faça push.**

## Regras fixas (do usuário, não negociáveis)
- Nunca feche, mate ou mexa no jogo dele nem no `xrevive.exe`. Nunca mate processo pelo nome. Só feche cópias de teste que você abriu: use o arquivo `D:\dev\mc3-coop-data\xrun\stop-tests` ou confira a linha de comando, que precisa conter `mc3-coop-data`.
- Testes sempre mudos, num desktop invisível (`tools\xrun.py` / `tools\pair.py` já fazem isso). Não mande teclas do Windows e não roube foco.
- Não altere nada em `D:\Jogos` (só leitura). A pasta de jogo de teste é `D:\dev\mc3-coop-data\testgame\...`.
- Nada de arquivos do jogo nem do `coop-online.toml` (credenciais) no git. O zip tem credenciais: uso local, não publicar.
- Não diga que funciona sem ter testado. Registre o que testou e como em `docs/TESTE-ISOLADO-2026-10-18.md`.

## Estado atual (último commit `a10a532` + mudanças não commitadas, ver abaixo)
Testado ao vivo com 2 cópias (host 45703, parceiro 45704) no **build novo do xrevive** (`0x6AD5001B`):
- carro do parceiro no mundo do host, sem tela dividida; chegada sem travar;
- largada comum (o parceiro larga 0,3 s depois do host);
- vitória para os dois e prêmio;
- tráfego: 48 de 48 carros, erro máximo de 0,3 m.

Mudanças desta sessão que **ainda não estão em commit** (compilam; o pacote foi gerado):
1. **`mod/launcher/launcher.cpp` e `tools/xrun.py`:** o xrevive mudou o nome do runner para `Midnight Club 3 DUB Edition Remix.exe` (antes era `.xrevive-<sha>.exe`). Agora a busca aceita qualquer `.exe` de nível superior com 20 MB ou mais.
2. **`mod/src/race.cpp`:** quando a corrida do host já existe na carreira do parceiro, o parceiro empresta os campos `+0x0B..+0x4B` do registro do host para essa largada (adversários e dificuldade) e devolve os originais depois que a corrida carrega. Os tipos de tarefa (`+0x1E`) e o flag de "nova" ficam como eram.
3. **`mod/src/guest_poll.h`:** o ponto de chamada do gancho de jogo agora é achado pela tabela de importação (`call [rip+iat]` para `xinput1_4` ordinal 2). Já está no commit `a10a532`.

Faça o commit dessas mudanças primeiro.

## Como testar (receita que funciona)
```
cmd /c mod\build.cmd                      (DLL do mod; mod\package.cmd gera out\MC3Coop-test.zip)
python tools\pair.py                      (2 cópias até o cruise, ~2,5 min; reinicia as anteriores)
python tools\traffic_test.py 15           (tráfego host x parceiro)
python tools\race_start_test.py           (host vai a uma corrida aberta; parceiro deve largar junto)
python tools\ai_test.py 3                 (posição de cada adversário host x parceiro)
python tools\race_finish_test.py          (leva o host à chegada por teleporte e lê os dois)
python tools\test_coop_memory.py          (testes de memória simulada, sem abrir o jogo)
python tools\ctl.py <porta> state|shot <nome>|players|peek <addr> [n]
```
`pair.py` usa os saves descartáveis `D:\dev\mc3-coop-data\xsave\i3` e `i4`. A DLL nova só entra quando as cópias são reiniciadas.

## Tarefas, em ordem

### 1. IA dos adversários igual nas duas cópias (critério validado localmente em 18/10)
- **Atualização Codex:** implementados setup autoritativo e resultados; teste5/5 com chegada passou. Ver TESTE-ISOLADO-2026-10-18.md. Texto abaixo preserva a investigação original. Próximo item: pacote.
- **Sintoma:** `tools\ai_test.py` mostra `ai=layout_mismatch`. Os pacotes `kAi` chegam (received=5), mas o parceiro recusa.
- **Causa encontrada:** a máscara de adversários do setup difere. No host, `[[0x53F2E0]+0x3D4] = FFFFFF1F` (5 adversários, `race+0x18 = 5`); no parceiro, `FFFFFF0F` (4). Os nomes também diferem (vêm do pool de nomes, `0x1D15B0`), e o hash de layout em `ai.cpp` inclui o nome (`progress+0x5C`).
- **O que fazer:**
  - **(a)** Incluir no `race::Start` (o pacote do host) a máscara `setup+0x3D4` e `setup+0x40`, lidas do setup NEXT `[0x53F2E4]` assim que ele estiver preenchido. Isso fica a ~3 s do trigger: espere `[[0x53F2E4]+0x68]` ser o nome da corrida. Se precisar, mande um segundo pacote, `kRaceSetup`, depois.
  - **(b)** No parceiro, escrever essa máscara nos dois setups (`[0x53F2E0]` e `[0x53F2E4]`) depois que o jogo dele montar o setup e antes de o player manager e o OppMgr serem criados (`OppMgr::Init 0x1E43C0` usa `race+0x18`). O mapa está em `docs/RE-MAPA.txt` (ai-traffic): número de adversários = popcount de `setup+0x3D4` sobre os adversários do arquivo da corrida (`0x26B0C0`).
  - **(c)** Tirar o nome do piloto do hash de layout em `mod/src/ai.cpp` (`ReadPool`, `HashString(... progress+0x5C ...)`). Os nomes são cosméticos, e a ordem e os pontos de largada já identificam cada adversário. Outra opção: o host manda os nomes e o parceiro escreve.
- **Critério de pronto:** `ai_test.py` com `ai=experimental applied=N`, a distância por adversário abaixo de ~5 m, e o host vendo a mesma posição de chegada que o parceiro.
- **Referências:** `docs/RE-IA.md` e `docs/RE-IA-IMPLEMENTACAO.md`. O bloqueio do cérebro da IA é o bit `car+0x15C` 0x10, já em `ai.cpp`.

### 2. Pacote para o amigo
- Testar o zip de verdade com `tools\test_launcher.py`. Ele extrai o zip numa pasta de teste e força `MC3COOP_GAME` para a pasta de teste.
  - **Atenção:** a pasta de teste tem o runner ANTIGO. Copie o runner novo (`D:\Jogos\...\Midnight Club 3 DUB Edition Remix.exe`, só leitura) para `D:\dev\mc3-coop-data\testgame\Midnight Club 3 DUB Edition Remix\`.
  - Confira que o lançador acha o runner novo, que o mod carrega e que o F10 aparece.
- Atualizar `mod/README-JOGAR.txt`: o estado da IA e o nome novo do runner.

### 3. Vitória tardia (vitória do parceiro chegando depois do resultado local)
- O projeto está em `docs/RE-VITORIA-TARDIA.md`: adiar o primeiro `ReportRaceResult` e mandar também resultado de derrota.
- Implementar só para o circuito comum (vtable `0x46B2D8`) e testar com `race_finish_test.py`, fazendo o parceiro chegar primeiro no próprio mundo.

### 4. Pendências menores
- Caminhões de tráfego (`+0xEC` reboque) ficam de fora: escondê-los ou sincronizá-los.
- Troca de carro do parceiro sem recarregar o mundo.
- Outras modalidades de corrida (eliminação, pontos, perseguição).

## Endereços úteis (guest Xbox; host = base + VA)
- Player manager `[0x558814]`: players em `+0x08`. Carro: `player+0x14 → vehicle`, `vehicle+0x08 → car`, `vehicle+0x1C → phInst` (matriz em `+0x10`).
- Setups: `[0x53F2E0]` (atual) e `[0x53F2E4]` (próximo). Neles: humanos `+0x4C`, views `+0x50`, máscara de adversários `+0x3D4`, nome da corrida `+0x68`.
- Corrida atual: `[[0x533E24]+4]`. Máquina de estado: `[0x557EEC]+4` (5 largada, 6 correndo, 7 chegada, 11 autosave, 12 fim).
- Carreira `[0x557F0C]`: `+0x0C` dados da carreira, `+0x2C == 4` quando há largada pedida, `+0x34` id da corrida.
- OppMgr `[0x534074]`: `+0` total, `+4` competidores, `+8` array com entradas de 0x43C.
- Tráfego: `tm = [[0x50152C]+8]`, pool em `tm+0x130`, slots de 0x21C bytes.
