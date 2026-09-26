# Banco di prova gxm-optimization

Branch `experiment/gxm-optimization-testbed`. Il submodule `smstrikers-port/extern/aurora-vita`
ha un remote `local` che punta a `~/Documents/Code/PSVita/aurora-vita` (branch
`gxm-optimization`); il piano delle patch è in
`platforms/vita/gxm/GXM_OPTIMIZATION_AUDIT.md` di quel repository.

## Build

```sh
./build_gxm_testbed.sh <revisione-aurora> <etichetta>
```

Stesse opzioni di `build_latest_aurora_gxm.sh` (GXM, no log, 60 Hz, direct stream write/submit).
Gli artefatti (VPK, SELF, ELF, `BUILD_INFO.txt` con revisione e SHA-256) finiscono in
`ab-artifacts/<etichetta>/` (ignorata da git). Una build alla volta: il submodule è condiviso.

| Etichetta | Aurora | Contenuto |
|---|---|---|
| `baseline-f96d00b` | `f96d00b` | riferimento (experiment/vita-native-gxm) |
| `opt-phase1` | `d2bb2b4` | contatori, G1/G3 depth load, G2 scene senza depth, S1 variante senza discard, M2 display copy |
| `opt-phase2` | `6a9a2b8` | + C1 stato persistente tra scene, C2 fast path texture, C8 |
| `opt-phase3` | `4b94a17` | + S2 wrap hardware, S4 copy mode/opacità come bit pipeline |

## Esecuzione su Vita

1. Installare il VPK (stesso title id `SMSVITA01`: sostituisce l'installazione corrente;
   conservare l'eboot attuale). Verificare l'hash dell'eboot installato.
2. `ux0:data/strikersVita/strikers.ini` solo per il test (conservare l'originale):

   ```ini
   benchmark = 1
   benchmark_seconds = 150
   bench_record = ux0:data/strikersVita/bench-<etichetta>-<n>.csv
   ```

3. Per ogni build **due avvii**: il primo compila/salva shader e manifest (S4 cambia la
   dimensione di `PipelineDesc`, quindi il manifest hot viene scartato e ricostruito una volta);
   si confronta il **secondo** avvio. Nell'intestazione del CSV `shader_blocked` deve essere 0.
4. Scaricare i CSV. L'intestazione `#` contiene i totali della partita:
   `frame_us`, `renderer_cpu_us`, `dq_avg_us`, `scenes`, `gxm_depth_load`, `gxm_depth_store`,
   `gxm_depthless`, `gxm_finish`, `gxm_scissor_free_draws` (la baseline riporta 0 per i
   contatori `gxm_*`, che non esistono in quella revisione). Le righe sono i tempi per frame.
5. Controllo visivo per ogni build: ombre, UI/HUD, riflessi e texture ripetute del campo,
   presentazione/letterbox, transizioni di menu e pausa. Qualsiasi differenza rispetto alla
   baseline blocca la patch corrispondente.
6. Ripristinare `strikers.ini` e l'eboot originale a fine sessione.

Cosa aspettarsi nei contatori (non sono misure di velocità):
- `gxm_depth_load` molto sotto `gxm_scenes` rispetto a prima (G1/G3);
- `gxm_depthless` ≥ 1 per frame se la presentazione passa da blit/display copy (G2);
- `gxm_scissor_free_draws` vicino al numero di draw (S1);
- `gxm_finish` vicino a 0 durante la partita.
