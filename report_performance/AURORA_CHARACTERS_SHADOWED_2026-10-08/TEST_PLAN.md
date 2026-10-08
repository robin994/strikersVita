# Test di correttezza e prestazioni

Questo piano definisce gate verificabili per accettare le ottimizzazioni. Una suite finita non garantisce assenza assoluta di bug: la protezione è data da oracle indipendenti, fixture delle transizioni critiche, prove su Vita, fallback e identità degli artifact. Il solo aumento degli FPS non è un criterio di correttezza.

## Baseline verificata nell'audit

Eseguiti nella copia Aurora incorporata, 8 ottobre 2026:

```sh
cd /Users/robin994/Documents/Code/strikersVita/smstrikers-port/extern/aurora-vita
cmake --preset vita-host-tests
cmake --build --preset vita-host-tests --parallel 8
ctest --preset vita-host-tests
```

Risultato: **18/18**, 4,34 secondi per CTest. [Log salvato](HOST_TESTS.log). Warning non bloccanti sulla dipendenza robin_hood e una policy CMake deprecata.

Il preset host seleziona `VITAGL` per il backend host e compila esplicitamente il generatore Cg GXM in test dedicati. Quindi controlla traduzione e contratti CPU, ma non esegue le API del renderer `gxm_renderer.cpp` sul driver Vita, non compila tutto il Cg tramite vitaShaRK e non dimostra timing o immagini su dispositivo. La nuova strumentazione shader della working tree non è coperta automaticamente dal fatto che questi 18 test passino.

| Test esistente | Cosa riutilizzare | Limite da colmare nei ticket |
|---|---|---|
| vita_frontend_translation | Stato reale GX, transizioni, recipe fixed e lifetime async | A2 deve esercitare scritture codificate e decoder, non solo modifiche dirette dei domini |
| vita_fixed_uniform_builder | Confronto builder completo/incrementale | A3 deve aggiungere sequenze consumatore ritardato e casi nuovi |
| vita_submission / vita_command_stream | Ordine, stato condiviso, batching, snapshot | Non certificano la prenotazione driver GXM senza recorder e device |
| vita_native_material | 768 programmi × 8 campioni, interprete scalar delle espressioni materiali | Non copre l'intera esecuzione shader, alpha/kill, fog, texture filtering e GPU FP32 |
| vita_backend_contract / vita_regression | Contratti backend e varie transizioni | Ogni nuovo caso richiede un'asserzione di comportamento specifica |
| vita_geometry_recipe / vita_vertex_pack | Layout, indici, preparazione geometria | A5 non può cambiare texgen senza aggiornare e verificare la recipe |
| vita_prepared_display_list | Cache e contenuto di liste preparate | Non prova beneficio FPS; non riabilitare l'esperimento implicitamente |
| vita_build_manifest / vita_binary_audit_contract | Formati e comportamento degli strumenti | Non sostituiscono l'audit dell'ELF candidato effettivo |

## Gate comuni

| Gate | Condizione necessaria | In caso di fallimento |
|---|---|---|
| G0 Provenienza | Sorgenti, dirty patch, artifact, INI, asset e cache identificati | Cattura non confrontabile, nessuna conclusione prestazionale |
| G1 Correttezza CPU | Suite pertinente + nuove fixture/oracle, nessun mismatch | Correggere o spegnere il candidato |
| G2 Driver e immagini | Build GXM, replay A/A deterministico, A/B senza differenze non ammesse | Nessuna promozione anche se il test host passa |
| G3 Stabilità | Nessun crash/errore nuovo, transizioni e suspend/resume, memoria stabile | Conservare dump e binari; tornare al controllo |
| G4 Prestazioni | Beneficio ripetuto, superiore al rumore e senza peggiorare le code | Candidato non promosso; documentare esito inconcludente o negativo |
| G5 Integrazione | Stessi gate sulla combinazione e INI ripristinato/verificato | Singoli ticket restano separati/default OFF |

### G0 Identità obbligatoria

Per ogni run salvare commit di Strikers e Aurora, hash del diff e dei file non tracciati rilevanti, compilatore/toolchain, CMakeCache e flag, SELF locale e `eboot.bin` remoto, VPK ed eboot estratto, ELF non stripped/map/VELF. Per i sorgenti dirty il solo commit non basta.

Salvare hash INI e configurazione effettiva dopo il parsing, clock richiesti ed effettivi, stato worker, asset/sidecar, versione e contenuto del cache shader, stadio, squadre, camera, seed, script/input/replay, fixed_dt, frame iniziale/finale. Le INI possono avere chiavi duplicate: non ricostruire il run da una sola riga trovata con una ricerca testuale.

Le catture già archiviate SELF 4836 sono un riferimento storico. Il prossimo candidato deve avere un controllo costruito dalla stessa base e con le medesime modifiche preesistenti, per isolare il ticket.

### G1 Equivalenza CPU e lifetime

Confrontare dati osservabili a ogni draw. Per vertici, indici e uniform copiati senza cambiare aritmetica: uguaglianza bit per bit dei campi definiti. Per record con padding/handle non stabili: serializzazione canonica dei campi, non `memcmp` indiscriminato. Per shader aritmetici: oracle separato sul sottoinsieme supportato, decisione alpha/kill esatta, tolleranza FP dichiarata solo dove giustificata e mai usata per mascherare una soglia.

Usare fixture congelate e numeri riproducibili. Ampliare i test con sequenze avverse, non soltanto draw isolati o assert sul contatore dei cache hit. Introdurre temporaneamente una violazione nota per verificare che il test la rilevi, poi rimuoverla prima della consegna.

| Famiglia | Sequenza minima | Esito richiesto |
|---|---|---|
| Domini | write identica → draw → write singolo campo → draw | Identità semantica e invalidazione del solo insieme necessario |
| Stato inattivo | cambia TEV inattivo → cambia stageCount → draw | Il valore nuovo appare quando diventa attivo |
| Palette | stesso mesh, 10 slot PN; cambia un solo slot usato | Cambia solo il risultato corrispondente, nessun riuso della posa precedente |
| Normali | scala non uniforme, normale inverse transpose, lit/unlit | Il candidato coincide con il percorso completo |
| Recipe | texgen/lighting cambia senza cambio fragment base | Attributi, stride e shader restano coerenti |
| Reset | nuovo GXState, stessa revisione numerica apparente | Nessun riuso dal vecchio stato |
| Memoria guest | enqueue → scrittura sorgente → execute ritardato | I draw già accodati consumano i bytes previsti dal contratto |
| Texture | stesso handle apparente, TLUT/dati/sampler/copy mode cambiati | Il nuovo draw campiona contenuto e coordinate aggiornati |
| Scene | draw → EndScene → BeginScene → stesso draw | Rebind completo richiesto dal contratto Aurora |
| Uniform | programma A→B→A, valori uguali e poi diversi | Buffer valido del programma corrente, nessun puntatore di una vecchia prenotazione |
| Risorse | pool pieno, cache eviction, alloc/upload failure | Fallback valido; nessuna mutazione di memoria in volo |
| Batching | draw compatibili → copy/clear → draw compatibile | Nessuna fusione attraverso la barriera |

Per A2 i test devono attraversare il decoder reale. Per A3b introdurre un recorder/adattatore di test che osservi reserve, buffer scritto, bind e draw realmente decisi dal codice usato in produzione; non scrivere un simulatore che replichi a mano l'implementazione. Il recorder non sostituisce G2.

Sanitizer per patch che modificano memoria, array, cache o lifetime: configurazione host separata con ASan+UBSan; TSan mirato se cambia trasporto asincrono. Le esecuzioni sanitizer non sono benchmark. Non azzerare il budget dei cache o alterare il fallback per far passare il caso di overflow.

### G2 Shader e immagini su Vita

Per A4/A5 il corpus include materiali reali dominanti e: 1/16 stadi TEV, registri signed, accumulator D senza wrapping improprio, bias/scale, clamp attivo/disattivo, compare/indirect nel fallback, swap, fog, dstAlpha, alpha test, sampler clamp/repeat/mirror, mip e copy mode. Per ogni shader salvare descrittore, Cg, hash sorgente compilata, GXP, log compilatore, fallback e variante scissor attivi.

Test delle soglie alpha: ref 0–255, otto funzioni compare, quattro operatori e valori subito sotto/sulla/subito sopra la soglia rappresentabile. Non dedurre la correttezza FP32 da equivalenza algebrica double. Alpha/kill e regioni depth devono concordare esattamente col controllo.

Prerequisito immagini: due replay A/A devono essere ripetibili allo stesso frame e stato. Una demo con lo stesso seed e un'immagine a un frame nominale può divergere per temporizzazione e NIS; se A/A non coincide correggere la fixture prima di misurare A/B.

| Candidato | Criterio immagine iniziale |
|---|---|
| A2/A3/A6, aritmetica e raster invariati | Zero pixel differenti sui target deterministici color, shadow ed effetti; verificare anche depth via probe se leggibile |
| A4/A5, trasformazione dichiarata esatta | Stesso criterio; nessuna differenza keep/kill, maschere e depth |
| A4/A5 con differenze FP inattese | Non promuovere. Classificare e correggere; un allentamento della tolleranza richiede una decisione esplicita sulla qualità |

Se depth non è leggibile nel percorso disponibile, usare fixture con geometria sonda successiva per rendere osservabile una differenza depth, oppure un probe dedicato. Non dichiarare depth validato dalla sola immagine color di una scena che non lo esercita. Una soglia SSIM globale può nascondere errori di ombre, bordi alpha o HUD: non basta.

Casi visivi obbligatori: campo largo; personaggi vicini con più parti skinned; pose estreme; differenti capitani/sidekick/portieri; sovrapposizioni con alpha; variazioni di illuminazione/fog; ombre sul campo e su geometria; power-up/effetti; rete/porta; goal e replay/NIS; HUD, pause menu; scissor parziale sui quattro bordi e non allineato ai tile; target size differenti; EFB copy e orientamento. Scegliere e registrare almeno tre stadi con carico/illuminazione differenti.

Compilare sia con cache shader fredda sia calda. Un cache vecchio che nasconde il nuovo generatore è un fallimento G0, non un test riuscito. Il limite o fallimento di compilazione deve usare il fallback previsto senza draw mancante.

### G3 Stabilità

Per il candidato finale: almeno una partita completa e 30 minuti di esercizio, dieci cicli PS button/suspend/resume distribuiti tra gameplay, goal/replay e menu; cambio stadio/squadre e ritorno al menu; cold launch e warm launch. Registrare massimo e plateau della memoria, cache evictions, errori GXM, fallback, frame incompleti e nuovi dump. Durate e numero cicli sono un gate proposto, non prove già eseguite.

Al primo crash interrompere l'attribuzione prestazionale, preservare coredump e relativo SELF/ELF/map/VELF/INI, verificare l'identità dell'app/modulo prima di attribuirlo a Strikers, ripristinare il controllo. Un dump di un'altra applicazione non è una regressione Strikers. Riprodurre cambiando una sola variabile.

### G4 Prestazioni e rumore

1. Conservare l'INI normale. Demo/test-match solo con override temporaneo in `strikers.ini`; nessuna attivazione forzata nel codice. Ripristinare i bytes originali e verificarne l'hash anche dopo errore.
2. Per il confronto quieto: stesso SELF con flag OFF/ON quando possibile, stessi asset/cache/clocks/topologia, `vita_frameskip=0`, diagnostica/overlay/screenshot OFF, nessuna vista o shader soppresso. Usare almeno 600 frame live di warmup e 1.200 frame di misura per run, coerenti con la baseline.
3. Eseguire almeno tre coppie controllo/candidato con ordine alternato, per esempio A-B, B-A, A-B. Aggiungere A/A per stimare il rumore. Tenere separati avvio, compilazione shader, gameplay e transizioni: la coda di un goal non va nascosta selezionando soltanto gameplay.
4. Registrare media, mediana, P95/P99, massimo, deadline hit a 16,667 ms, durata, numero di frame simulati/renderizzati/presentati e cadence. `FPS = frame / tempo totale`; non media aritmetica degli FPS istantanei. Verificare che fixed_dt/test-match non mascheri rallentamento del game time.
5. Stabilire in anticipo un minimo pratico: per questo progetto si propone un guadagno sulla media maggiore di `max(0,5 ms, variabilità A/A misurata)`; usare le differenze fra run accoppiati, non considerare 1.200 frame correlati come 1.200 esperimenti indipendenti. Tutte le coppie devono avere direzione coerente; con esito misto estendere la misura o dichiarare inconcludente.
6. P95/P99 e transizioni non devono peggiorare oltre la variabilità A/A. Con tre coppie la confidenza statistica sulle code è limitata: aumentare i run se il risultato è vicino alla soglia, senza presentare una precisione non sostenuta dai dati.
7. Eseguire a parte i timer CPU e il probe GPU serializzato per spiegare il risultato. Non usare `GxmDiagSceneFinish`, `GxmDiagDrawGpu` o screenshot nella misura quieta. La somma di timer annidati e latenze per draw non stima il frame time normale.

Contatori richiesti per spiegare un miglioramento: rebuild per dominio, bytes copiati nelle palette, snapshot pubblicati/riusati, reserve/upload uniform e bytes, cambi programma, scissor-free draw, fallback CPU, texture/geometry hit/miss, draw logici/nativi, numero scene/finish e cause. La riduzione di un contatore deve essere collegata al tempo, non sostituirlo.

Per il gate 60 FPS proposto: almeno 99% dei frame gameplay presentati entro 16,667 ms nelle scene testate, frameskip zero e game time coerente. Il gate non significa “60 stabili ovunque”: transizioni e frame eccedenti restano espliciti. Un risultato positivo su uno stadio non si estende agli altri.

## Comandi e strumenti disponibili

I comandi host sopra sono stati eseguiti. I seguenti sono istruzioni di esecuzione per gli incarichi, non verifiche già completate dall'audit.

Per la build standalone GXM, dal checkout Aurora corretto:

```sh
VITASDK=/usr/local/vitasdk cmake --preset vita-gxm
cmake --build --preset vita-gxm --parallel 8
```

Questa build non produce da sola un candidato Strikers comparabile. Ricostruire poi Strikers usando il profilo e i flag del controllo congelato. Non scegliere il preset `vita-gxm-stable` dal solo nome: contiene opzioni async/direct specifiche che potrebbero differire dal controllo.

L'audit binario richiede l'ELF non stripped e la sua map, non SELF/VPK. Schema del comando:

```sh
python3 tools/check_vita_gxm_binary.py /percorso/candidato.elf --map /percorso/candidato.map
```

I due percorsi sono segnaposto da sostituire con quelli della build effettiva. Eseguire lo strumento dal checkout Aurora; registrare toolchain `nm` e risultato. Verificare anche che l'eboot dentro il VPK corrisponda al SELF consegnato.

Gli strumenti Strikers già disponibili, con schema controllato nell'audit:

```sh
cd /Users/robin994/Documents/Code/strikersVita
python3 smstrikers-port/tools/vita_workflow_device.py --help
python3 smstrikers-port/tools/analyze_vita_performance.py --help
python3 smstrikers-port/tools/test_vita_performance_analysis.py
```

`vita_workflow_device.py` espone `backup`, `install`, `run`, `fetch`, `restore`, `restore-config`, con `--host`, `--out`, `--baseline`, `--self`, `--frames`, `--skip`, `--set KEY=VALUE`. Usare i parametri della console e degli artifact verificati, senza riutilizzare automaticamente un indirizzo o un backup storico. Verificare nel codice la procedura di ripristino per la versione corrente. `analyze_vita_performance.py` analizza snapshot consumer diagnostici; per gli FPS quieti usare l'analizzatore frame e i suoi test già presenti nel progetto, non interpretare i timer consumer come FPS.

Non lanciare raccolte hardware concorrenti: un helper che cambia INI/eboot invalida l'identità delle altre. Al termine salvare un manifest di ripristino con hash riletto dalla console.

## Scheda di chiusura del ticket

Ogni ticket consegna questo insieme di dati, compilato con valori reali o con “non eseguito”:

| Campo | Contenuto richiesto |
|---|---|
| Identità | Ticket, commit/diff, hash SELF/ELF/map, INI, flag attivo e fallback |
| Ipotesi | Quale costo doveva scendere e quale contatore lo dimostra |
| Correttezza host | Test nuovi, oracle, casi avversi, fixture/seed, sanitizer |
| Correttezza Vita | Replay A/A e A/B, shader reali, immagini/diff e regioni osservate |
| Stabilità | Durata, transizioni, suspend/resume, memoria e dump |
| Prestazioni | Coppie A/B, rumore A/A, media/P95/P99/deadline hit, simulazione/presentazione |
| Conclusione | Accettato, scartato, oppure host pronto/hardware pendente; motivazione |
| Ripristino | Configurazione finale e hash verificato |

Un ticket può concludersi correttamente con un'ipotesi smentita. Non va chiuso come “ottimizzazione riuscita” solo perché compila, riduce righe di shader o aumenta il cache hit-rate.
