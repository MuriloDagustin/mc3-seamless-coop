# Relatório — co-op da campanha do MC3

**Resumo:** o carro de campanha do parceiro já entra no seu mundo como mais um corredor e anda pela rede. Ainda falta o que torna isso jogável para vocês dois: menu F10, EOS, início de corrida sincronizado, crédito para os dois e o zip.

## Rota atual: mod dentro do seu xrevive
- Um `xinput1_4.dll` (o mod) fica ao lado de uma **cópia** do runner do xrevive. Sua instalação e o seu exe não são tocados.
- O mod acha a memória do Xbox no runner, onde host = base + endereço do Xbox.
- A tentativa anterior, uma recompilação própria com o SDK do Black, ficou parada: tela preta e sem XACT. Está no commit `80b0387`, se um dia for útil.

## O que funciona (testado com 2 cópias escondidas na mesma máquina)
| Item | Como foi testado |
|---|---|
| Cópias de teste mudas, num desktop invisível, com save e log próprios; nenhuma janela no seu desktop | `tools\xrun.py`, conferido pela lista de janelas |
| Prints dessas cópias (pelo D3D12 do runner) | `tools\ctl.py <porta> shot` |
| Controle virtual (XInput) para dirigir as cópias pelos menus e na corrida | as cópias chegam à 1ª corrida e aceleram |
| Carro do jogador lido ao vivo (posição, rotação, velocidade, comandos) | conferido acelerando: posição muda, velocidade em `car+0xB0` |
| Teleporte (base do "me leve até meu parceiro") | carro movido 25 m e parado no lugar |
| Rede direta (UDP): sessão, apelidos, posição a 30 Hz | o parceiro vê o host a ~1 m de atraso a 17 m/s |
| **Carro de campanha do parceiro no seu mundo** | o HUD do host passa de 4 para 5 corredores; o tC do parceiro aparece andando na frente do host |
| Mesmo grid nos dois mundos (host na posição 0, parceiro na 1) | posições de largada lado a lado nas duas cópias |
| O host recebe o carro da garagem do parceiro (`CarCfg`, 0x108 bytes) | pintura alterada no parceiro chega ao host em 1 s |

## Como o jogo foi usado (detalhes em `docs/RE-MAPA.txt` e `docs/RE-REMOTO.txt`)
- **O parceiro é um 2º jogador humano "offline".** Não uso o modo rede do jogo, porque ele desliga IA, tráfego, chegada, dinheiro, crédito e autosave da carreira.
- **Configuração da vaga:** os setups de corrida recebem 2 humanos, e a vaga 1 recebe o `CarCfg` do parceiro, sem controle.
- **Movimento:** o mod move o carro do parceiro (posição, rotação, velocidade e comandos), interpolado 100 ms atrás.

## O que falta (em ordem)
1. **Menu F10 dentro do jogo** (ImGui no D3D12 do runner). Para isso o jogo roda com `XRECOMP_GPU_BACKEND=d3d12` pelo nosso lançador, e não com o Vulkan que o seu xrevive usa hoje; ainda não testei se a imagem fica igual.
2. **Início de corrida sincronizado.** Quando o host começa uma corrida, o parceiro entra na mesma (identificada por cidade + índice, porque o ID muda de save para save).
3. **Crédito para os dois.** Se qualquer um vencer, a corrida conta na carreira dos dois e cada um passa pelas próprias telas de save.
4. **EOS (sala por código)** com as credenciais do Black. Compila, mas não foi testado.
5. **Lançador e zip para o seu amigo**, com README.
6. **Melhorias:** IA sincronizada pelo host, colisão entre os carros e a prova visual do carro do parceiro com dois saves diferentes.

## Pendências e riscos conhecidos
- **Corrida presa na chegada.** O jogo só fecha a corrida quando todos os humanos chegam. Se o parceiro cair no meio, o mod precisa voltar para 1 humano (não testado).
- **Mudança de carro só no próximo carregamento.** Se o parceiro trocar de carro, o seu mundo só mostra o novo depois que o mundo recarregar (garagem ou menus).
- **Base da navegação automática.** O menu principal ignora comandos durante a animação de entrada, e o direcional segurado por 300 ms repete.

## Commits locais (branch `coop-noite`, sem push)
`80b0387` build própria (abandonada) · `09d2570` mod, canal de teste e prints · `f54e0f9` sessão de rede e carro local · `3e5cd1e` carro do parceiro no seu mundo
