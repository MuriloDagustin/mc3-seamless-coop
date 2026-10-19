# MC3 Seamless Co-op

🇧🇷 [Leia em português](README.pt-BR.md)

Play the career of **Midnight Club 3: DUB Edition Remix** (Rockstar, Xbox) together with a friend, on PC.

This is a mod for the xrevive runner of the original Xbox game. The game's online
mode is gone, and its career is single player: everything here is built on top of the game's own code.
**It is a test version:** expect rough edges and crashes.

> **You need your own copy of the game, already working in xrevive.** This repository contains only the
> co-op code. It has no game files and no code from xrevive.

## Like Elden Ring Seamless Co-op

- Each player drives **the car of their own career**; your partner's car shows up in your city.
- **Both careers progress.** A race the host starts is raced by both; when either player wins, the race
  counts as won in both careers (a partner who has not unlocked it yet still races and gets the credit).
- The race's opponents and the city traffic are the host's, synchronized to the partner.

## Playing

1. Build the mod (below), or get the test package from someone who did, and extract it anywhere.
2. Close xrevive and run `MC3Coop.exe`: it finds the game, prepares a copy of the runner beside itself and
   loads the mod in it. The game folder is never changed.
3. In the **main menu**, press **F10 → CO-OP**:
   - **Online room** (Epic Online Services): one player creates a room and sends the code, the other types
     it. Needs `coop-online.toml` with your own EOS credentials (see `config/coop-online.example.toml`).
   - **By IP**: one player hosts, the other joins the host's IP (UDP 45410 reachable on the host).
4. Then both enter the career. **F8** (or *Take me to my partner*) takes you to your partner.

Connect before entering the career: the partner's car is built when the city loads.

## Status

Works in tests (two copies on one PC, and two PCs over EOS): connection, both cars in the same city,
starting a career race together, AI opponents and traffic synchronized, wins counted for both.

Known problems: a crash when the partner connects while the host is in the garage, a crash when a race
ends into the garage, and race series (sprint, circuit) can get out of step between the two players.
See [docs/HANDOFF-CODEX.md](docs/HANDOFF-CODEX.md).

## For developers

- `mod/`: the mod, an `xinput1_4.dll` proxy loaded by the runner (`mod/build.cmd`, clang-cl), and the
  launcher (`mod/launcher`). `mod/package.cmd` makes the local test package.
- `tools/`: test copies on a hidden desktop, muted, with their own ports and saves (`tools/xrun.py`,
  `tools/pair.py`), live tests, and memory tests (`tools/test_coop_memory.py`).
- `docs/`: reverse-engineering notes (`RE-*.md`) and test reports.
- `src/`, `CMakeLists.txt`: an earlier attempt on [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp).

## License

[MIT](LICENSE). Third-party code in `mod/third_party` keeps its own license.
