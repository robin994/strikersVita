# Strikers Vita: stato attuale e percorso verso 60 FPS

Analisi del 6 ottobre 2026. Sono stati riletti i CSV e i manifest del build
`1.3.0-assets-20261005`, la configurazione PSARC selezionata il 6 ottobre e il
codice attuale. Questa analisi non modifica il runtime o la console e non
introduce nuove misure hardware. LOD resta fuori dallo scope concordato.

## Baseline verificata

| Dato | Valore |
|---|---:|
| PSARC, gameplay, diagnostica e screenshot OFF | 600 frame consecutivi, 0–599 |
| FPS sul tempo totale del campione | 24,979 |
| Tempo medio / mediano | 40,033 / 39,806 ms |
| P95 / P99 | 43,818 / 64,745 ms |
| Mediana fase `tasks` / `present` | 39,459 / 0,338 ms |
| Sleep del limiter nell'intero campione | 0 µs |
| Frame entro 16,667 ms | 0 su 600 |
| Clock effettivi CPU / GPU | 444 / 222 MHz |
| Budget a 60 FPS | 16,667 ms |
| Riduzione necessaria rispetto alla media | 23,367 ms, ossia 58,37% |
| Accelerazione necessaria | 2,402× |

Una riduzione del tempo del 50% porterebbe a circa 50 FPS. Anche un raddoppio
delle prestazioni non basta, da questa baseline, per raggiungere 60 FPS.

Identità e limiti:

- SELF: `b7324a95ff5106d201ab9a3671dbbe09171b3bc245c6da1939909784092efca8`.
- CSV: `5c00a2b1c9ccc0a68e407b57026ad43740a7bb0d12660c2468692cb4b62cc12f`.
- Root base `fcbc550c`, Aurora embedded `39fb53e9`, con modifiche locali;
  gli hash dei file registrati in `source-identity.json` coincidono ancora.
- Il campione usa seed `0x53545249`, timestep fisso 16,666666667 ms,
  frameskip 0, WARM, `gxm_disable=0x8`. È un confronto ripetibile di gameplay,
  non una prova di partita intera, di tutti gli stadi o del pacing reale con
  timestep variabile. La configurazione normale ha il test automatico spento.
- Il confronto ISO dello stesso SELF misura 25,079 FPS. La differenza PSARC
  è −0,398%: una sola coppia di campioni non prova una differenza significativa.
  PSARC resta il formato scelto per il risparmio del 57,28% sul disco.
- Il trace Razor originale è del 3 ottobre a 333 MHz e non contiene un hash
  SELF riconducibile a questa build. Orienta l'indagine; i suoi millisecondi
  cumulativi non sono il costo residuo per frame della build attuale.

Fonti locali: [CSV PSARC](ab-artifacts/asset-pipeline-20261005/psarc-quiet/assets-psarc-quiet.csv),
[manifest del run](ab-artifacts/asset-pipeline-20261005/psarc-quiet/run-identity.json),
[log diagnostico dello stesso build](ab-artifacts/asset-pipeline-20261005/psarc-diag/assets-psarc-diag.log),
[report asset e prove hardware](ASSET_PIPELINE_STATUS_2026-10-05.md),
[analisi del trace originale](PERFORMANCE_CPU_TRACE_2026-10-05.md).

## Cosa misurano realmente questi tempi

La fase `tasks` parte prima di `aurora::vita::begin_frame()` e termina dopo
`RunAllTasks()`: include ingresso nel renderer, lavoro del gioco, emissione GX
e possibili attese. `present` comprende il lavoro successivo, inclusi audio,
chiusura/presentazione e sincronizzazione finale. Nessuno dei due valori è un
timer GPU o una misura esclusiva di calcolo CPU.

Con async GX attivo, CPU0 e il consumer GX su CPU2 lavorano durante lo stesso
frame; `end_frame()` attende il consumer prima di consentire il riuso delle
risorse. Il consumer può aver imposto attese prima del `present`. Il valore
di 0,338 ms non dimostra quindi che la GPU sia libera o irrilevante.

La prima misura utile deve distinguere: simulazione, costruzione delle liste,
GX sul consumer, attese di coda/token, decode/transform/pack, upload e attese
GXM. Non si sommano i tempi dei thread sovrapposti o le fasi annidate.

## Riscontri nel codice attuale

### 1. Vertici dinamici: percorso GPU presente ma spento

`main.cpp` seleziona `gxm_streamed_vertex_gpu=0`; le geometrie animate o non
ammesse nella cache statica continuano quindi sul percorso CPU. In
`DrawSink::submit`, `fixedStreamCandidate` permette invece di decodificare e
impacchettare sulla CPU, spostando matrici, normali, illuminazione e texgen sul
vertex shader. Non equivale a eliminare tutta la posa/skinning sulla CPU.

È il primo esperimento mirato da confrontare, dopo aver misurato quanti vertici
sono eleggibili. Usare lo stesso SELF, variando solo
`gxm_streamed_vertex_gpu=0/1`. Controllare personaggi, ombre, animazioni e HUD;
il programma/layout deve aggiornarsi correttamente, incluso il caso coperto
dalla recente correzione del GPU crash. Il beneficio dipende dalla quota di
vertici dinamici e dal margine GPU: non è ancora quantificato.

### 2. GX asincrono: parte del lavoro ripetuto rimane

La copia immutabile delle display list è già usata come trasporto dal consumer
asincrono, anche con `gxm_dl_shadow=0`. Cambiare quel flag non è un guadagno
nuovo da contare. Il percorso `submit_simple_display_list`, però, rifiuta
esplicitamente il caso con worker GX attivo: il consumer continua a interpretare
le liste con `process()`.

Il passo successivo è preparare descrittori di draw validati e riutilizzabili,
consumati in ordine sul thread GX dopo gli aggiornamenti di stato precedenti.
Partire dalle liste immutabili con un solo draw e misurarne la copertura.
Conservare identità/revisioni di vertici e palette, lifetime, copie EFB, target
e barriere. Il codice ha già riuso di pipeline, ricette e geometria: la nuova
via deve eliminare costi ancora presenti, non duplicare quelle cache.

Una successiva compilazione GLG di stadio e oggetti statici nel PSARC può
alimentare gli stessi descrittori. La sola conversione offline, se poi il
runtime ricostruisce comunque tutto il percorso GX, non garantisce più FPS.

### 3. CPU3 disponibile, ma perimetro ancora limitato

Il log del build conferma `core3_cpu=3`, affinità `0x00080000` e quattro lane
disponibili. `cpu_parallel_for_vertex()` abilita lane 3 con quota dinamica;
la configurazione conserva però `cpu_game_execution_lanes=3` e
`cpu_renderer_execution_lanes=3` per gli altri lavori. Pose, snapshot e culling
del gioco non sfruttano CPU3 attraverso l'API pubblica attuale.

Le chiamate dei personaggi usano 10 elementi e minimo 3 per lane. C'è un solo
pool: se è già occupato, il flag `busy` fa eseguire un secondo chiamante in
seriale. Con gioco e GX asincrono questo è un possibile limite di sovrapposizione,
da contare prima di cambiare la topologia. Non è dimostrato quanto incida oggi.

Estendere prima un job indipendente di posa/matrici, con output privati e commit
deterministico sul thread proprietario. Preparare dati e capacità fuori dal job,
adattare dimensioni e soglie, misurare attese e fallback per pool occupato.
Mantenere il limite CPU3 del 70% totale, guardia e finestre esistenti. Il quarto
core da solo non giustifica una previsione di accelerazione 2,4×.

`vita_skin_packets=1` è un esperimento distinto già disponibile: prepara matrici
per vista, ma aggiunge una visita della render list, copie e confronti, usa tre
lane e scarta batch sotto 128 matrici. Prima confrontare tempo risparmiato,
preparazione, utilizzi e fallback; abilitarlo non è automaticamente conveniente.

### 4. Texture: due ostacoli concreti, guadagno da misurare

- Il facade passa `nativeDesc.cacheable=false` per possedere lui la cache e il
  rilascio. L'ammissione CMPR→BC1 richiede `desc.cacheable`: questo percorso
  non viene quindi raggiunto. Separare l'eleggibilità di una sorgente immutabile
  dalla proprietà della cache; conservare ritiro GPU e invalidazioni.
- Il preflight della cache stima sempre 4 byte per texel, prima di scegliere il
  formato nativo. Può espellere risorse o rifiutare upload anche quando I8,
  RGB565 o un futuro BC1 richiedono meno memoria. Calcolare l'ingombro del layout
  realmente scelto, includendo mip, padding e allocazione, poi applicare il budget.

Misurare upload/frame, byte residenti, espulsioni e attese di distruzione.
Se le texture sono già residenti senza ricambio durante il gameplay, il beneficio
può essere principalmente memoria/caricamento. La conversione esatta CMPR→BC1
copre un sottoinsieme; una conversione più ampia richiede una politica di qualità
esplicita. Il PSARC può contenere mip pronti per GXM con identificazione del formato
e fallback ai dati originali.

### 5. Geometria residente e materiali

La cache statica è già attiva, limitata a 8 MiB. `BufferPool` riceve un flag
`dynamic`, ma `Renderer::create_buffer()` alloca sempre `MemoryKind::CpuGpu`,
che preferisce USER uncached. Non esiste ancora una scelta CDRAM dedicata alle
geometrie immutabili. Introdurla per i buffer residenti può aiutare il fetch GPU;
non rende il rendering gratuito e va dimensionata insieme a texture e superfici.
Prima verificare hit/miss/espulsioni, byte e cause di fallback della cache da 8 MiB.

Il generatore di materiali aritmetici è già presente. Il minor testo shader non
prova un minor tempo GPU. Specializzare ulteriormente i materiali dominanti solo
dopo averne misurato draw, pixel/passate e costi. `gxm_disable=0x8` disattiva il
riuso degli snapshot fixed specifico di quel bit, ma lascia attivi il nuovo
builder e il riuso basato sulla revisione del dominio vertex: non significa che
tutti gli uniform vengano sempre ricostruiti. Il confronto 0x8/0 è secondario e
deve preservare i test di transizione e il reset dello stato a BeginScene.

## Ordine operativo proposto

| Ordine | Intervento | Esito che decide se continuare |
|---|---|---|
| P0 | Profilo attribuito al SELF attuale e alla scena | Ripartizione del percorso critico, attese, draw e vertici per via CPU/GPU |
| P1 | A/B vertici dinamici GPU; A/B skin packets separato | Riduzione ripetibile di tempo per frame, immagini e animazioni equivalenti |
| P2 | Draw preparati sul consumer GX; riduzione dei passaggi e delle copie residue | Copertura dei draw e riduzione di GX/submit senza perdita di ordine o lifetime |
| P3 | Pose/preparazione su CPU3 e granularità dei job | Minore tempo sul percorso critico, meno attese; quota totale rispettata |
| P4 | Eleggibilità BC1, budget texture reale, GLT preparati | Meno byte/upload/espulsioni e beneficio misurato nel caso limitante |
| P5 | Geometria statica CDRAM e GLG preparati nel PSARC | Minore costo di fetch/preparazione, memoria sostenibile, nessuna regressione |
| P6 | Materiali/passate più semplici se il profilo indica un limite GPU | Riduzione del costo GPU e delle latenze senza alterare il risultato visivo |

L'ordine P2–P6 deve cambiare se P0 identifica un limite diverso. Non assegniamo
millisecondi di guadagno previsti senza la ripartizione attuale: i benefici possono
sovrapporsi, spostare il collo di bottiglia o non incidere sul percorso critico.

### Acquisizione minima prima delle modifiche principali

1. Tre campioni PSARC sullo stesso SELF, seed, scena, clock e shader caldi:
   almeno 1.200 frame di gameplay, warmup esplicito, frameskip/screenshot OFF.
2. Un'acquisizione di attribuzione con task, FIFO e fasi vertex, distinta dalla
   misura FPS senza diagnostica. Registrare sessione/frame, thread/core, attese
   produttore/consumer, occupazione del pool, simulazioni/frame, cache e upload.
3. Usare snapshot già pubblicati dal consumer quando possibile. Attualmente
   `performance_snapshot()` fa `run_sync` con async GX: interrogarla spesso può
   cambiare la sovrapposizione che si vuole misurare. La pubblicazione dei
   completed snapshot è oggi condizionata dalla diagnostica e va considerata
   nell'eventuale collector leggero.
4. Le opzioni scene/draw-finish serializzano CPU/GPU. Il loro dato è attesa di
   completamento residua, non automaticamente tempo totale GPU; usarle per
   localizzare, poi disabilitarle per A/B. Un eventuale confronto a risoluzione
   ridotta è una prova separata da implementare preservando scissor/EFB, non un
   flag già individuato in questa analisi.
5. Applicare una modifica alla volta. Confrontare media, mediana, P95/P99,
   percentuale entro 16,67 ms, visuali e audio; completare partita, replay,
   menu e sospensione/ripresa prima di chiamare stabile il nuovo default.

Checkpoint di lavoro: 33,33 ms (30 FPS), 25 ms (40 FPS), 20 ms (50 FPS),
16,67 ms (60 FPS). Sono obiettivi, non previsioni. Il criterio finale include
frame realmente presentati, corretto avanzamento della simulazione e assenza
di frameskip, non soltanto il valore dell'overlay.

## Riferimenti di implementazione

- [Configurazione renderer, worker e percorsi GPU](smstrikers-port/src/Game/main.cpp)
- [Timing del collector](smstrikers-port/src/platform/benchmark.c)
- [DrawSink e selezione CPU/GPU](smstrikers-port/extern/aurora-vita/platforms/vita/gx/aurora_vita_draw_sink.cpp)
- [FIFO asincrona e shadow immutabili](smstrikers-port/extern/aurora-vita/lib/gx/fifo.cpp)
- [Percorso diretto display list](smstrikers-port/extern/aurora-vita/lib/gx/command_processor.cpp)
- [Pool worker e quote CPU3](smstrikers-port/extern/aurora-vita/platforms/vita/gfx/vita_cpu_workers.cpp)
- [Pose e snapshot](smstrikers-port/src/Game/RenderSnapshot.cpp)
- [Preparazione matrici skin](smstrikers-port/src/NL/glx/glxSend.cpp)
- [Cache texture e buffer](smstrikers-port/extern/aurora-vita/platforms/vita/gxm/gxm_facade.cpp)
- [Layout texture native](smstrikers-port/extern/aurora-vita/platforms/vita/gxm/gxm_texture_layout.cpp)
- [Allocazione buffer e binding GXM](smstrikers-port/extern/aurora-vita/platforms/vita/gxm/gxm_renderer.cpp)
- [Allocatore USER/CDRAM](smstrikers-port/extern/aurora-vita/platforms/vita/gxm/gxm_memory.cpp)

Gli interventi MMU software, ABI PPC e sostituzione FMA del workflow originale
non rappresentano lavoro residuo di questo port nativo ARM. Release/LTO,
GX asincrono, trasporto immutabile, builder incrementale e materiali aritmetici
sono già presenti. La richiesta CPU 500 MHz ricade a 444 nella configurazione
misurata: non va conteggiata come una prestazione disponibile. La priorità è
ridurre lavoro e attese, misurando il risultato sulla Vita.
