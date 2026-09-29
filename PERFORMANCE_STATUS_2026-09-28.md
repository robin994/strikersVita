# Strikers Vita: stato prestazioni e piano verso 60 FPS

Data del report: 2026-09-28. Questo documento fotografa il repository e gli artefatti locali alla data indicata; non è una misura in tempo reale della console.

## Identità del checkout

- Repository: `strikersVita`, branch `main`, commit `2cc7b1b2ce665f7fabce06e7cd9b9b6fdf0426d7`.
- Submodule `smstrikers-port/extern/aurora-vita`: `4cca118c041f71fbd4e0700918d4225cb0a4427c`.
- La directory non tracciata `.codex/` era già presente e non è stata modificata per questo report.
- Il target di 60 FPS richiede circa 16,67 ms per frame (16,68 ms a 59,94 Hz). Il target non è stato raggiunto né verificato in gameplay sulla Vita.
- Non è disponibile negli artefatti esaminati un benchmark hardware completo della combinazione esatta di `main` e submodule sopra indicati. Le misure seguenti riguardano build precedenti, con revisioni e configurazioni documentate.

## Evidenza hardware disponibile

I valori di mediana e p95 qui sotto sono calcolati dalle righe `frame_us` dei CSV, non dal campo riassuntivo `# frame_us` (che è uno snapshot del renderer). P95 indica che il 95% dei frame del campione non supera il valore riportato. I run non costituiscono tutti un A/B controllato: cambiano revisioni, durata o opzioni.

| Run | Frame registrati | Mediana frame | P95 frame | Configurazione e interpretazione |
| --- | ---: | ---: | ---: | --- |
| [`opt-psfix-1`](ab-artifacts/results/bench-opt-psfix-1.csv) | 4.173 | 62,3 ms | 128,8 ms | Aurora `4cca118`, Strikers `1cad08c0`, `gxm_disable=0`, Release/WARM, GX thread OFF; il migliore riferimento disponibile vicino alla revisione Aurora attuale. |
| [`nolog-fog-6`](ab-artifacts/results/bench-nolog-fog-6.csv) | 630 | 84,6 ms | 141,2 ms | Aurora `63f12bd7`, `gxm_disable=0xff`, GX thread OFF; controllo con otto gruppi di ottimizzazioni disabilitati. GX mediano 33,6 ms e p95 100,2 ms. |
| [`nolog-fog-gxthread-1`](ab-artifacts/results/bench-nolog-fog-gxthread-1.csv) | 590 | 75,5 ms | 200,6 ms | Aurora `63f12bd7`, `gxm_disable=0xff`, GX thread ON; mediana inferiore al controllo, ma p95 peggiore. Non dimostra un guadagno stabile. |
| [`nolog-gxthread2-1`](ab-artifacts/results/bench-nolog-gxthread2-1.csv) | 525 | 84,9 ms | 212,3 ms | Seconda build sperimentale con GX thread; p95 ancora elevato. |

Nel run `opt-psfix-1`, nessun frame registrato rientra nel budget di 16,67 ms; l'intestazione riporta `shader_blocked=0` e `shader_gameplay=0`. Il log del dispositivo conferma CPU 444 MHz, GPU 222 MHz e `gxm_disable_mask=0x0`: [`runtime.log`](ab-artifacts/results/opt-psfix-1/runtime.log). Le revisioni della build sono in [`BUILD_INFO.txt`](ab-artifacts/opt-psfix/BUILD_INFO.txt); l'SHA-256 dell'`eboot` installato è annotato nel CSV.

Nel run `nolog-fog-6` il log conferma gli stessi clock, ma `gxm_disable_mask=0xff`: [`runtime.log`](ab-artifacts/results/nolog-fog-6/runtime.log). I bit `0xff` disabilitano politica depth, variante scissor, stato persistente, snapshot fixed vertex, revisioni uniform, wrap nativo, eviction geometria e shadow delle display list; la mappa dei bit è in [`vita_gfx_types.hpp`](smstrikers-port/extern/aurora-vita/platforms/vita/gfx/vita_gfx_types.hpp). Per questo motivo 84,6 ms non è la prestazione della configurazione ottimizzata corrente.

## Colli di bottiglia osservati e incertezze

- Negli ultimi intervalli stabili di 120 frame di [`task_profile-fog-6.log`](ab-artifacts/results/task_profile-fog-6.log), `render-profile send_views` è circa 48,5–49,6 ms medi per frame. `Game Fixed Update` è circa 16,1–21,7 ms medi; `Game Render` circa 6,7–7,2 ms. Sono medie di intervalli, non percentili per frame.
- Nel CSV `nolog-fog-6`, GX processing ha mediana 33,6 ms; è un sottoinsieme del lavoro del frame. I tempi task, render e GX possono essere annidati o sovrapposti e **non vanno sommati**.
- `submit_*_us` misura la sottomissione CPU, non l'esecuzione GPU. L'attesa della display queue segnala possibile contropressione ma non attribuisce da sola il costo alla GPU. I campioni diagnostici per scena richiedono un controllo dell'overhead prima di essere usati come baseline di throughput.
- I contatori di cache e le ottimizzazioni GXM implementate non certificano equivalenza visiva. Ombre, HUD, riflessi, texture ripetute del campo, transizioni e stabilità devono essere controllati su hardware per ogni modifica che può alterare la resa.
- [`run-nolog-streamgpu.log`](ab-artifacts/run-nolog-streamgpu.log) registra un timeout senza CSV per `nolog-streamgpu-1`; non è una misura FPS valida e richiede diagnosi separata prima di abilitare quel percorso.

## Stato del banco di prova

Il benchmark avvia direttamente una demo AI contro AI con Mario, Luigi e stadio Mario fissati in [`SHTitleScreen.cpp`](smstrikers-port/src/Game/SH/SHTitleScreen.cpp). [`benchmark.c`](smstrikers-port/src/platform/benchmark.c) conserva campioni per frame e inizia la raccolta dopo l'ingresso nella partita. [`vita_gxm_ab.sh`](vita_gxm_ab.sh) installa l'`eboot`, ne verifica l'hash con lettura di ritorno e raccoglie CSV/log. [`vita_bench_watch.sh`](vita_bench_watch.sh) può rilevare un'immagine OBS ripetuta; un'immagine uniforme senza segnale non conta come freeze. La procedura esistente è descritta in [`GXM_TESTBED.md`](GXM_TESTBED.md).

La baseline va ripetuta dopo le ultime modifiche del testbed e sul commit esatto indicato sopra. I vecchi run possono orientare le ipotesi, ma non sostituiscono quella misura.

## Piano operativo, in ordine

### 1. Baseline riproducibile del checkout attuale

1. Costruire una Release senza logging diagnostico continuo, con Aurora `4cca118`, `gxm_disable=0`, GX thread OFF e configurazione registrata. Conservare VPK, SELF, ELF, mappa, revisioni e SHA-256.
2. Verificare sul dispositivo l'hash dell'`eboot` installato, i clock effettivi e la configurazione caricata. Fissare CPU 444 MHz e GPU 222 MHz per questo primo confronto; una misura con clock standard richiederà un profilo separato.
3. Fare un primo avvio di warmup shader, poi almeno tre run misurati della demo fissa di 120–150 secondi. Escludere il transitorio iniziale della partita dall'analisi; annotare il numero di frame esclusi. Verificare `shader_blocked=0`.
4. Archiviare per ogni run mediana, p95, p99, media, quota di frame ≤16,67 ms, scene, eventuali freeze e screenshot della stessa scena. Usare il secondo avvio e lo stesso criterio di campionamento per ogni A/B successivo.

**Criterio di uscita:** baseline completa e ripetibile, con differenze tra run spiegate o quantificate. Non usare un singolo run anomalo come riferimento.

### 2. Attribuire il costo del rendering

1. Profilare `send_views` e GX per fasi: decode/trasformazione vertici, risoluzione texture e pipeline, costruzione comandi, submit e attese. Controllare `nativeTimingsSampled` e registrare la frequenza di campionamento.
2. Raccogliere tempi GPU per scena e, se utile, una bisezione per pass/draw. Confrontare una build diagnostica con la stessa build senza diagnostica per stimare l'overhead. Separare il tempo GPU effettivo dall'attesa CPU sulla coda.
3. Correlare i picchi p95/p99 ai pass e ai task del gioco, non solo alla mediana del renderer.

**Criterio di uscita:** una classifica dei costi CPU e GPU con evidenza su hardware, sufficiente a scegliere la prima modifica. L'[`audit GXM`](smstrikers-port/extern/aurora-vita/platforms/vita/gxm/GXM_OPTIMIZATION_AUDIT.md) è un elenco di candidati, non una classifica di guadagni misurati.

### 3. Ottimizzare il percorso dominante, una modifica per volta

- Se domina la CPU GX: esaminare draw ripetuti, snapshot/uniform fixed vertex, percorso display list e cache, misurando il tempo della fase e dell'intero frame.
- Se domina una scena GPU: identificare il pass e i draw costosi prima di modificare depth, scissor, shader o risoluzione. Mantenere un controllo visivo e un percorso di fallback per le modifiche che toccano la semantica GX.
- Profilare `Game Fixed Update` indipendentemente: i 16–22 ms medi osservati possono da soli consumare l'intero budget. Misurare i sottosistemi interni prima di parallelizzare o cambiare frequenze di update.
- Ripetere il test GX thread in un A/B a parità di revisione e condizioni; il p95 disponibile non giustifica ancora abilitarlo per default. Diagnosticare il timeout streamed GPU prima di ulteriori confronti su quel percorso.

**Criterio di uscita per ogni patch:** stessa scena e configurazione, almeno tre run A/B, miglioramento credibile di mediana e coda p95/p99, nessun nuovo freeze o difetto visivo. Conservare i CSV, hash e screenshot associati alla patch.

### 4. Gate del target

Richiedere gameplay sostenuto vicino a 59,94/60 FPS con p95 del frame entro circa 16,67 ms, p99 e picchi documentati, cache shader calda, e verifiche visive su più fasi di gioco. Validare poi stabilità prolungata e clock/temperatura della console. Fino a quel momento, distinguere sempre throughput misurato, correttezza visiva verificata e ipotesi di ottimizzazione.

## Prossima azione consigliata

Eseguire la baseline della `main` attuale con `gxm_disable=0`. È il dato mancante che permette di decidere se intervenire prima su `send_views`, sul lavoro GPU per scena o su `Game Fixed Update` senza confondere le misure dei controlli `0xff` con il percorso ottimizzato.
