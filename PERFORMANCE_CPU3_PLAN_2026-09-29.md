# Strikers Vita: stato CPU e piano per il quarto core con limite del 70%

Data: **29 settembre 2026**, Europe/Rome. Raccolta dalla console: 28 settembre 2026, 23:18 UTC (29 settembre, 01:18 locali).

Scopo: analisi dello stato attuale e specifica delle modifiche da implementare per ridurre il tempo CPU e avvicinare il rendering a 60 FPS. Questo documento non certifica l'utilizzo del quarto core: il codice proposto non è stato implementato e non è stato eseguito un nuovo benchmark durante l'analisi.

## 1. Conclusione operativa

Il costo CPU del rendering è il primo candidato su cui intervenire. Le prove disponibili supportano questa priorità, ma **non dimostrano ancora che la build installata sia esclusivamente CPU bound né che i tre core applicativi siano saturi**. I tempi `busy` e `send_views` sono tempi trascorsi, che possono contenere attese di worker, sincronizzazioni e GPU.

CapUnlocker è presente nella configurazione kernel della console. Il port continua però a usare CPU0, CPU1 e CPU2: non esiste un worker su CPU3, né un controllo della sua occupazione. L'intervento consigliato è aggiungere **un terzo helper CPU opportunistico**, mantenendo inizialmente GX/GXM sincrono su CPU0. CPU3 riceverà solo lavoro indipendente, in blocchi brevi, entro un budget adattivo. Il limite del 70% va misurato sull'occupazione totale del core, includendo sistema e plugin, e definito su finestre temporali.

Il quarto core da solo non giustifica una previsione di 60 FPS. Nell'ipotesi illustrativa di tre core già completamente utilizzati e lavoro perfettamente divisibile, aggiungere 0,7 core passa da 3 a 3,7 core equivalenti: +23,3% di capacità, oppure -18,9% di tempo per la sola parte parallelizzabile. Se CPU3 ospita già lavoro di sistema, il margine è inferiore. È un modello ideale di capacità, non un limite universale di speedup né una misura del port. I vecchi 62,3 ms mediani richiederebbero circa 3,74 volte il throughput per arrivare a 16,67 ms: serviranno anche meno lavoro per frame, meno attese e meno costi seriali.

## 2. Stato del checkout e dell'installazione

| Elemento | Evidenza verificata |
| --- | --- |
| Strikers | `main`, HEAD `2cc7b1b2ce665f7fabce06e7cd9b9b6fdf0426d7`, con modifiche locali non committate |
| Aurora incorporato | `smstrikers-port/extern/aurora-vita`, HEAD `4cca118c041f71fbd4e0700918d4225cb0a4427c`, anch'esso modificato localmente |
| Eseguibile installato | `ux0:app/SMSVITA01/eboot.bin`, 3.419.153 byte, SHA-256 `de5a1ea7011524f56c19ed424d422310b69ee3751bdd798d44c51dacce5b319a` |
| Corrispondenza locale | Hash identico a `smstrikers-port/build-vita-gxm-latest/strikers_vita.self`, file prodotto il 28 settembre alle 18:01 locali |
| Cache CMake della build locale | `GX_THREAD=OFF`, `AURORA_VITA_ASYNC_GX=OFF`, `AUDIO_THREAD=ON`, `NO_LOGS=ON`, `PROFILER=OFF`, shader default `SEALED` |
| CapUnlocker | Riga `ur0:tai/CapUnlocker.skprx` nella sezione `*KERNEL` di `ur0:tai/config.txt` |

L'hash conferma i byte installati, non ricostruisce da solo l'esatto insieme di sorgenti utilizzato dalla build. Le revisioni Git senza i diff non identificano questo checkout. Il manifest della raccolta conserva hash dei diff e copie delle modifiche di entrambi i repository: [collection.json](/Users/robin994/Documents/Code/strikersVita/ab-artifacts/review-20260929-core3/collection.json). Non è stato eseguito un probe di affinità: la configurazione di CapUnlocker non dimostra ancora che CPU3 sia accessibile al processo corrente.

Rispetto al [report del 28 settembre](/Users/robin994/Documents/Code/strikersVita/PERFORMANCE_STATUS_2026-09-28.md), le modifiche locali hanno:

- rimosso la modalità benchmark automatica, il percorso demo deterministico e l'esportazione CSV; [benchmark.c](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/src/platform/benchmark.c) conserva il monitoraggio per l'overlay su una finestra di 256 frame;
- introdotto una schermata di avanzamento durante il prewarm shader;
- reso il bit `0x10` capace di disabilitare tutto il riuso delle uniform fragment;
- sostituito il rilascio della geometria ritirata con una distruzione che rispetta il flag GPU `inFlight`. Questo può introdurre un `finish()` e deve essere misurato; non va annullato senza una prova alternativa della durata delle risorse.

Il vecchio [vita_gxm_ab.sh](/Users/robin994/Documents/Code/strikersVita/vita_gxm_ab.sh) attende CSV che il working tree attuale non produce più. Il [build_gxm_testbed.sh](/Users/robin994/Documents/Code/strikersVita/build_gxm_testbed.sh) rifiuta un submodule modificato. Per la futura baseline bisogna costruire il working tree preservato, registrandone i diff, oppure preparare una revisione che includa esplicitamente queste modifiche; non basta passare il vecchio SHA allo script A/B.

### Configurazione letta dalla console

Il file [strikers.ini acquisito](/Users/robin994/Documents/Code/strikersVita/ab-artifacts/review-20260929-core3/strikers.ini) contiene:

```ini
benchmark = 1
benchmark_seconds = 60
bench_record = ux0:data/strikersVita/bench-nolog-gxthread2-1.csv
cpu_mhz = 444
gpu_mhz = 222
gxm_shader_profile = WARM
gxm_streamed_vertex_gpu = 0
gxm_disable = 0x18
task_profile = 1
gxm_dl_shadow = 0
```

Le prime tre opzioni sono residue e non ripristinano il benchmark rimosso. Il nome del CSV non prova che il thread GX sia attivo. `0x18` disabilita snapshot fixed vertex e riuso uniform fragment; `gxm_dl_shadow=0` disabilita separatamente le shadow delle display list. Sono possibili costi CPU aggiuntivi, senza un guadagno recuperabile già quantificato.

Questa è la configurazione su disco: va confermata al prossimo avvio con un manifest runtime. Il `runtime.log` presente sulla console è vecchio: la lista FTP lo data al 27 settembre e il contenuto riporta `gxm_disable=0xff`. Non è una prova della configurazione effettiva del binario attuale. Analogamente 444/222 MHz sono richiesti dall'INI, ma in questa analisi non sono stati misurati i clock live. Il default CMake `SEALED` può essere sovrascritto dall'INI `WARM`.

## 3. Prestazioni: cosa si può concludere

### Misure storiche con campioni per frame

Ricalcolate dai CSV archiviati del 27 settembre. Per coerenza con il vecchio reporter, mediana e percentili usano il campione ordinato agli indici `n/2`, `floor(0,95*n)`, `floor(0,99*n)`; nessun nuovo warmup è stato escluso in questa rianalisi.

| Run | Frame | Mediana | P95 | P99 | Limite di interpretazione |
| --- | ---: | ---: | ---: | ---: | --- |
| [opt-psfix-1](/Users/robin994/Documents/Code/strikersVita/ab-artifacts/results/bench-opt-psfix-1.csv) | 4.173 | 62,292 ms | 128,826 ms | 255,584 ms | Aurora `4cca118`, Strikers precedente, `gxm_disable=0`, GX sincrono |
| [nolog-fog-6](/Users/robin994/Documents/Code/strikersVita/ab-artifacts/results/bench-nolog-fog-6.csv) | 630 | 84,571 ms | 141,154 ms | 189,511 ms | Build diversa, `gxm_disable=0xff`, GX sincrono |
| [nolog-fog-gxthread-1](/Users/robin994/Documents/Code/strikersVita/ab-artifacts/results/bench-nolog-fog-gxthread-1.csv) | 590 | 75,519 ms | 200,574 ms | 264,143 ms | GX asincrono sperimentale: coda dei tempi peggiore |

In `opt-psfix-1`, `busy_us` mediano è 61,516 ms contro 62,292 ms totali; in `nolog-fog-6` GX mediano è 33,607 ms e la display queue mediana 0,086 ms. Questi indizi indirizzano l'indagine verso il lavoro prima della presentazione. Non separano l'esecuzione CPU dalle attese interne. Non sommare misure annidate, né chiamare `submit_us` un tempo GPU.

### Profilo recuperato oggi dalla console

[task_profile.log](/Users/robin994/Documents/Code/strikersVita/ab-artifacts/review-20260929-core3/task_profile.log) contiene 78 blocchi ed è un file append senza ID di sessione o hash del binario per blocco. Le ultime quattro finestre da 120 frame riportano:

| Fase | Intervallo delle medie nelle ultime quattro finestre |
| --- | ---: |
| End Frame | 49,145–49,160 ms |
| send_views, incluso nel percorso di rendering | 48,694–48,712 ms |
| Game Render | 5,353–5,383 ms |
| Game Fixed Update | circa 0,011 ms |
| Task Audio sul thread chiamante | 0,039–0,047 ms |

Il fixed update quasi nullo non prova un gameplay attivo; potrebbe trattarsi di una fase senza simulazione. Non convertire questi blocchi in un dato di FPS sostenuti della partita né associare automaticamente tutta la coda alla build installata. Il task Audio non misura il costo completo del worker MusyX. Il [riepilogo dei blocchi](/Users/robin994/Documents/Code/strikersVita/ab-artifacts/review-20260929-core3/task-profile-summary.json) conserva i valori estratti.

Servono quindi contatori di tempo realmente eseguito per thread, tempo in attesa, utilizzo dei singoli core, numero di tick di simulazione per frame e stato del gioco. L'ipotesi CPU bound diventa verificabile quando il lavoro CPU o la relativa catena di dipendenze spiega il frame, mentre le attese GPU non lo dominano.

## 4. Distribuzione attuale del lavoro

Topologia derivata dal codice e dalle opzioni della build locale, da confermare a runtime:

| Core | Lavoro attuale | Evidenza |
| --- | --- | --- |
| CPU0 | Main, simulazione, emissione GX, GX/GXM sincrono, quota del caller nei job paralleli | [main.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/src/Game/main.cpp), affinità `USER_0`, GX thread OFF |
| CPU1 | Worker audio e secondo helper Aurora a priorità bassa | [audio_out.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/src/platform/audio_out.cpp), [vita_cpu_workers.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/extern/aurora-vita/platforms/vita/gfx/vita_cpu_workers.cpp) |
| CPU2 | Primo helper Aurora | `USER_2`, priorità `0x10000110` nel pool |
| CPU3 | Nessun worker del port; carico sistema/plugin non misurato | Nessuna affinità `SYSTEM` nella topologia del port |

Il secondo helper usa priorità `0x10000180`; il pool attende i risultati con semafori. Quando abilitato, il thread GX di [fifo.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/extern/aurora-vita/lib/gx/fifo.cpp) è creato su **CPU1**, non su CPU2 come suggerisce un commento in `main.cpp`. Abilitarlo senza riprogettare la topologia aggiunge competizione con audio e helper su CPU1. Il pool condiviso ha inoltre un flag `busy` che fa eseguire sul caller le chiamate concorrenti o ricorsive: il GX asincrono non crea automaticamente due pipeline di preparazione pienamente parallele.

### Ostacoli concreti al quarto core

1. `MaxWorkers=2` nel pool: massimo caller più due helper.
2. `main.cpp` accetta `STRIKERS_AURORA_CPU_WORKERS` solo da 0 a 2 e imposta `cpu_renderer_execution_lanes=3`.
3. Le affinità dei worker sono cablate a CPU2 e CPU1; non c'è probe CPU3 o fallback specifico.
4. `FusedVertexContext::error` e `StreamedVertexContext::error` in [vita_draw_adapter.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/extern/aurora-vita/platforms/vita/gfx/vita_draw_adapter.cpp) hanno tre elementi. `DecodeContext::badVertex` in [vita_vertex_decode.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/extern/aurora-vita/platforms/vita/gfx/vita_vertex_decode.cpp) ha tre elementi. Il quarto partecipante, lane logica 3, viene respinto dalle guardie: aumentare soltanto il numero dei thread può far fallire il rendering.
5. `parallel_for(10, 3, ...)` dei personaggi consente al massimo `floor(10/3)=3` partecipanti. `parallel_for(170, 48, ...)` degli oggetti ne consente ugualmente tre. Anche con quattro lane, un draw da 200 vertici e minimo 64 resta a tre. Il semplice aumento del pool non sposta questi carichi sul nuovo core.
6. La partizione attuale assegna intervalli statici e poi attende tutti i worker. Un worker CPU3 che dorme per rispettare la quota dopo aver ricevuto un quarto fisso del lavoro può diventare il thread che rallenta l'intero frame.

## 5. Come usare CPU3 rispettando il requisito del 70%

### Accesso e probe

CapUnlocker rimuove il controllo che impedisce certe affinità; non ripartisce il lavoro del gioco e non offre una quota percentuale. Il sorgente ufficiale è [CapUnlocker/main.c](https://github.com/GrapheneCt/CapUnlocker/blob/master/main.c). VitaSDK definisce `SCE_KERNEL_CPU_MASK_SYSTEM` come **`0x00080000`**, mentre `USER_ALL` comprende soltanto CPU0–CPU2: [cpu.h](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/kernel/cpu.h).

All'avvio del profilo CPU3, creare un piccolo worker di prova con quella maschera, controllare il risultato di create/start, leggere nuovamente l'affinità e verificare dal worker `sceKernelGetCpuId()==3`. Registrare i risultati. In caso di rifiuto o telemetria non valida, mantenere il pool a due helper e motivare il fallback nel manifest. L'installazione dichiarata dall'utente e la riga in `config.txt` sono già acquisite; il probe serve a misurare l'accesso effettivo.

### Definizione misurabile del limite

Interpretazione prudente del requisito: **occupazione totale CPU3 ≤70% su finestra mobile di 100 ms, con controllo anche su 1 secondo**. La scelta delle finestre è una proposta progettuale. Un core è istantaneamente occupato o inattivo: un tetto percentuale istantaneo non è definibile. Il processo Strikers può limitare il proprio lavoro, ma non può impedire a sistema o altri plugin di superare il 70% da soli. Il limite totale è quindi un obiettivo verificato dalla telemetria, non una garanzia hard real-time contro carico esterno arbitrario.

Usare i contatori `cpuInfo[3].idleClock` di `sceKernelGetSystemInfo`, il runtime dei thread tramite `sceKernelGetThreadInfo` e un clock monotono. Le strutture e le API sono dichiarate da [thread.h](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/kernel/threadmgr/thread.h) e [threadmgr.h](https://github.com/vitasdk/vita-headers/blob/master/include/psp2common/kernel/threadmgr.h). Validare disponibilità e unità sulla console: conservare i valori grezzi e verificare la conversione con intervalli controllati di esecuzione e attesa. Non dividere automaticamente `runClocks` per la frequenza ARM basandosi sul nome del campo.

Dopo normalizzazione a unità compatibili:

```text
U_total3 = 1 - delta_idle3 / delta_wall
U_app3   = somma(delta_runtime dei thread Strikers fissati a CPU3) / delta_wall
U_other3 = max(0, U_total3 - U_app3)
budget_Strikers = max(0, 0.70 - stima_conservativa(U_other3) - margine)
```

Margine iniziale proposto: 5 punti percentuali. Esempio puramente illustrativo: con sistema/plugin al 15%, assegnare al massimo circa il 50% a Strikers, puntando al 65% totale e lasciando 5 punti di margine. Dare il 70% al solo worker non rispetterebbe un limite totale del 70%.

Il budget deve seguire il tempo reale, non il numero di frame: a 60 FPS il 70% di un frame equivale a 11,67 ms, ma assegnare sempre 11,67 ms per frame a una build lenta produce un'altra percentuale. La telemetria di utilizzo non deve dipendere dal completamento di un frame lungo; aggiornare il controllo almeno ai confini dei chunk e con campionamento regolare, inizialmente circa ogni 10 ms, misurandone l'overhead.

### Scheduler proposto

- CPU0 resta proprietario di GX/GXM e degli aggiornamenti che modificano stato globale; CPU1 mantiene audio e helper a priorità bassa; CPU2 mantiene l'helper principale; CPU3 aggiunge un helper a priorità bassa. La priorità consente la preemption, ma da sola non limita il carico al 70%.
- CPU3 prende solo chunk indipendenti e già preparati. Obiettivo iniziale di durata per chunk: 0,25–0,5 ms, da tarare per tipo di job. Usare una previsione conservativa, prenotare budget prima del dispatch, contabilizzare il costo reale e portare eventuale debito alla finestra successiva. Registrare ogni sforamento.
- I job su CPU3 hanno input immutabili, destinazioni disgiunte e storage preallocato. Escludere API GX/GXM, scritture di cache condivise, allocator legacy, I/O e mutazioni globali della fisica da quei callback.
- Quando il budget è esaurito, il lavoro non assegnato resta disponibile al caller o agli helper CPU1/CPU2. CPU3 segnala il completamento del lavoro già preso e si sospende senza busy-wait. Non deve dormire tenendo in esclusiva un intervallo necessario al completamento del frame.
- Per ottenere questo comportamento, introdurre assegnazione di chunk tramite indice atomico/queue limitata, anziché un quarto statico del batch per CPU3. Non spostare un chunk già in esecuzione: il chiamante attende il suo completamento prima di riutilizzare contesti e buffer.
- Conservare pubblicazione release/acquire e una conferma di completamento per ogni job effettivamente pubblicato. Separare la lane logica dal core fisico e usare slot distinti per risultati/errore, evitando anche false sharing dove misurabile.
- Su telemetria assente, counter reset o occupazione totale sopra soglia, fermare nuovi dispatch a CPU3 e lasciare il lavoro ai tre core originari. Se il carico esterno supera da solo il 70%, segnalarlo esplicitamente e non certificare il limite totale.

La quota cooperativa può sforare fino al completamento di un chunk e non controlla i picchi del sistema. Per un vincolo rigido servirebbero limiti dimostrabili sulla durata dei chunk e supporto di scheduling ulteriore. Nel report dei test indicare finestra, massimi osservati, durata e causa degli sforamenti; non dichiarare semplicemente «CPU3 al 70%».

## 6. Modifiche da implementare, in ordine

| Priorità | Modifica concreta | File principali | Criterio di completamento |
| --- | --- | --- | --- |
| P0 | Sessioni di profiling attribuibili alla build e al gameplay: hash, config effettiva, clock, stato, ID frame, tick fisica, tempi per thread e per core | [benchmark.c](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/src/platform/benchmark.c), [benchmark.h](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/include/port/benchmark.h), [vita_profiler.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/src/platform/vita_profiler.cpp), [nlTask.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/src/NL/nlTask.cpp), [glPlat.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/src/NL/gl/glPlat.cpp) | Nuova baseline della build attuale; campioni per frame esportabili senza cambiare il normale avvio del gioco; overhead quantificato |
| P1 | Opzione CPU3, probe e mappa di affinità esplicita; estendere parser a 0–3 helper e cap renderer fino a quattro lane | [main.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/src/Game/main.cpp), [aurora_vita_backend.hpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/extern/aurora-vita/platforms/vita/aurora_vita_backend.hpp), [aurora_vita_backend.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/extern/aurora-vita/platforms/vita/aurora_vita_backend.cpp), [vita_cpu_workers.hpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/extern/aurora-vita/platforms/vita/gfx/vita_cpu_workers.hpp) | Core ID 3 verificato, errori gestiti, fallback 3 core funzionante |
| P1 | Capacità condivisa `MaxExecutionLanes=4`, estensione di tutti gli array per lane e delle inizializzazioni di errore | [vita_cpu_workers.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/extern/aurora-vita/platforms/vita/gfx/vita_cpu_workers.cpp), [vita_draw_adapter.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/extern/aurora-vita/platforms/vita/gfx/vita_draw_adapter.cpp), [vita_vertex_decode.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/extern/aurora-vita/platforms/vita/gfx/vita_vertex_decode.cpp) | Lane 3 accettata e risultati identici al percorso seriale; nessun array a tre elementi usato come storage per lane |
| P2 | Budget CPU3 e assegnazione dinamica di chunk con quota sul tempo reale | Pool worker più un piccolo modulo di budget/telemetria, da creare vicino a `vita_cpu_workers.cpp` | Copertura esatta del lavoro, completamento senza aspettare il rinnovo della quota, limiti e sforamenti registrati |
| P3 | Applicare CPU3 prima a decode/transform/pack già paralleli; misurare soglie per classe di job | `vita_draw_adapter.cpp`, `vita_vertex_decode.cpp`, [vita_vertex_pipeline.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/extern/aurora-vita/platforms/vita/gfx/vita_vertex_pipeline.cpp) | Guadagno sul frame e sul p95, non solo sul tempo del callback; piccoli draw lasciati seriali se il dispatch costa di più |
| P4 | Distribuire pose, copie/blend snapshot e culling già indipendenti; usare costo stimato e chunk per personaggio/oggetti, superando dove utile il limite 10/3 e 170/48 | [FixedUpdateTask.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/src/Game/FixedUpdateTask.cpp), [RenderSnapshot.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/src/Game/RenderSnapshot.cpp), [world.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/src/Game/world.cpp) | Commit fisica e ordine GX deterministici; first-use allocation sul main; costi per tick e per frame distinti |
| P5 | Ridurre lavoro seriale e attese: misurare riuso snapshot/uniform, display-list shadow, costi `finish` del retirement e invalidazioni | [aurora_vita_draw_sink.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/extern/aurora-vita/platforms/vita/gx/aurora_vita_draw_sink.cpp), [gxm_renderer.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/extern/aurora-vita/platforms/vita/gxm/gxm_renderer.cpp), [vita_static_geometry.hpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/extern/aurora-vita/platforms/vita/gfx/vita_static_geometry.hpp) | Recupero misurato di ms senza regressioni nelle transizioni o risorse GPU premature |
| P6 | Rivalutare il thread GX solo se la traccia dimostra un vantaggio dalla sovrapposizione | [fifo.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/extern/aurora-vita/lib/gx/fifo.cpp), configurazione worker e `main.cpp` | Topologia senza contesa accidentale; ownership/memoria in volo corrette; p95 e p99 migliori |

Parametri nuovi suggeriti, **non ancora implementati**: `vita_core3=auto`, `vita_core3_max_total_pct=70`, `vita_core3_guard_pct=5`, `vita_core3_window_ms=100`, `vita_core3_chunk_target_us=250`. Il loader INI converte i nomi in `STRIKERS_*`, ma questo non basta: vanno aggiunti parsing, validazione e applicazione in `BackendConfig`. L'opzione esistente `aurora_cpu_workers=3` oggi non è accettata. Prevedere anche un override OFF per il confronto sullo stesso eseguibile.

Per P4 non parallelizzare indiscriminatamente l'intero `FixedUpdateTask`. `PreparePlayerPoses` è già separato dal commit sequenziale degli oggetti fisici. Snapshot e skinning hanno protezioni per le allocazioni iniziali; conservarle. I job più lunghi di un singolo personaggio vanno suddivisi internamente solo se le dipendenze lo permettono, altrimenti CPU3 li rifiuta quando il budget residuo è insufficiente.

Per P5 partire dalla configurazione effettivamente in uso, `0x18` e shadow OFF, che può rappresentare un workaround di stabilità. Testare separatamente `0x18 → 0x10` (snapshot), `0x18 → 0x08` (uniform fragment) e shadow OFF → ON, sulla stessa build e dopo verifica della relativa correzione. Non riattivare tutti i percorsi insieme. Contare le attese `finish` con causa; un fence/serial di completamento GPU può consentire retirement più preciso in futuro, ma la sola età in frame non prova che la GPU abbia finito.

## 7. Protocollo per confermare CPU bound e misurare il guadagno

1. Identificare la build attuale includendo entrambi i diff, impostazioni e hash dell'installato. Mantenere inizialmente GX thread OFF, configurazione grafica, risoluzione e clock costanti. Non ripristinare automaticamente vecchi default del report precedente.
2. Aggiungere registrazione opt-in durante il normale gioco, con start/stop espliciti e raccolta in RAM; salvare a fine finestra per limitare I/O nel percorso critico. Conservare anche il profilo per task, ma con ID sessione. Le vecchie opzioni `benchmark` non sono un meccanismo di acquisizione attivo.
3. Usare una sequenza ripetibile sul gioco reale: stesso stadio, personaggi, camera e condizioni. Misurare separatamente ingresso in partita, gioco attivo, goal/replay e pausa/ripresa. Segnalare `simulation_ticks`, frame scartati, shader compilation e transizioni. Non dichiarare 60 FPS da tick di simulazione o frame non presentati.
4. Confrontare tempi elapsed e runtime CPU per thread, `U0..U3`, attese semafori, `send_views`, decoder, pack, uniform, `finish`, EFB e present. Misurare i job per categoria e distinguere `Game Fixed Update` per frame dal costo per tick, evitando di attribuire al tick singolo l'eventuale recupero di più tick.
5. Se la diagnosi resta ambigua, fare un A/B di frequenza CPU a GPU costante e/o risoluzione a CPU costante, con clock realmente letti. Il codice attuale richiede esplicitamente solo 444 MHz per la CPU: scrivere `cpu_mhz=333` non costituisce un comando supportato per riportarla a 333. Per quel test serve gestione esplicita e readback della frequenza o una configurazione esterna verificata.
6. Per ogni variante, un avvio di warmup e almeno tre run di 120–150 secondi; nella misura a regime escludere e annotare i primi 300 frame attivi, conservando i dati grezzi per lo studio dello stutter. Richiedere shader cache stabile e visualizzare i frame corrispondenti alle anomalie.

Matrice di confronto proposta, sullo stesso binario quando possibile:

| Variante | Core3 | Configurazione grafica | Scopo |
| --- | --- | --- | --- |
| A | OFF | Attuale verificata | Baseline dei tre core |
| B | Probe e telemetria, zero job | Identica ad A | Costo dell'instrumentazione e accesso a CPU3 |
| C | Helper con budget totale 50% | Identica ad A | Verificare che il lavoro trasferito migliori il frame senza blocchi |
| D | Helper con budget totale 60% | Identica ad A | Curva prestazioni/carico |
| E | Helper con massimo totale 70% e margine adattivo | Identica ad A | Profilo richiesto, controllando anche le finestre peggiori |
| F | Migliore profilo worker validato | Una sola ottimizzazione P5 per volta | Recuperare anche costi seriali e dei workaround |

Non è obbligatorio riempire CPU3: se un carico inferiore produce lo stesso throughput o p95 migliore, mantenerlo. Confrontare frame time e tempo del main sgravato, non il solo aumento dell'utilizzo totale.

## 8. Test e criteri di accettazione delle future modifiche

Estendere [vita_cpu_workers_test.cpp](/Users/robin994/Documents/Code/strikersVita/smstrikers-port/extern/aurora-vita/tests/vita_cpu_workers_test.cpp), che oggi prova uno/due helper, per coprire tre helper/quattro lane, errore di creazione del worker CPU3, callback falliti, copertura esatta, job piccoli/grandi, nested/concurrent fallback, stop e shutdown. Aggiungere clock finto per budget esaurito, rinnovo della finestra, debito e carico esterno; verificare che nessun range resti perso o bloccato sul worker limitato. I test host verificano la logica, non affinità, uso reale del core o FPS.

Su Vita verificare: core ID e affinità, contatori calibrati, percentuale totale e applicativa CPU3, massimi sulle finestre 100 ms/1 s, chunk più lungo, sforamenti, attese del caller, audio underrun, crash/freeze e immagini di ombre/HUD/riflessi. Una patch viene accettata se il miglioramento supera la variabilità dei run, non peggiora la coda p95/p99 e conserva correttezza e stabilità. Gli sforamenti del 70% invalidano l'affermazione di rispetto del limite fino alla loro spiegazione/correzione.

Il target finale resta rendering reale sostenuto a 59,94/60 Hz, circa 16,68/16,67 ms per frame, con margine sul percorso critico; misurare frazione di deadline rispettate, p95, p99 e pause lunghe. Un p95 entro budget da solo non prova assenza di stutter. L'incremento di capacità del quarto core deve accompagnarsi alla riduzione dei costi del renderer e alla verifica della simulazione.

## 9. Evidenze e ripresa del lavoro

- [Manifest della raccolta](/Users/robin994/Documents/Code/strikersVita/ab-artifacts/review-20260929-core3/collection.json): data UTC, hash installato/locale, presenza CapUnlocker in configurazione, SHA dei file letti e dei diff.
- [INI della console](/Users/robin994/Documents/Code/strikersVita/ab-artifacts/review-20260929-core3/strikers.ini), [lista FTP](/Users/robin994/Documents/Code/strikersVita/ab-artifacts/review-20260929-core3/device-directory.txt), [runtime log storico](/Users/robin994/Documents/Code/strikersVita/ab-artifacts/review-20260929-core3/runtime.log).
- [Profilo task acquisito](/Users/robin994/Documents/Code/strikersVita/ab-artifacts/review-20260929-core3/task_profile.log) e [ultimi blocchi estratti](/Users/robin994/Documents/Code/strikersVita/ab-artifacts/review-20260929-core3/task-profile-summary.json).
- [Diff Strikers](/Users/robin994/Documents/Code/strikersVita/ab-artifacts/review-20260929-core3/strikers-working-tree.patch) e [diff Aurora](/Users/robin994/Documents/Code/strikersVita/ab-artifacts/review-20260929-core3/aurora-working-tree.patch), fotografati prima di creare questo report.
- Fonti API primarie: [CapUnlocker](https://github.com/GrapheneCt/CapUnlocker), [VitaSDK CPU masks](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/kernel/cpu.h), [VitaSDK thread APIs](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/kernel/threadmgr/thread.h), [VitaSDK thread structures](https://github.com/vitasdk/vita-headers/blob/master/include/psp2common/kernel/threadmgr.h). Consultate il 29 settembre 2026.

`ab-artifacts/` è ignorata da Git: le evidenze sono disponibili in questo checkout, ma non viaggiano automaticamente con il documento. I risultati essenziali sono riportati sopra per rendere il report consultabile autonomamente da un altro agente. La console è stata soltanto letta; nessun eseguibile o INI è stato modificato, nessuna partita è stata avviata e nessun risultato di performance nuovo è attribuito al quarto core.

**Prima attività implementativa consigliata:** P0 e P1, seguite dal budget P2. La nuova baseline deve essere attribuibile al binario e mostrare i runtime dei tre core esistenti; il primo A/B CPU3 deve già rispettare il controllo di budget e gestire tutti gli array/soglie che oggi limitano le lane.
