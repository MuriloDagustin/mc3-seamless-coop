# Diagnóstico — Midnight Club 3 DUB Edition Remix (co-op)

Legenda: **[C]** confirmado (lido no arquivo/log ou testado) · **[S]** suposição.

## 1. O que está instalado

| Item | Onde | Observação |
|---|---|---|
| Jogo (arquivos do disco) | `D:\Jogos\xbox-xrevive\Midnight Club 3 DUB Edition Remix\extracted` (4,6 GB) | `default.xbe` + `assets_x.dat*`, `audio`, `media`... **[C]** |
| Exe que você joga | `...\.xrevive-8c22…9221.exe` (65 MB), copiado para `%LOCALAPPDATA%\com.teraflopping.xrevive\runs\54540079\runner.exe` a cada execução | **[C]** pelo log `Log_File_For_Tera.log` |
| xrevive | launcher fechado em `%LOCALAPPDATA%\xrevive\xrevive.exe`; baixa builds prontas de um catálogo assinado | **[C]** |
| Saves | `...\SaveData\Midnight Club 3_ DUB edition` | **[C]** |

- **Jogo:** título `54540079` "Midnight Club 3: DUB edition", região **NTSC-U (0x1)**, versão de certificado **5**. Todas as bibliotecas são do **XDK 5849** (XAPILIB, D3D8, DSOUND, XONLINES, XACTENG, XVOICE...). **[C]** (cabeçalho do XBE)
- **Build xrevive:** versão **1.0.5** ("Portuguese and Russian translations, and multiplayer car fixes"), 34 addons internos (`mc3-netcode`, `mc3-multiplayer-garage`, `mc3-multiplayer-population`, `mc3-uncapped`, `mc3-widescreen`...), backend **Vulkan** ("Plume"). **[C]**
- **Não há código-fonte, git nem build** na pasta do jogo. O exe do xrevive é fechado, **não exporta nenhuma função** (verificado com pefile) e não vem com símbolos. **[C]**

### Consequência
Não dá para fazer um "mod em cima da recompilação do xrevive" no nível do código, porque não temos o código. Há dois caminhos:

1. **Build própria (escolhido):** recompilar o mesmo `default.xbe` com o SDK aberto **XboxXBE-Recomp** (`C:\dev\XboxXBE-Recomp`), o mesmo usado no co-op do Black. Ganhamos hooks `X86_FUNC` em qualquer função, o menu F10 em ImGui e reaproveitamos quase toda a infraestrutura do Black (rede, EOS, canal de controle, testes). Perdemos os addons do xrevive (widescreen, HUD HD, correções de netcode...). O risco é o MC3 precisar de correções que o xrevive já fez e o SDK aberto não tem.
2. **Injeção no runner do xrevive (plano B):** a memória do Xbox fica mapeada de forma plana (`Guest VA image: 4 GiB at <base>`, com host = base + VA), então ler e escrever posições é fácil. Porém não há hooks de função (sem símbolos), o overlay precisaria interceptar Vulkan, e cada atualização do xrevive quebraria os padrões de bytes.

## 2. Pasta de desenvolvimento

- Repositório: `C:\dev\mc3-coop` (git local, branch `coop-noite`, sem remoto).
- `out\` → `D:\dev\mc3-coop-build`, `generated\` → `D:\dev\mc3-coop-data\generated`, `assets\` → `D:\dev\mc3-coop-data\assets` (cópia do `extracted`). São junções, porque o C: tem só 36 GB livres. Tudo isso está no `.gitignore`.
- Compilar: `build.bat` (clang/cmake/ninja do Visual Studio 18). Sem perguntas: `set CI=1 && build.bat`.

## 3. Pistas do código do jogo (strings do XBE)

Classes vistas em mensagens de assert (engine Angel/"mc*"): `mcGameState::EnterState`, `AmcGameState::DoKillGarage`, `mcRaceBase::Load` / `LoadCheckpoints`, `mcRaceStateDerived::EnterState`, `mcRaceHookmen::GetOpenRaceFromOpponentIdx`, `mcPgCareer::SetupNextCareerRace` / `ReportRaceResult`, `mcTkCareerStart::SetupNextCareerEvent`, `mcPlayerFactory::MakeFeedback`, `mcMemCard::Update`, `mcFeMemCard::Update`, `aiCurveCatrom`, `phBoundGeometry`. **[C]** (as strings existem) / funções ainda não localizadas.

Outras pistas: `hud_%d_player_game-player_%d` (o jogo tem **tela dividida para 2 jogadores**, ou seja, já suporta 2 carros de jogador no mesmo mundo) **[S]**; `host_selected_next_race`, `cruise` (modo Cruise online/System Link) **[C]**; `NoTraffic`, `tune/traffic`, `$/resources/city/%s_traffic` **[C]**; `%sMC_Career_log_%s.txt` (log de carreira de depuração) **[C]**.
