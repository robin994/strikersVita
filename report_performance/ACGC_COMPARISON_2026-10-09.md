# Strikers Vita: costo ricorrente e confronto con ACGC-Vita

Data: 2026-10-09. Richiesta: identificare il lavoro pesante, verificare cosa riutilizzare dal port Animal Crossing a 60 FPS e chiarire se eliminare i fallback GX.

## Esito dell'audit

La conversione degli asset non sostituisce il percorso di preparazione della draw. Strikers usa già GXM direttamente e riusa i buffer residenti, ma continua a eseguire callback GLX, comandi di stato GX, traduzione e preparazione degli uniform. Il prototipo mode 2 intercetta soltanto lo slot della display list: conserva questo lavoro precedente e aggiunge gestione delle ricette. Le ultime prove quiet lo collocano a 16,762 FPS contro 20,391 FPS OFF; resta disattivato.

ACGC fornisce tecniche applicabili, soprattutto deduplicazione dello stato e fusione locale delle draw. Alcune sono già presenti in Aurora. Il suo risultato a 60 FPS è riportato dall'autore nelle release, non misurato da noi; non quantifica il guadagno ottenibile su Strikers.

Questo lavoro confronta sorgenti e catture archiviate. Non modifica renderer, flag, pacchetti o console e non esegue nuove prove hardware.

## Provenienza

- ACGC: ramo **vita**, commit/tag v0.5.0 `e6a258f342c3ec0b0d67aab3d2110da484282d0b`. Il default `master` contiene il port PC; non è il sorgente usato per il confronto Vita.
- Fork vitaGL associato: ramo `async-compressed-tex-prep`, HEAD osservato `6e900026f25ef36998deaf60671f13e772767e43`. Questo HEAD non prova quale libreria sia stata linkata nel VPK della release.
- Root Strikers HEAD `031923fbe23240eea4b698e5c9befc893e006372`; Aurora embedded HEAD `6acc6a889d0bec1fb206b1c73cb693a434c132df`, con modifiche locali. Il binario è identificato dai manifest, non soltanto dagli HEAD.
- Diagnostica mode 2: SELF `173f4dfb2d8c1c56503aff9668f86aa2f46ac54480429fae94bfac338855e705`, CSV SHA-256 `4ece0b267ffbb1ab5e87eaf1edd496c5447a84662e4b7e37b4e9b32704675886`; 120 snapshot consumer, endpoint 391–678, 168 frame non campionati.
- Sidecar AVNR locale SHA-256 `3d2d10cd625555b0208127aa60b8cd5507261909482b2842b4a8b44f92585c37`, ricalcolato in questo audit e coincidente con il precedente readback completo della console. Nessun nuovo readback del dispositivo in questa fase.
- [Evidenze consolidate](../ab-artifacts/acgc-comparison-20261009/comparison-evidence.json).

## Quale lavoro stiamo pagando

La catena corrente è: aggiornamenti del gioco e dell'istanza → callback GLX → stato/comandi GX → consumer Aurora → preparazione della draw → binding GXM → esecuzione GPU. AVNR interviene nell'ammissione della geometria nella cache, non sostituisce automaticamente questa catena.

Mediane del campione diagnostico **ON mode 2**, in millisecondi:

| Scope | Mediana | Interpretazione |
|---|---:|---|
| Draw frontend | 32,783 | Scope ampio della preparazione, contiene sottofasi |
| Traduzione stato | 7,406 | Pipeline, layout, stato vertex e memo |
| Costruzione comandi | 3,169 | Preparazione dei packet per il renderer |
| Decode vertex | 2,816 | Lavoro rimasto nei percorsi che lo richiedono |
| Cache geometrica | 2,153 | Lookup, validazione e ammissione |
| Risoluzione texture | 1,072 | Binding e risorse correnti |
| Risoluzione pipeline | 0,134 | La ricerca della pipeline già in cache è relativamente piccola |
| Submit | 5,549 | CPU nel submit; non tempo GPU dello shader |
| Attesa del produttore | 26,261 | Backpressure verso il consumer; non una misura autonoma della GPU |

**Non sommare queste righe.** Sono scope annidati o thread sovrapposti, con diagnostica/censimento attivi. Non sottrarle dai 49,041 ms del controllo quiet OFF: configurazione e finestra sono diverse. Non è ancora disponibile una scomposizione completa e indipendente del tempo GPU per i materiali correnti.

Nel codice `GXState::mark_dirty()` senza argomento invalida tutti i domini. Il ramo invariato di `mark_pipeline_state_dirty_if()` chiama ancora quel metodo. Alcune scritture semanticamente equivalenti possono quindi far ricostruire dati successivi. Questo è un punto concreto da attribuire per famiglia di registri, non autorizza a disabilitare globalmente gli aggiornamenti.

I piccoli esperimenti A5 XF/TEV già implementati non hanno dimostrato un vantaggio sostenuto nei campioni disponibili. La strategia di ACGC va applicata ai domini reali ancora costosi, non ripresentata come una funzionalità mancante generica.

## Perché le ricette attuali non bastano

Il delta diagnostico contiene 32.988 tentativi consumer, 2.543 draw native, 30.445 fallback e 30.414 nuove compilazioni. Il riuso è 7,71% dei tentativi. Il produttore conserva 64 stamp, il consumer 64 entry e il limite complessivo è 96 ricette vive. All'arrivo della 65esima entry il consumer svuota tutta la mappa.

Su un primo utilizzo il consumer non ha ancora la ricetta preparata: esegue la draw ordinaria e può usarla per preparare quella successiva. Se la ricetta viene sostituita prima di essere riutilizzata, si paga la gestione aggiuntiva senza recuperare il costo. I conteggi supportano questa spiegazione, ma non distinguono ancora tutte le cause di ogni fallback.

Anche un hit mode 2 conserva il lavoro GLX per istanza, il pin/validazione della display list e la risoluzione dello stato corrente. Non equivale a un compilatore persistente mesh/materiale con un packet d'istanza minimo.

## Copertura reale degli asset

Il pack verificato contiene **4.822 geometrie AVNR v2 statiche e zero record con selettore PN per vertice**. Il convertitore GLG salta sezioni animate e costruisce le richieste degli stream statici. La famiglia indexed-PN dei personaggi non viene quindi precompilata da questo adattatore nella sua rappresentazione GPU animata.

Nel campione mode 2 i contatori di ammissione AVNR v2 passano da 306 a 392 hit, e da 1.970 a 3.900 tentativi: delta **86 / 1.930**. Non sono contatori di draw residenti. Zero nuove ammissioni in un tratto non significa riconversione, né assenza di riuso: una geometria già residente non richiede un'altra lettura del sidecar.

La cache geometrica osservata è quasi al limite 8 MiB; fra gli endpoint si contano 57.632 hit e 8.908 miss. Miss, fallback, evizioni, risorse ritirate e ammissioni AVNR sono contatori distinti. La cache include anche byte di validazione e risorse ritirate: il consumo osservato non coincide soltanto con i buffer GPU utili. Attribuire per modello prima di cambiare il budget.

## Cosa riprendere da ACGC

La [release v0.2.0](https://github.com/Brendonm17/ACGC-Vita-Port/releases/tag/v0.2.0) attribuisce i 60 FPS nelle scene dense a copertura shader, correzione del culling, draw merging e riduzione delle chiamate ridondanti. Riporta una riduzione delle draw del 28–58% in aree aperte. Sono risultati dell'autore su quel gioco.

| Tecnica verificata | Applicazione a Strikers |
|---|---|
| Confronto per gruppi di stato prima degli upload | Invalidare soltanto i domini modificati; mantenere completi gli snapshot d'istanza |
| Shader scelto nuovamente solo quando cambiano le proprietà rilevanti | Aurora ha già generazione e cache per pipeline; attribuire il costo GPU prima di aggiungere varianti |
| Fusione di draw adiacenti compatibili, fermandosi alle copie EFB | Misurare opportunità sul percorso residente, attualmente distinto dal batching streamed |
| Correzione dei bounds di visibilità | Verificare ciò che Strikers invia fuori vista; non è dimostrato lo stesso bug |
| Cache di programmi, texture e stream GXM | Già presente in Aurora; misurare gli effettivi cache miss e le chiamate rimaste |

Sorgenti controllati:

- [Deduplicazione e merging ACGC](https://github.com/Brendonm17/ACGC-Vita-Port/blob/e6a258f342c3ec0b0d67aab3d2110da484282d0b/vita/src/vita_gx_cmdbuf.c#L32): confronto di gruppi, reset al cambio shader; fusione al blocco intorno a L1617, con controlli su stato, indici ed EFB.
- [Selezione shader ACGC](https://github.com/Brendonm17/ACGC-Vita-Port/blob/e6a258f342c3ec0b0d67aab3d2110da484282d0b/pc/src/pc_gx_tev.c#L1010): cache guidata dai cambiamenti pertinenti; conserva shader generali per configurazioni non coperte.
- [Culling ACGC](https://github.com/Brendonm17/ACGC-Vita-Port/blob/e6a258f342c3ec0b0d67aab3d2110da484282d0b/src/game/m_actor.c#L295): il ramo Vita corregge l'ampliamento eccessivo dei bounds della free camera.
- [Cache GXM del fork vitaGL](https://github.com/Brendonm17/vitaGL/blob/6e900026f25ef36998deaf60671f13e772767e43/source/custom_shaders.c#L586), [reset alle scene](https://github.com/Brendonm17/vitaGL/blob/6e900026f25ef36998deaf60671f13e772767e43/source/gxm.c#L1250).

Non si trasferisce il renderer vitaGL a Strikers. Si applicano le idee al backend GXM esistente. Aurora già evita texture/stream/setter identici all'interno della scena e ripristina lo stato a BeginScene. Copiare quel livello non rimuove il costo del frontend precedente.

Il batching Aurora esistente viene offerto alla coda **streamed**; il ramo **gpuGeometry residente** accoda direttamente il proprio packet e invalida `queuedPipelineValid_`. Attivare soltanto `gxm_local_draw_batching` non dimostra dunque fusione delle draw residenti dominanti. Anche lì pose e illuminazione differenti limitano quali draw possano essere unite senza cambiare il risultato.

## I diversi significati di fallback

| Tipo | Cosa significa | Come ridurlo |
|---|---|---|
| Ricetta nativa → GX ordinario | Contratto rapido assente o invalidato; la draw prosegue verso GXM | Identità persistente, copertura, cache e invalidazione corrette |
| Geometria residente → preparazione CPU | Sorgente variabile, layout non ammesso, cache/budget o altro rifiuto | Coprire la rappresentazione animata e attribuire le cause |
| Materiale nativo → generatore TEV completo | Operazioni non supportate dalla variante aritmetica ristretta | Implementare specializzazioni semanticamente equivalenti |
| Texture/media native → percorso originale | Record/formato/revisione non disponibili o non validi | Coprire i casi effettivamente usati e misurarne il costo |

ACGC chiama fallback l'uso del suo shader universale. Questo non coincide con il nostro contatore delle ricette. Aurora produce già shader per la configurazione TEV anche quando usa il generatore completo; una precedente cattura A1c registra 3.218/3.218 draw con `program_native=1`, su un SELF differente, senza implicare assenza del frontend GX.

Per classi implementate integralmente si può evitare il traduttore GX nel percorso normale. Per azzerare tutte le occorrenze occorre coprire tutti i contratti incontrati: animazione, materiali, copie EFB, texture, cambi scena. Eliminare il ramo di fallback prima di questo completamento omette draw o usa stato non valido. Il criterio corretto è eliminare le cause e misurare copertura, immagini e tempo, non forzare a zero il contatore.

## Prossime tranche concrete

1. **Identità e cache delle ricette.** Attribuire miss/evizioni/limite live/pin/guardie; sostituire la cancellazione totale con sostituzione selettiva e capacità coordinata. Tenere geometry budget 8 MiB. Testare invalidazione e risorse in volo, poi confronto immagini e ABBA. Gate: meno ricompilazioni e vantaggio quiet misurato, non solo più hit.
2. **Mesh/materiale persistenti e istanza dinamica.** Compilare una classe frequente all'ammissione del modello, con handle di mesh/pipeline e semantica texture; inviare soltanto palette/matrici/luci/binding e scissor correnti nella coda ordinata. Evitare l'emissione e rilettura del materiale GX per ogni submesh. Includere selettori PN dove presenti, senza congelare l'animazione. Gate: medesime draw, ordine e risultato in Characters/Shadowed e transizioni, con vantaggio oltre la variabilità.
3. **Invalidazioni per dominio.** Selezionare una famiglia di registri dove il profiler dimostra lavoro duplicato. Confrontare lo stato decodificato prima di invalidare; test differenziali contro ricostruzione completa. Non usare un hash dello stato intero come nuova spesa per draw.
4. **Draw e GPU.** Misurare visibilità e possibilità di unire draw residenti adiacenti. Attribuire separatamente le pipeline fragment pesanti prima di specializzarle, mantenendo alpha/depth/scissor e copie EFB. Non attraversare clear/copy/barrier né togliere passate per ottenere FPS.

Queste tranche non sono implementate da questo audit. Gli esperimenti già esistenti A3/A4/A5 non vanno descritti come vantaggi hardware acquisiti. Il budget target rimane 16,667 ms contro 49,041 ms dell'ultimo controllo quiet: circa 66% di riduzione, senza promessa che una singola tranche lo raggiunga.

## Evidenze locali correlate

- [Ultime prove delle ricette](NATIVE_MODEL_TEXTURED_2026-10-09.md).
- [Copertura del pack](../ab-artifacts/native-runtime-audit-20261009/pack-coverage.json).
- [Tempi diagnostici](../ab-artifacts/native-textured-20261009/nt-pin-diag1/consumer-summary.json).
- [ABBA quiet](../ab-artifacts/native-textured-20261009/quiet-abba-summary.json).
- [A5 e risultati precedenti](A5_GX_STATE_AND_DL_2026-10-08.md).
- [Precedente A1c, binario distinto](AURORA_CHARACTERS_SHADOWED_2026-10-08/FIRST_HARDWARE_CAPTURE_2026-10-08.md).

Nessuna modifica al renderer o al dispositivo; nessun nuovo build/test hardware, commit o push. Sorgenti esterni conservati soltanto negli artefatti locali ignorati.
