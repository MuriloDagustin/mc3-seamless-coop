# MC3 Seamless Co-op

🇺🇸 [Read in English](README.md)

Jogue a carreira do **Midnight Club 3: DUB Edition Remix** (Rockstar, Xbox) junto com um amigo, no PC.

É um mod para o runner do xrevive do jogo original de Xbox. O modo online do jogo
não existe mais e a carreira é de um jogador só: tudo aqui é feito em cima do próprio código do jogo.
**É uma versão de teste:** espere problemas e travamentos.

> **Você precisa da sua própria cópia do jogo, já funcionando no xrevive.** Este repositório tem só o
> código do co-op. Não tem arquivos do jogo nem código do xrevive.

## Como o Seamless Co-op do Elden Ring

- Cada jogador dirige **o carro da própria carreira**; o carro do parceiro aparece na sua cidade.
- **As duas carreiras avançam.** Uma corrida que o host começa é corrida pelos dois; se qualquer um
  ganhar, ela conta como vencida nas duas carreiras (quem ainda não tinha liberado também corre e ganha).
- Os adversários da corrida e o trânsito da cidade são os do host, sincronizados no parceiro.

## Como jogar

1. Compile o mod (abaixo), ou pegue o pacote de teste de quem compilou, e extraia em qualquer pasta.
2. Feche o xrevive e abra o `MC3Coop.exe`: ele acha o jogo, prepara uma cópia do runner ao lado dele e
   carrega o mod nela. A pasta do jogo nunca é alterada.
3. No **menu principal**, aperte **F10 → CO-OP**:
   - **Sala online** (Epic Online Services): um cria a sala e manda o código, o outro digita. Precisa do
     `coop-online.toml` com as suas credenciais EOS (veja `config/coop-online.example.toml`).
   - **Por IP**: um hospeda, o outro entra pelo IP do host (UDP 45410 acessível no host).
4. Depois os dois entram na carreira. **F8** (ou *Take me to my partner*) leva você até o parceiro.

Conectem antes de entrar na carreira: o carro do parceiro é montado quando a cidade carrega.

## Situação

Funciona nos testes (duas cópias no mesmo PC, e dois PCs pelo EOS): conexão, os dois carros na mesma
cidade, começar juntos uma corrida da carreira, adversários e trânsito sincronizados, vitória contando
para os dois.

Problemas conhecidos: o jogo do parceiro trava se ele conecta com o host na garagem, trava quando uma
corrida termina indo para a garagem, e as séries de corridas (sprint, circuito) podem sair de sincronia
entre os dois. Veja [docs/HANDOFF-CODEX.md](docs/HANDOFF-CODEX.md).

## Para desenvolvedores

- `mod/`: o mod, um proxy `xinput1_4.dll` carregado pelo runner (`mod/build.cmd`, clang-cl), e o lançador
  (`mod/launcher`). `mod/package.cmd` gera o pacote local de teste.
- `tools/`: cópias de teste numa área de trabalho oculta, sem som, com portas e saves próprios
  (`tools/xrun.py`, `tools/pair.py`), testes ao vivo e testes de memória (`tools/test_coop_memory.py`).
- `docs/`: notas de engenharia reversa (`RE-*.md`) e relatórios de teste.
- `src/`, `CMakeLists.txt`: uma tentativa anterior sobre o [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp).

## Licença

[MIT](LICENSE). O código de terceiros em `mod/third_party` mantém a sua própria licença.
