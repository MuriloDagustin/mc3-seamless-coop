# Resumo para o Codex — ponte nativa e troca de carro (20/10/2026)

## Objetivo

Continuar a investigação da troca de modelo do carro remoto sem reconectar e chegar a um resultado: feature funcionando ou prova do bloqueio.

## O que foi implementado

- `mod/src/native.cpp` / `native.h`: sonda opt-in `MC3COOP_NATIVE_PROBE=1` na thread do jogo (gancho XInput) e preflight seguro da troca (`MC3COOP_CAR_SWAP=1`).
- A sonda valida a build (`6AD54432`, SHA `51354a04…`), assinaturas de código, base guest, `guest record`, KPCR/KTHREAD e margem de stack. Chama funções guest pelo wrapper full-save `1403AD270` e compara EAX/EBX/ECX/EDX/ESI/EDI/EBP/ESP, x87, xmm0..7, FCW/MXCSR e flags emulados.
- `mod/src/car.cpp` / `car.h`: lista de slots (garagem/setup/live), `ModelPending`, estabilidade da configuração e cooldown da tentativa.
- `mod/src/control.cpp`: comandos de diagnóstico `cars`, `swapcar <índice>`, `swapfrom <slot setup>`, `localcar <slot setup>`.
- `mod/src/panel.cpp`: aviso no HUD/F10 quando há troca de modelo pendente.
- `tools/inspect_native_bridge.py` + `tools/tests/test_native_bridge.py`: inspeção estática do PE, `.pdata`, unwind, referências RIP e grafo de chamadas.
- `tools/tests/coop_memory_tests.cpp`: inclui `native.cpp` e stub de `mod::Env`.

## 1. ABI da ponte: validada ao vivo

Duas cópias isoladas (`tools/pair.py`, portas 45703/45704), desktop invisível, áudio mudo, runner em caminho fixo.

```
native: call guest=003308C0 argc=0 result=1 mismatch=0 esp=04927F8C
native: call guest=002BD910 argc=1 result=1 mismatch=0 esp=04927F8C
native: call guest=003308C0 argc=0 result=1 mismatch=0 esp=04927F8C (x3)
native: probe PASS
car: new world, local car ...
```

- `3308C0`: `mov eax,[5638E4]; ret` (getter puro).
- `2BD910`: `mov eax,[esp+4]; mov eax,[ecx+eax*4+8]; ret 4`, chamado com `ECX=[558814]` e argumento 0.
- Cinco chamadas no mesmo contexto; `mismatch=0`; o jogo continuou e carregou o mundo.
- Nenhum deadlock do mutex do callback foi observado. A análise estática já mostrou que o caminho direto do input (`300D00` → `3068D0`/`306BB0`) não referencia nem chama a aquisição do mutex (`3105B0`); 13 ancestrais diretos também não.

## 2. Troca instantânea de modelo: bloqueada pelo próprio release

Tentativa real de construir `vp_eclipse_04` no lugar de `vp_d_scion_tc_05` no host, com a sessão conectada:

```
native: model resolve ok=1 index=00000000 (vp_eclipse_04)
native: factory ctor=1 f04=vp_eclipse_04 f54=80E7AD00 fE8=1 fE1=00 fEC=1
native: factory build=1 f44=00000000 f4C=00000000 f50=837658E0
native: swap build failed (wrapper=00000000 carData=00000000)
```

Cadeia demonstrada:

1. `2CBC40` chama o método virtual `+20` (`2CB2D0`), que chama `105B80` com o modo offline (`arg4 = 0`).
2. O primeiro loop de `105B80` só reutiliza uma entrada existente quando `dl != 0` (modo rede). Com `dl = 0`, ele ignora as entradas existentes e procura uma livre; no caminho livre chama `105AA0`.
3. `105AA0` tenta criar o recurso de carro e o **próprio binário release** responde:

   `Cannot make car resource on RELEASE - build on Beta/debug or ask Yomal why they are not in the assets!`

   e retorna 0.
4. `105B80` devolve 0; `2CBC40` continua com produto nulo e chama métodos virtuais inválidos. O runner registra `[ICALL] unresolved target` em `2CBD56/2CBD5D/2CBD64/2CBDCB` e encerra.

### Evidência do pool `[50A08C]`

Pool de 16 entradas, 16 bytes cada. Na ronda, as entradas em uso eram:

```
índice 00  carData 83295380  modo 1  (vp_eclipse_04)
índice 05  carData 83335380  modo 1  (vp_montecarlo_78)
índice 0A  carData 833C7B00  modo 1  (vp_gs430_04)
índice 45  carData 83471780  modo 0  (vp_d_scion_tc_05, o carro em uso)
```

O modo 1 é do pool de garagem/showroom e não é reutilizado pelo modo offline. Só o modelo em uso tem entrada modo 0.

**Conclusão:** a troca instantânea de modelo em mundo carregado é impossível nesta build release. Não é limitação da ponte; é ausência do criador/assets de recurso de carro no release.

## 3. Troca sem reconectar pelo próximo carregamento: funciona

Fluxo testado ao vivo:

1. O host estava em ronda, sessão `COOP 2/8 CONNECTED`.
2. No parceiro, a entrada selecionada da garagem foi trocada para `vp_eclipse_04` pelo comando `localcar 2` (simula a escolha da garagem; não é a UI real).
3. O host recebeu a configuração e manteve `live slot cars: vp_d_scion_tc_05 vp_d_scion_tc_05`; `car_model_pending=1`.
4. O host carregou uma corrida normal com `gorace`, sem reconectar.
5. Depois da carga: `live slot cars: vp_d_scion_tc_05 vp_eclipse_04`; `current/next slot cars` com Eclipse na vaga 1.
6. As duas cópias permaneceram conectadas e na mesma corrida (`serial=1`).

Esse é o caminho suportado atual: a configuração diferente fica em NEXT e é aplicada na próxima carga normal (corrida ou garagem), sem reconectar. Não é instantânea no mundo atual.

## 4. Código final da troca

`native::SwapRemote` final **não chama o construtor**: valida build/contexto/mundo/jogador/configuração e recusa antes de `2CB270`/`2CBC40`, para não repetir o encerramento. Ele registra que o modelo foi staged para a próxima carga. O `car::Tick` continua aplicando o mesmo modelo uma vez por revisão e mantém o modelo diferente em NEXT.

## 5. Testes executados

- `python tools/inspect_native_bridge.py D:\dev\mc3-coop-data\xrun\MC3Runner.exe`: PASS (build `6AD54432`).
- `python -m unittest discover -s tools/tests -p test_native_bridge.py -v`: 12 PASS.
- `python tools/test_coop_memory.py`: PASS com `native.cpp` incluído.
- `cmd /c mod\build.cmd`: PASS.
- Testes ao vivo com duas cópias descartáveis; sem criar regra de firewall, sem UAC, sem tocar no jogo/xrevive do usuário. Cópias de teste encerradas ao final.
- Commits locais `a5c62d8` e `a588116`, sem push e sem ZIP novo.

## 6. Perguntas para o Codex analisar

1. **O criador de recurso existe no release?** A string em `0x455230` e o fluxo `105AA0` → `2105F0`/`2126C0` indicam que não. Confirmar se é exclusão de compilação ou apenas assets ausentes; se houver flag global/arquivo que habilite, qual?
2. **É viável construir com carData modo 1?** `105B80` reutiliza uma entrada modo 1 quando chamado com `arg4=1`. Isso exigiria reproduzir o `2CBC40` sem o passo `+20`: criar o custom (`F4430`), adquirir o carData modo 1, chamar os métodos virtuais restantes e registrar no manager. Falta decodificar com precisão os argumentos de `F4430` em `2CBC6A..2CBCA0`. O modo 1 é equivalente em gameplay (física/visual/áudio) ou só showroom?
3. **Há caminho de carga sem reconectar mais suave?** O fluxo NEXT funciona em corrida/garagem. Vale a pena automatizar uma transição controlada de garagem quando há modelo pendente, ou isso destrói estado de corrida/série?
4. **UI real da garagem:** o teste usou `localcar`; validar a seleção real do parceiro, a leitura de `LocalCarCfg` e o envio no timing da UI.
5. **Outros modelos:** o teste usou Eclipse, que era modelo de adversário/garagem e pôde ser carregado no mundo. Testar um modelo que não aparece no setup da próxima corrida para confirmar que o NEXT ainda o carrega no load normal.
