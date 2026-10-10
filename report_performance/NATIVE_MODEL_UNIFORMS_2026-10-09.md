# Strikers Vita: builder uniform comune per le ricette native

Prove iniziate il 2026-10-09, riprese il 2026-10-10. Prosecuzione autorizzata dopo il push as-is e la regressione della cache.

## Base pubblicata

- Strikers `b21c2fa0927990a4cc5d94f98386c77c4cc0f5ec`, `origin/main` verificato.
- Aurora `b1a4cc55b996fa5fbe8cea01a1df6302ddb62fa5`, `origin/vita-experiment` verificato.
- Push as-is di tutti i sorgenti/report tracciabili; artefatti locali sotto `ab-artifacts/` restano nella directory ignorata prevista dal repository.

## Precisazione rispetto alla tranche precedente

`FixedUniformPool::publish()` chiama già `publish_from(scratch, reuse=true)` e può riusare lo snapshot CPU quando i byte coincidono. Il costo verificato nel ramo nativo è la costruzione completa con `fixed_vertex_uniforms_into`, contrapposta al builder incrementale del ramo ordinario. Il report precedente descriveva la differenza di pubblicazione in modo troppo ampio.

Inoltre `gxm_disable=0x8` disabilita `GxmDisableFixedSnapshot`: il ramo ordinario rispetta questa maschera nella pubblicazione e il renderer la rispetta nelle riserve GXM, mentre il ramo nativo precedente usa sempre il default `reuse=true` della pool CPU. Le prove precedenti restano risultati del candidato completo; non isolano soltanto una politica di cache o pubblicazione.

## Modifica e scope

Nuovo flag `gxm_native_model_uniform_build=1`, default 0. Helper privato comune al draw sink, con builder incrementale già esistente, confronto esatto degli snapshot e rispetto di `GxmDisableFixedBuild`/`GxmDisableFixedSnapshot`. Il controllo flag 0 conserva la costruzione/publicazione delle ricette della base as-is. Si aggiungono gli scope diagnostici state/build/publish anche al controllo per rendere visibile il lavoro che prima restava soltanto nel frontend.

Non si cambia la traduzione GX per istanza: i dati correnti sono ancora letti a ogni draw, con stato completo inizializzato. Non si introducono cache basate su revisioni per matrici/luci, nuove riserve GXM, variazioni di scissor o scene, riordinamenti, scorciatoie per pin/texture/TLUT/EFB o nuovi shader. Geometria 8 MiB; arena sorgenti RAM 48 MiB nei test.

## Gate locali

- Aurora host: 31/31 PASS; include variante delle ricette con nuovo flag attivo.
- Snapshot identici riusati, maschera snapshot OFF che impone valori pubblicati distinti, risorse in coda immutabili dopo mutazione e alternanza nativa/ordinaria.
- Oracle CPU fra builder incrementale e costruzione completa, con 20 transizioni di palette PN, matrici texture, luci e texgen. Lit/indexed-PN rimane un oracle CPU sul backend host: serve la Vita per il programma GPU.
- Configurazione: default 0, override 1 e preservazione impostazioni verificati.
- Sanitizer: i tre test textured completano con ASan e UBSan non-halting. Il run rigoroso fallisce per il decoder disallineato `command_processor.cpp:158`, anche nel controllo. Log verbose conservati; LeakSanitizer disattivato perché non supportato sull'host. Non è un run UBSan pulito.

## Protocollo Vita

Nuovo SELF unico per controlli e candidato, con hash letto prima/dopo ogni acquisizione. Prima: immagini complete ai frame live 120 e 360, dt fisso 50 ms, seed comune, diagnostica separata. Dopo il gate: sei run quiet con 600 frame warm-up e 300 campioni, dt reale e clock invariati:

1. GX ordinario.
2. Ricette/cache precedenti, uniform build 0.
3. Ricette/cache, uniform build 1.
4. Ricette/cache, uniform build 1.
5. Ricette/cache precedenti, uniform build 0.
6. GX ordinario.

Il confronto 2/5 contro 3/4 isola il nuovo flag entro il percorso delle ricette (comprende l'allineamento alla maschera snapshot CPU). Il confronto con 1/6 valuta il vantaggio rispetto al percorso ordinario. La maschera resta 0x8: non si abilita il riuso delle riserve GPU per nascondere o mescolare i costi. Demo configurato via INI temporaneo, con ripristino byte-verificato di INI e SELF originali.

## Avvio e ripetibilita delle catture

Le prime tre catture (`nu-v120-ref`, `nu-v120-on`, `nu-v120-ref2`) usavano i due Cross automatici del vecchio helper. Il controllo ripetuto differiva in 517.400 pixel, contro 18.094 per la prima coppia controllo/candidato: non erano un oracle ripetibile. Queste immagini sono conservate ma escluse dal gate.

Companion produce gia il log di processo prima di qualsiasi conferma. La nuova politica `--launch-confirm startup-log` verifica un log unico di avvio e invia al massimo un Cross soltanto se il processo non e ancora partito. Le catture accettate avviano senza input: controllo, candidato e controllo ripetuto al frame live 120 hanno lo stesso PPM SHA-256 `c2da0760011373d66a1f1a2cf0e7e3639cf1c7a16ec22c1b12521153f3a780b4` e gli stessi frame consumer 618-737. Il comportamento e coerente con interferenze delle precedenti conferme nell'introduzione.

Il default dell'helper ora usa il controllo del log nei run diagnostici e nessun input nei run quiet. `once` e `twice` restano scelte esplicite per una LiveArea che richiede conferma. La patch riguarda il programma host di prova e non modifica il SELF gia sigillato. Nei quiet non si abilita il logging per riconoscere l'avvio.

La configurazione riletta puo avere un hash diverso da quella installata: il gioco aggiorna il suo blocco gestito. Per questo il gate verifica anche che tutti gli override effettivi, letti con la stessa regola first-value-wins del runtime, coincidano con quelli dichiarati.

## Gate visivo sulla Vita

Tutti i 960 x 544 pixel coincidono tra GX ordinario, ricette/cache precedenti e nuovo builder a entrambi i frame live 120 e 360. A 120 il controllo ripetuto conferma la stessa immagine. PPM SHA-256:

- Frame 120: `c2da0760011373d66a1f1a2cf0e7e3639cf1c7a16ec22c1b12521153f3a780b4`.
- Frame 360: `3f9fa807a6223f424cd5b1de8ea2959cf72d2e7e7d5427de17a91e982902e33f`.

Sette acquisizioni accettate, stesso SELF e override effettivi verificati, nessun input Cross dopo il launch, nessun nuovo coredump. Le tre acquisizioni iniziali non ripetibili restano escluse. Il gate prova quelle due immagini e le transizioni dei test; non e una certificazione di tutte le scene del gioco.

## Identita del candidato

- SELF SHA-256 `1d167223d44486182ee4954481fd96e0c2fd47212b64e8a3d415bdcc7b81e111`.
- VPK SHA-256 `27eb90d0e25b92d58e02c10686a527eab71111e831b93e03f2710367f5e4be6d`; eboot estratto identico al SELF.
- Audit binario del gioco: 12.846 simboli, GXM draw/present presenti, nessuna dipendenza GL/vgl/vita2d. Audit del probe standalone: 1.944 simboli.
- Artefatti locali in `ab-artifacts/native-uniforms-20261009/`: manifest candidato, SELF/ELF/map/VELF/VPK, patch e sorgenti sigillati, manifest dei programmi di prova, log dei gate, CSV/PPM grezzi e identita per run.

## Diagnostica separata

Al frame live 60-179, senza input di avvio, i due controlli e il candidato hanno gli stessi frame consumer 618-737 e 22.575 tentativi della cache. Mediane in millisecondi per frame:

| Scope | Controllo 1 | Builder attivo | Controllo 2 |
|---|---:|---:|---:|
| Costruzione uniform | 0,898 | 0,726 | 0,892 |
| Pubblicazione uniform | 1,006 | 0,905 | 1,003 |
| Traduzione stato | 5,455 | 5,403 | 5,427 |
| Frontend draw | 27,987 | 27,603 | 27,905 |

Il frontend include altri scope: non si sommano queste righe. Il risparmio locale e piccolo e questi run hanno diagnostica, census e una lettura del framebuffer. Non sono una misura FPS quiet e non stabiliscono un vantaggio di prestazioni.

## Pubblicazione del codice sperimentale

Aurora `dec0a58028972f3c4eb295235cd94ba3c24aabc6`, pubblicato su `origin/vita-experiment` con hash remoto verificato. I sette file dei sorgenti runtime/test/CMake inclusi nel manifest sigillato coincidono byte per byte con il checkout dopo la pubblicazione. Il SELF resta quello costruito dalla base as-is piu le patch sigillate, non un binario ricostruito dopo il commit.

## Risultato FPS sulla Vita

Sei acquisizioni valide, 300 campioni ciascuna, live 600-899 dopo 600 frame warm-up, diagnostica/census/overlay OFF, dt reale, geometria 8 MiB e arena sorgenti RAM 48 MiB. Clock effettivi letti dalle acquisizioni: CPU 444, GPU 222, bus 222, xbar 166 MHz.

| Condizione | FPS dei due run | FPS aggregati | Tempo medio | p95 | p99 |
|---|---|---:|---:|---:|---:|
| GX ordinario | 20,518 / 20,019 | 20,265 | 49,346 ms | 64,322 ms | 77,454 ms |
| Ricette/cache, builder OFF | 15,064 / 14,857 | 14,959 | 66,848 ms | 81,452 ms | 101,381 ms |
| Ricette/cache, builder ON | 14,878 / 15,324 | 15,098 | 66,236 ms | 81,424 ms | 100,937 ms |

FPS aggregati = campioni / somma dei tempi completi dei frame. Tutte le condizioni hanno 0% di campioni nel budget 60 FPS. Il candidato mostra +0,924% FPS rispetto alle ricette precedenti, ma i due run ON differiscono del 2,955% nel tempo medio; i controlli differiscono dell'1,384% per ricette e del 2,465% per GX. Non e un vantaggio ripetibile dimostrato. Il candidato usa il 34,228% di tempo in piu rispetto al GX ordinario.

Il piano originale continuo si e interrotto: `nu-q-ref2` non ha prodotto il CSV entro 480 secondi, senza nuovi coredump, con FTP reattivo e no-sleep Companion attivo. Identita SELF/INI e configurazione finale conservate prima della chiusura; SELF e INI originali poi ripristinati e riletti identici. La risposta Home/LiveArea e arrivata il giorno successivo dopo quel ripristino: il suo riferimento temporale e da chiarire e non dimostra da sola un'uscita spontanea dell'app.

Il 10 ottobre lo stesso SELF e stato reinstallato e i due controlli restanti hanno completato i campioni (`nu-q-ref2-reboot`, `nu-q-gx2-reboot`). Le due sessioni restano esplicite nei dati: sei run validi completano le condizioni previste, ma non sono un ABBA continuo. Anche con seed fisso il dt reale non garantisce pose e carico identici. Il run scaduto e escluso da tutti i calcoli.

## Decisione e seguito

`gxm_native_model_uniform_build` resta default 0. La correttezza dei payload e delle immagini selezionate e verificata; non si promuove una modifica sulla base di un guadagno piu piccolo della variabilita osservata. Il risultato continua a sconsigliare il percorso sperimentale ricette/cache come ottimizzazione FPS di default.

La costruzione degli uniform e soltanto circa 0,9 ms nei run diagnostici delle ricette e il nuovo builder risparmia pochi decimi: non spiega il divario fra circa 49 e 66 ms dei run quiet. Per il target 60 FPS, il percorso GX corrente deve passare da circa 49,35 a 16,67 ms, una riduzione di circa il 66%. Il seguito deve attribuire i costi continui di frontend, stato e produzione/consumo dei comandi; l'aumento della pool geometrica non e giustificato da queste prove.

## Confronto diagnostico GX completato

`nu-d-gx` ha gli stessi frame consumer 618-737, 120 campioni e la stessa immagine al frame 120 dei tre run diagnostici delle ricette. Mediane in ms:

| Scope | GX ordinario | Ricette OFF, controllo 1 | Ricette ON |
|---|---:|---:|---:|
| Frontend draw | 26,798 | 27,987 | 27,603 |
| Traduzione stato | 6,994 | 5,455 | 5,403 |
| Costruzione uniform | 0,727 | 0,898 | 0,726 |
| Pubblicazione uniform | 0,917 | 1,006 | 0,905 |
| Decodifica vertici | 2,915 | 2,941 | 2,945 |
| Risoluzione texture | 0,990 | 1,612 | 1,599 |
| Costruzione comandi | 2,896 | 1,379 | 1,321 |
| Submission CPU | 5,125 | 6,003 | 6,048 |

Questi scope sono annidati o riguardano fasi diverse: non si sommano e non sono tempo GPU. Tutte le condizioni hanno mediana 272 draw e 64.722 vertici per frame; mediana upload texture 0 chiamate / 0 byte. Le ricette riducono parte della preparazione dei comandi ma il frontend misurato resta circa 27-28 ms. La verifica della ricetta (pin, maschera runtime, guard materiale, geometria e layout) precede il suo scope DrawFrontend: quel numero non include tutto il lavoro del percorso nativo e non basta ad attribuire l'intera regressione quiet.

## Perche gli asset nativi non eliminano il costo delle draw

La conversione attuale e una preparazione di dati, non una sostituzione dell'intero renderer del gioco. `tools/asset_pipeline/native.py:89` esclude esplicitamente i chunk `0x8001B008` e `0x1B011`, identificati come SKIN e VERTEX_ANIM da `src/NL/glx/glxLoadModel.cpp:56`. Geometria statica compatibile e texture preparate non equivalgono a tutti i modelli animati e materiali convertiti in oggetti di rendering autonomi.

`src/NL/glx/glxSend.cpp` continua a eseguire lo stato originale del gioco. Il percorso `native_model_draw_only` chiama GXFlush, cerca la ricetta e, sul miss, registra una GXCallDisplayList; gli altri casi mantengono la display list ordinaria. Nel consumer `submit_native_model_recipe` verifica lo stato corrente, risolve le texture con `broadDirty=true`, traduce matrici/luci correnti e costruisce/pubblica uniform e packet. Ci sono quindi ancora costi GX e di preparazione per istanza anche quando i buffer sono gia nativi.

Le chiamate finali sono gia GXM: `platforms/vita/gxm/gxm_renderer.cpp:1717` chiama direttamente sceGxmDraw, dopo bind di programma, texture, uniform, viewport, scissor e vertex stream. Togliere il nome Aurora o chiamare la sola sceGxmDraw da un'altra funzione non elimina il lavoro precedente.

Un seguito utile e un percorso Strikers che riceve handle persistenti di mesh/materiale GXM e solo i dati dinamici dell'istanza, evitando la generazione/reinterpretazione dello stato GX per i casi coperti. Primo esperimento circoscritto su una famiglia statica dello stadio: geometria, indici, shader, texture e raster state preparati al caricamento, camera/per-istanza aggiornati a runtime, submission sullo stesso backend GXM e nello stesso ordine della coda. Restano obbligatori invalidazione corretta, snapshot immutabili, scissor esatto, reset BeginScene, ordine EFB e fallback per i casi non coperti. Estendere la preparazione offline a skin/vertex animation e un intervento distinto, da misurare anche nel costo CPU del gioco.

Il bypass del frontend GX ha un motivo tecnico da verificare; non c'e ancora una prova che basti per 60 FPS. Nessun percorso nuovo di questo tipo e implementato in questa tranche.

## Copertura e significato dei contatori nativi

Nel controllo GX, la mediana e 62.744 vertici nel percorso della geometria GPU su 64.722 vertici totali. Questo contatore misura il percorso di rendering, non la provenienza offline degli asset. Il costo GX rimane quindi anche quando molti vertici usano buffer GPU residenti.

I contatori `native_gpu_geometry_hits`/`attempts` aumentano soltanto quando si cerca un record offline durante l'ammissione nella cache, non a ogni draw che lo riusa (`vita_static_geometry.hpp:254`, `vita_native_assets.cpp:229`). Al primo campione risultano gia 302 caricamenti GPU nativi riusciti. Fra i frame consumer 618 e 737 ci sono 11 nuove ricerche e zero nuovi caricamenti riusciti, mentre la cache geometrica ordinaria registra 25.482 hit. Il delta zero dei caricamenti nativi non dimostra che i buffer gia caricati non vengano usati; i contatori attuali non separano tutte le draw residenti per origine offline/runtime.

Nel percorso sperimentale delle ricette, fra gli stessi estremi, 22.575 tentativi consumer producono 14.441 hit e 8.134 fallback (circa 64% e 36% dei tentativi). Di questi, 7.710 sono ricette mancanti nel consumer. Sono delta cumulativi fra gli estremi del CSV, non conteggi di ogni draw del gioco, e il fallback della ricetta torna al percorso GX/GXM ordinario: non significa rendering software o riconversione completa di tutti gli asset. Eliminare i controlli senza coprire i casi mancanti comprometterebbe la correttezza.

Il seguito deve misurare la copertura per famiglia di draw e distinguere buffer offline riusati, geometria ammessa a runtime e rifiuti della ricetta. Per i casi statici coperti, handle persistenti di mesh/materiale e aggiornamenti dei soli dati per istanza possono evitare la produzione e traduzione GX; skinning/animazione richiedono un percorso distinto.

## Ripristino e dati pubblicabili

Ripristino finale completato il 2026-10-10 e verificato con lettura byte per byte:

- SELF originale SHA-256 `0de405e4fa3891e04b298d6e8b8198ca8fda7e7ddb3b57d08444f635337e45f3`.
- INI originale SHA-256 `efb59a686ba0c3e0ffca1fbbb952d053859361ef556e0d59872fe2ff7bb0a40c`.
- Nessun nuovo coredump rispetto ai nove presenti prima delle prove. Pool geometrica mai aumentata oltre 8 MiB.

[Evidence JSON](NATIVE_MODEL_UNIFORMS_2026-10-09.json) conserva manifest candidato, identita e override per run, sessioni separate, run escluso, statistiche quiet/diagnostiche e ripristino. In `evidence/NATIVE_MODEL_UNIFORMS_2026-10-09/` sono versionati 14 CSV grezzi per ricalcolare i risultati; SELF/ELF/map/VELF/VPK e immagini restano negli artefatti locali sigillati. Codice opt-in e report inclusi nella pubblicazione Strikers; nessun default promosso.
