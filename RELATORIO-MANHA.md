# Relatório da manhã — co-op do MC3

**Resumo: ainda NÃO dá para jogar co-op.** O critério de pronto (F10 → hospedar/entrar → ver o carro do parceiro) não foi atingido. O motivo está abaixo, com o que foi feito e testado.

## A descoberta que mudou o plano
- O MC3 que você joga é um exe **fechado** do launcher xrevive (`runner.exe`, renderer Vulkan, cerca de 34 addons internos). Na pasta do jogo não há código-fonte; o exe não exporta funções e não tem símbolos. Não dá para fazer um mod "em cima dessa recompilação" no nível do código. Detalhes em `docs/DIAGNOSTICO.md`.
- Escolha feita: **recompilar o mesmo `default.xbe` com o SDK aberto XboxXBE-Recomp** (o mesmo do co-op do Black) e reaproveitar o co-op do Black. O custo é corrigir no SDK aberto o que o xrevive já tinha corrigido para o MC3.

## O que existe agora (`C:\dev\mc3-coop`, branch `coop-noite`, commits locais)
| Item | Estado | Como foi testado |
|---|---|---|
| Build própria do MC3 (`build.bat`, `tools\b.cmd` incremental, cerca de 1 min) | ✅ compila | build limpo, exit 0 |
| Boot: APU (`XGSCNT`), 11 setters do DirectSound sem nome, Winsock 2.0, Bink (`BinkWait`/`BinkNextFrame`), `D3D_FvfShader`, `DrawIndexedVerticesUP`, `IDirectSound_CreateSoundStream` | ✅ cada crash corrigido | cópia escondida, log analisado com `tools\crash.py` |
| Cópias de teste escondidas (desktop invisível, `--mute`, `--instance`) | ✅ | `tools\testcopies.py`; conferi que nenhuma janela aparece no seu desktop |
| Canal de controle UDP (ping, state, pad, peek/poke, find, shot, coop, players, rescue) | ✅ funciona | `tools\ctl.py 45703 ping` → `pong frame=…` |
| Captura de tela de cópia escondida (`shot` → PNG) | ✅ funciona | `captures\*.png` |
| Sessão co-op (UDP direto + EOS, até 8, host roteia) e menu F10 | ⚠️ compila, **não testado** | sem carro ainda, não dá para testar de verdade |
| EOS | ⚠️ compila com o SDK `C:\dev\eos-sdk` e o `coop-online.toml` do Black (copiado só para a pasta de teste) | não testado |
| Carro do jogador / carro do parceiro | ❌ não começado | depende do jogo chegar ao gameplay |

## Onde parou (bloqueio atual)
1. **Tela preta:** a build roda, mas os frames só têm `clear` e nenhuma draw (vi isso pelo `frame.txt` das capturas, 4 vezes em 25 s, apertando A/Start).
2. **O som (XACT) derruba o jogo** depois de 20 a 70 s. O SDK aberto **não tem XACT nenhum**, e a XACT do MC3 chama funções internas do DirectSound que ficam sem nome (`IDirectSoundStream_SetOutputBuffer` = `3C2933`, cálculos 3D em `3C1627`/`3C2C7C`...). Estou fechando uma por uma.

## Próximas decisões que são suas
- **Seguir na build própria ou ir para o plano B?** O plano B é injetar uma DLL no runner do xrevive (memória plana, host = base + VA). Ele mantém seus addons e o renderer que já funciona, mas não tem hooks de função, o overlay teria que ser em Vulkan e cada atualização do xrevive quebra o mod. Minha recomendação: seguir na build própria só se a tela preta e o XACT caírem logo. Se não, conversar com o autor do xrevive (que já tem netcode próprio do MC3) pode valer mais que as duas opções.
- **Teclado:** o SDK não mapeia teclado para o controle (o xrevive tinha `XRECOMP_KEYBOARD=1`). Se você joga no teclado, isso precisa entrar na build própria.

## Como reproduzir
```
cd C:\dev\mc3-coop
tools\b.cmd                                   (build incremental)
python tools\boot_test.py 90                  (uma cópia escondida por 90 s, prints e crash explicado)
python tools\ctl.py 45703 state | shot nome   (com uma cópia aberta por tools\testcopies.py)
```
