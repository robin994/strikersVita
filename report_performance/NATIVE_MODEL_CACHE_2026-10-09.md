# Strikers Vita: cache selettiva delle ricette native

Data: 2026-10-09. Prosecuzione autorizzata del [confronto con ACGC](ACGC_COMPARISON_2026-10-09.md). Scope: prima tranche, metadati e identità delle ricette; budget geometrico invariato a 8 MiB.

## Stato

Prima tranche implementata e misurata. Le immagini complete coincidono fra OFF e ON ai frame live 120 e 360 dopo la correzione del programma vertex. **Il candidato fallisce il gate prestazionale: 15,78 FPS contro 20,85 OFF, -24,3%.** Nessuna promozione dei default; ricette/cache restano sperimentali e disattivate di default. **SELF e INI originali ripristinati e verificati byte per byte.**

## Modifica

- Nuovo flag INI `gxm_native_model_cache=1`, sperimentale e default `0`. Richiede il percorso delle ricette `gxm_native_model_draw=1` o `2`.
- Produttore e consumer: massimo 256 entry ciascuno, 64 gruppi da quattro voci, sostituzione della meno recente nel gruppo. La ricerca esamina al massimo quattro voci; il produttore continua a confrontare tutti i campi semantici del packet e degli stream prima di riusare una ricetta.
- Il produttore raggruppa per identità della display list e vista; più varianti possono condividere la chiave senza essere considerate equivalenti. Il consumer usa l'identità della ricetta. Nessun hash da solo autorizza una draw.
- Limite coordinato di 384 oggetti ricetta vivi, contro 96 del controllo. Le entry del produttore possiedono riferimenti condivisi; la FIFO conserva autonomamente i propri fino alla fine del comando. Sostituire un metadato non libera una ricetta in volo.
- Il controllo con flag `0` conserva 64 stamp e lo svuotamento consumer a 64 entry. La cache candidata alloca metadati CPU; non possiede buffer GPU. Restano tutte le verifiche di residenza geometrica, pin, layout, materiali, texture/TLUT/EFB e modalità runtime.
- Contatori diagnostici separano lookup/riuso/sostituzioni, rifiuti della registrazione, limite live, rifiuti del pin/trasporto e ciascuna guardia consumer. Peak distinti per occupazione, metadati, oggetti live, payload della singola ricetta, segmenti e byte pinned della batch e batch in coda. Nessun log per draw; mutex e contatori dettagliati sono disattivati nelle prove quiet.

L'array originale del produttore rimane presente anche nel candidato e viene contato nel peak dei metadati. Il peak consumer del controllo è un limite inferiore del payload, senza overhead delle bucket/allocazioni robin_hood. Quello candidato misura lo storage del vettore di slot. Questi numeri non comprendono overhead dell'allocatore/shared_ptr e storage delle shadow display list; quest'ultimo mantiene il proprio limite separato. `recipe_payload_bytes` misura il massimo della capacità before/after di una singola ricetta, non la somma di tutte.

## Contratto del programma vertex

Il confronto `PipelineConfig` delle ricette copre TEV/raster ma esclude le maschere XF delle luci. Il programma vertex fisso le include nella propria identità. Si è aggiunta una verifica esatta dei quattro valori correnti: quando cambiano, la draw esegue GX nello stesso slot e aggiorna la ricetta dal risultato ordinario. Non si conserva il programma di un'altra configurazione di illuminazione.

Il test indipendente dimostra che cambiare la maschera da 1 a 2 può lasciare invariato il guard `PipelineConfig` pur cambiando la chiave della pipeline GPU. Verifica inoltre la matrice GX_TEXMTX8 usata dal crowd atlas, confrontando uniform completi e traduzione nativa. Questa prova identifica una lacuna del contratto, ma non attribuisce da sola una differenza di pixel sulla Vita.

## Verifiche locali

- Suite Aurora host finale: **30/30 PASS**. Include controllo e candidato, sostituzione selettiva oltre 64 identità, mantenimento della ricetta in uso, collisioni senza false equivalenze, risorse in volo, shutdown, aggiornamenti di pose/scissor, texture/sampler/TLUT, EFB, raster e sorgenti geometriche. Il percorso GPU lit/indexed-PN ha ancora un oracle CPU degli uniform: il backend host non prova l'esecuzione del driver GXM.
- Configurazione: default cache `0` e override INI `1` verificati; percorso draw default `0`, override `2`.
- Analizzatore: **5/5 PASS**; eventi cache trattati come delta fra endpoint, peak memoria come gauge. Snapshot consumer ripetute/non campionate non vengono sommate.
- ASan/UBSan: test della cache e dei contratti eseguiti. ASan non segnala errori di memoria; UBSan segnala letture disallineate preesistenti nel decoder `command_processor.cpp:158`. Il run rigoroso fallisce per queste segnalazioni, presenti sia nel controllo sia nel candidato; il run non-halting conserva i diagnostici e completa le cinque prove. **Non è una suite UBSan pulita.** LeakSanitizer non è supportato su questo host. Il decoder non è stato modificato in questa tranche, e le segnalazioni non dimostrano la causa del precedente freeze della console.
- Build gioco Vita e standalone GXM completate. Audit: **12.845** simboli eseguibili nel gioco, **1.944** nello standalone; draw/present GXM, nessuna entry point/libreria GL, vgl o vita2d. Eboot nel VPK byte-identico al SELF.

## Identità

Root HEAD `031923fbe23240eea4b698e5c9befc893e006372`; Aurora embedded HEAD `6acc6a889d0bec1fb206b1c73cb693a434c132df`. Working tree con modifiche precedenti, conservate prima dell'intervento: gli HEAD non identificano da soli il binario.

- Candidato iniziale cache, SELF `2148ac11ae8cd7d3531f9991cd87b62cecb8da4289fd0ee8d5bada46c9ca1989`.
- Secondo candidato con guard vertex, SELF `b5baa6e9f34983918abacd01829b2e3332fa3e8319e22aa4d8133c11d2590861`.
- Baseline iniziale letta dalla Vita: SELF `0de405e4fa3891e04b298d6e8b8198ca8fda7e7ddb3b57d08444f635337e45f3`, INI `efb59a686ba0c3e0ffca1fbbb952d053859361ef556e0d59872fe2ff7bb0a40c`.

Manifest, ELF/map/VELF/SELF/VPK, patch e sorgenti non tracciati conservati in [candidate](../ab-artifacts/native-cache-20261009/candidate/manifest.json) e [candidate-vertex-guard](../ab-artifacts/native-cache-20261009/candidate-vertex-guard/manifest.json). I test usano override INI temporanei; demo non forzata nel codice.

## Prime acquisizioni e limite visivo

Sul primo candidato, frame di gameplay 120, seed comune, dt fisso 50 ms, maschera GXM 0x8:

- Due avvii OFF producono lo stesso PPM SHA-256 `c2da0760011373d66a1f1a2cf0e7e3639cf1c7a16ec22c1b12521153f3a780b4`, zero pixel differenti.
- ON cache: **904 pixel differenti**, bounding box `(3,0)-(531,73)`, nella zona superiore pubblico/HUD. Il resto coincide. Il gate dell'immagine completa non è passato; non si tratta di una prova di correttezza completa.
- Delta diagnostico su endpoint consumer 618–737, intervallo 119: 22.575 tentativi, 15.321 hit, 7.254 fallback, 5.711 compilazioni. Riuso **67,87%**. Consumer missing 7.249, guard 5; zero altri rifiuti consumer nell'intervallo. Consumer replacement 7.198, clear 0; producer replacement 5.711. Questi dati non sono una misura FPS e non sono un confronto isolato con il 7,71% storico, raccolto in un'altra finestra.
- Peak: 255 stamp producer, 256 entry consumer, 384 oggetti ricetta live, 65.536 byte metadati producer, 1.226.752 byte consumer; circa **1,23 MiB combinati**. Payload delle ricette draw-only 0; pending pinned batch massimo 262.016 byte, segmenti 127, coda 8 batch.

Il secondo candidato è stato ricostruito e installato dopo il guard vertex. Al frame live 120, OFF e ON producono lo stesso PPM dei precedenti controlli: **zero pixel differenti sull'immagine completa**. Nell'intervallo consumer 618–737, 22.575 tentativi producono 14.441 hit (63,97%) e 8.134 fallback; 7.710 per entry mancante, 419 per programma vertex e 5 per guard pipeline. Questa evidenza lega la correzione a un cambiamento effettivo sul dispositivo; non prova tutti gli stati di gioco né un miglioramento FPS.

Anche al frame live 360 le immagini complete OFF/ON coincidono: SHA-256 `3f9fa807a6223f424cd5b1de8ea2959cf72d2e7e7d5427de17a91e982902e33f`. Il candidato registra 23.733 tentativi, 15.141 hit (63,80%), 8.592 fallback e 6.088 compilazioni nell'intervallo consumer di 119 frame: missing 7.843, programma vertex 749, altri rifiuti consumer zero. La copertura visiva rimane limitata a queste due snapshot del demo, non a una partita completa o a suspend/resume.

La raccolta del candidato al frame 360 ha incontrato timeout FTP su SIZE e sulla successiva connessione. L'utente conferma che la console risponde e la partita continua; il canale comandi Companion risponde e chiude l'app regolarmente. FTP rimane non responsivo dopo la chiusura e un riavvio comandato. Dopo il riavvio manuale confermato dall'utente, la connessione torna operativa e i file vengono recuperati integri, con SELF/INI verificati e nessun nuovo coredump. Si è osservata anche una connessione locale FileZilla ESTABLISHED; l'utente la dichiara non connessa, e la causa del timeout non è accertata. Non si attribuisce il problema a un freeze della console o alla cache.

ABBA quiet: OFF = percorso GX ordinario (`draw=0, cache=0`); ON = ricette con cache selettiva (`draw=2, cache=1`), sul medesimo SELF corretto. Questo confronto misura il candidato completo rispetto al percorso ordinario; non isola la sola politica di sostituzione rispetto alle vecchie ricette con `draw=2, cache=0`. Seed comune, dt reale, 600 frame live di warm-up, 300 campioni per avvio, diagnostica/census/overlay disattivati, geometria 8 MiB e arena sorgenti RAM 48 MiB. Due avvii per condizione.

## Risultato quiet sulla Vita

SELF comune `b5baa6e9f34983918abacd01829b2e3332fa3e8319e22aa4d8133c11d2590861`. Ogni campione contiene esclusivamente gameplay live, frame 600–899, con clock effettivi CPU/GPU/bus/xbar 444/222/222/166 MHz. Hash del SELF riletto prima/dopo ogni acquisizione; hash dei file e override effettivi dell'INI verificati in tutte le 11 catture complete. L'INI può essere riscritto dal runtime nel blocco gestito: identità di avvio e finale sono entrambe registrate, e i valori first-value-wins coincidono con gli override attesi.

| Avvio | Configurazione | FPS effettivi | Tempo medio ms | P95 ms |
|---|---|---:|---:|---:|
| OFF 1 | GX ordinario | 21,040 | 47,528 | 56,834 |
| ON 1 | Ricette + cache selettiva | 15,607 | 64,072 | 74,667 |
| ON 2 | Ricette + cache selettiva | 15,952 | 62,686 | 75,195 |
| OFF 2 | GX ordinario | 20,662 | 48,397 | 56,362 |

Aggregati da 600 campioni per condizione, FPS = campioni divisi per il tempo totale dei frame:

| Condizione | FPS | Medio ms | Mediana ms | P95 ms | P99 ms | Frame entro 16,667 ms |
|---|---:|---:|---:|---:|---:|---:|
| OFF | 20,849 | 47,963 | 46,968 | 56,704 | 69,060 | 0% |
| ON | 15,778 | 63,379 | 61,634 | 75,195 | 84,909 | 0% |

ON aumenta il tempo medio del **32,14%** e riduce gli FPS del **24,32%**. Span del tempo medio fra le due ripetizioni: OFF 1,81%, ON 2,19%. La differenza supera ampiamente la variabilità osservata in queste quattro prove. Due avvii per condizione e seed comune con dt reale non garantiscono pose/carico identici né coprono ogni scena; non è un'attribuzione isolata alla dimensione della cache o a una singola sottofase.

**Decisione: non attivare il candidato.** Il maggiore hit rate, nessun clear-all consumer e i due confronti visivi positivi non soddisfano il gate FPS. Si conservano codice default-off, guard vertex corretto, test e artefatti per la successiva bisezione; niente ulteriori aumenti di cache/pool senza una nuova ipotesi misurabile. Dal controllo attuale al target servirebbe ridurre il tempo medio di circa il 65,25%, da 47,963 a 16,667 ms.

## Lavoro continuo rimasto

Il riuso della ricetta evita il decode della display list, ma `submit_native_model_recipe` continua a risolvere le texture correnti, tradurre lo stato fixed vertex, costruire gli uniform completi e pubblicare uno snapshot per ogni istanza. Imposta inoltre a zero le revisioni del packet per richiedere confronti esatti. Il percorso ordinario ha già memo e riuso degli uniform in `buildFixedUniforms`; quello nativo usa direttamente `translate_fixed_vertex_state`, `fixed_vertex_uniforms_into` e `publish`. Questo è un costo verificato nel sorgente, non ancora una misura isolata del suo peso sul dispositivo.

Diagnostica separata, mediana per frame in ms, finestra live 60–179:

| Scope | OFF | ON |
|---|---:|---:|
| Draw frontend | 28,070 | 27,544 |
| Traduzione stato | 7,206 | 4,517 |
| Costruzione comandi | 2,995 | 1,345 |
| Submit CPU | 5,347 | 6,123 |

Gli scope sono annidati e non vanno sommati. Le sottofasi non coprono allo stesso modo il ramo nativo: parte del lavoro lì rimane nello scope frontend senza la medesima strumentazione state/command. Un calo della sottofase non prova da solo che tutto quel lavoro sia stato eliminato; il frontend complessivo resta vicino al controllo. Il submit non misura l'esecuzione GPU.

Nell'intervallo consumer della prima finestra, ON registra 11.041 hit e 60 miss della geometria ordinaria (99,46% hit), contro OFF 25.482 hit e 60 miss (99,77%). Nella seconda, ON 11.599 hit e 12 miss (99,90%), OFF 26.740 hit e 12 miss (99,96%). Le draw native residenti evitano quei lookup, quindi i denominatori differiscono. `pool_busy_fallbacks=0` in entrambe le finestre. Questi dati non sostengono l'aumento indiscriminato della pool geometrica, né provano che ogni precedente problema di memoria sia risolto.

La tranche successiva da misurare è riusare la preparazione ordinaria degli uniform anche per le ricette, evitando copie e ricostruzioni quando i domini effettivi sono invariati. Servono snapshot completi per ogni istanza, identità del vertex program, invalidazione di matrici/luci/materiali/texture e reset per scene/barriere. Non si possono saltare tali verifiche per ottenere hit. Non implementata in questa tranche.

## Ripristino

Chiusura della sola app Strikers, ripristino INI, riavvio controllato con Companion per liberare la directory applicazione, ripristino SELF e ulteriore lettura di entrambi. Identità finale:

- `eboot.bin`: 3.553.593 byte, SHA-256 `0de405e4fa3891e04b298d6e8b8198ca8fda7e7ddb3b57d08444f635337e45f3`, byte-identico al backup iniziale.
- `strikers.ini`: 3.617 byte, SHA-256 `efb59a686ba0c3e0ffca1fbbb952d053859361ef556e0d59872fe2ff7bb0a40c`, byte-identico al backup iniziale, comprese le impostazioni precedenti dell'utente.
- Nessun nuovo coredump rispetto ai nove già presenti all'inizio. Nessuna nuova prova lasciata in esecuzione.

Il timeout FTP intermedio rimane un problema del trasferimento non attribuito; non è stato osservato un freeze della console. La prova non costituisce una certificazione di stabilità lunga né una diagnosi del precedente blocco con geometria 16 MiB. Salvataggi, archivi dei contenuti e plugin non sono stati modificati.

FTP ha rifiutato lo staging del secondo SELF dopo la chiusura dell'app (`550 File not found`). Il binario precedente era ancora integro; riavvio controllato attraverso Companion e installazione successiva con readback riuscita. Nessun nuovo coredump nelle prime acquisizioni.

## Evidenze

- [Backup iniziale](../ab-artifacts/native-cache-20261009/baseline/backup-identity.json), [identità iniziale](../ab-artifacts/native-cache-20261009/initial-device-identity.json).
- [Test host finali](../ab-artifacts/native-cache-20261009/final-host-tests.log), [sanitizer con diagnostici conservati](../ab-artifacts/native-cache-20261009/final-sanitizer-observations.log).
- [Contatori primo candidato](../ab-artifacts/native-cache-20261009/nc-v120-on/consumer-summary.json), [ripetizione controllo immagini](../ab-artifacts/native-cache-20261009/visual-120-repeat-comparison.json).
- [Installazione dopo riavvio](../ab-artifacts/native-cache-20261009/install-vertex-guard-after-reboot/install-identity.json).
- [Immagine completa dopo la correzione](../ab-artifacts/native-cache-20261009/guard-visual-120-comparison.json), [stato parziale](../ab-artifacts/native-cache-20261009/partial-result.json), [verifica Companion dopo il timeout](../ab-artifacts/native-cache-20261009/companion-health-after-timeout.json), [chiusura dell'app e stato FTP](../ab-artifacts/native-cache-20261009/recovery-after-timeout.json).
- [Seconda immagine completa](../ab-artifacts/native-cache-20261009/guard-visual-360-comparison.json), [cattura recuperata](../ab-artifacts/native-cache-20261009/nc-g-v360-on/capture-identity.json), [stato dopo il riavvio manuale](../ab-artifacts/native-cache-20261009/health-after-manual-reboot.json).
- [Confronto ABBA quiet con identità e campioni](../ab-artifacts/native-cache-20261009/quiet-abba-summary.json), [audit delle 11 catture](../ab-artifacts/native-cache-20261009/capture-audit.json).
- [Identità finale della Vita](../ab-artifacts/native-cache-20261009/final-device-identity.json), [log del ripristino](../ab-artifacts/native-cache-20261009/final-restore.log), [esito finale strutturato](../ab-artifacts/native-cache-20261009/result.json).

Nessun commit o push. Modifiche precedenti conservate, esperimento default-off, ripristino finale verificato. Gate prestazionale fallito; la tranche successiva resta distinta da questo risultato.
