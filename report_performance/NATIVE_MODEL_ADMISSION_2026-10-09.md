# Strikers Vita: censimento dei rifiuti del percorso nativo

Data: 2026-10-09. Richiesta: proseguire verso 60 FPS verificando perché la prima ricetta nativa non riceve draw durante la partita.

## Risultato e perimetro

Due campioni hardware completati osservano **168.635 draw callback**: **zero ammesse dal filtro**, tutte con contesto di trasporto disponibile. Il filtro del prototipo non copre i packet effettivamente usati nella partita. Il 100% viene escluso per texconfig non nullo, handle texture nello stato del packet e stream aggiuntivi rispetto a posizione/colore. Nessuna draw ha un rifiuto di contesto worker/trasporto negli intervalli osservati.

È aggiunto un censimento diagnostico; la politica di ammissione e il percorso di draw rimangono quelli del prototipo. `gxm_native_model_census=0` e `gxm_native_model_draw=0` sono i default. Il censimento richiede `diagnostics=1`. Nessun incremento della cache geometrica, nessuna estensione automatica dei materiali e nessun guadagno FPS dimostrato.

**Prova completata. Binario e INI originali ripristinati e verificati byte per byte. Nessun nuovo coredump.** Entrambi i run hanno completato le finestre richieste; non è stato necessario riavviare la console per il ripristino.

## Metodo e limiti

I contatori sono cumulativi e coerenti sotto mutex; si usano le differenze fra gli endpoint, senza sommare le righe. I motivi di rifiuto possono sovrapporsi. Le osservazioni del produttore lette durante il completamento del consumer possono comprendere lavoro per frame successivi in coda: il denominatore è il numero di callback osservate fra snapshot, non un conteggio esatto delle draw di ciascun framebuffer. La tabella delle viste e quella dei programmi partizionano lo stesso totale.

Un classificatore diagnostico indipendente confronta il proprio verdetto con il filtro originale. In tutte le 420 righe dei due campioni le discrepanze sono zero; anche i primi endpoint, cumulativi dall’avvio, riportano zero ammissioni e zero ricette native. Gli esempi nel footer sono i primi dall'avvio del processo e possono provenire dall'introduzione; non sono esempi necessariamente appartenenti alla finestra misurata.

Questa è attribuzione diagnostica. I tempi non vanno confrontati con le acquisizioni quiet, e gli scope annidati non vanno sommati. Non è stato eseguito un nuovo A/B FPS perché nessuna draw viene ammessa dal prototipo.

## Implementazione e verifiche

- `src/NL/glx/glxSend.cpp`: raccoglie fatti su packet, vista, programma, texture, stream, modificatori e matrice prima del filtro esistente; registra gli stadi effettivi di inizio/cattura/coda.
- Aurora `lib/gx/native_model_census.hpp` e `fifo.cpp`: 27 motivi sovrapposti, 35 bucket vista, 9 programmi e 7 stadi; contesto worker, registrazione e limite ricette. Il controllo sul worker evita di leggere lo stato privato del produttore da un altro thread.
- `include/port/vita_performance_capture.hpp`: snapshot dei contatori e primi esempi nel CSV, separati dalle misure di tempo.
- `src/platform/config.c`, INI di esempio e analizzatore: opzione disattivata di default, commenti diagnostici e differenze dei nuovi contatori cumulativi.

Suite Aurora host **26/26**, test della configurazione e analizzatore **4/4** superati. Il nuovo test verifica motivi sovrapposti, limiti degli stream senza dereferenze aggiuntive, divergenze dal filtro e snapshot concorrenti; il test delle ricette verifica anche worker inattivo, callback sul worker e limite di 96 ricette vive. Build Vita e probe standalone riuscite. Audit ELF Strikers: **12.828** simboli eseguibili, draw/present GXM presenti, nessuna entry point o libreria GL/vgl/vita2d; probe **2.271** simboli. L'eboot nel VPK coincide byte per byte con il SELF.

Le modifiche antecedenti sono conservate in `ab-artifacts/native-admission-20261009/pre-edit/`; il candidato con ELF, map, VELF, SELF, VPK, build cache, patch e nuovi sorgenti è in `candidate/`. Il working tree era già modificato: HEAD da solo non identifica il binario.

## Protocollo e identità

Cache geometrica effettiva 8 MiB, arena sorgente 48 MiB RAM cached, seed `0x53545249`, dt reale, frameskip 0, clock effettivi dal CSV 444/222/222/166 MHz. Demo selezionata tramite INI temporaneo. Replay A6, probe riuso vertici e streamed GPU disattivati. Build `AURORA_VITA_ASYNC_GX=ON`, `STRIKERS_VITA_GX_THREAD=ON`, conservata dal checkout.

Nel primo run l'override della cache era stato scritto con il nome errato `gxm_static_geometry_mb`: è ignorato. Il default di build è 8 MiB e il log conferma esplicitamente `static geometry budget=8192 KB`. La seconda acquisizione usa il nome corretto `static_geometry_mb=8`. L'INI viene aggiornato dal normale refresh del blocco gestito all'avvio: sono conservati sia l'hash del readback prima del lancio sia quello scaricato dopo; gli override temporanei iniziali mantengono precedenza.

- Root HEAD: `031923fbe23240eea4b698e5c9befc893e006372`.
- Aurora embedded HEAD: `6acc6a889d0bec1fb206b1c73cb693a434c132df`.
- SELF census: `59c535851f928c030eb8cec0d56e84a3d1451f83854bbafc9c94becab8787901`.
- VPK census: `33e72f54fc235ac9bd3067ebb2dc5117a39418ae37c57ba8ea3c934a229f0f41`.
- SELF originale: `0de405e4fa3891e04b298d6e8b8198ca8fda7e7ddb3b57d08444f635337e45f3`.
- INI originale: `efb59a686ba0c3e0ffca1fbbb952d053859361ef556e0d59872fe2ff7bb0a40c`.

L'installazione e il primo fetch confermano lo stesso SELF candidato. La cattura USB mostra lo stadio; la snapshot live 120 mostra campo, HUD e personaggi. È un riscontro visivo qualitativo, non un confronto pixel, né una prova completa di audio/input/sospensione.

## Campioni hardware completati

| Run | Warm-up gameplay | Campioni consumer distinti | Frame consumer agli endpoint | Draw callback osservate |
| --- | ---: | ---: | --- | ---: |
| early1 | 60 | 120 | 403–522 | 31.903 |
| steady1 | 600 | 300 | 1084–1558 | 136.732 |

Il CSV principale contiene 120 frame live 60–179 e 300 frame live 600–899. La seconda acquisizione consumer contiene 300 snapshot distinti su 475 possibili frame nell'intervallo, quindi 175 frame non campionati. I contatori cumulativi coprono gli eventi fra endpoint; gli istogrammi dei tempi coprirebbero solo le snapshot presenti. Non si mescolano i due domini.

### Motivi di rifiuto

Le percentuali usano le draw callback osservate come denominatore. Le righe sono sovrapposte: non sommare le percentuali.

| Condizione incompatibile con il prototipo | early1 | steady1 |
| --- | ---: | ---: |
| Texture nello stato del packet | 31.903 (100.00%) | 136.732 (100.00%) |
| Texconfig non nullo | 31.903 (100.00%) | 136.732 (100.00%) |
| Stream oltre posizione/colore | 31.903 (100.00%) | 136.732 (100.00%) |
| Vista esclusa | 31.665 (99.25%) | 135.784 (99.31%) |
| Userdata presente | 26.919 (84.38%) | 113.745 (83.19%) |
| Programma diverso da 3d_unlit | 24.998 (78.36%) | 108.523 (79.37%) |
| Primitiva diversa da quella ammessa | 20.383 (63.89%) | 78.691 (57.55%) |
| Storage degli stream incompatibile con questa cache | 10.636 (33.34%) | 41.003 (29.99%) |
| Meno di 48 vertici | 7.704 (24.15%) | 33.446 (24.46%) |
| Modificatori attivi | 8.091 (25.36%) | 30.651 (22.42%) |
| Buffer non riconosciuto come display list | 4.595 (14.40%) | 20.081 (14.69%) |
| Stato dirty | 2.385 (7.48%) | 9.185 (6.72%) |

I contatori per worker inattivo, callback sul worker, callback dentro una display list, registrazione occupata e limite ricette sono tutti zero. Nessun tentativo di begin, nessuna ricetta catturata o accodata e nessun tentativo/fallback del consumer. Quindi l'assenza di hit nasce dal filtro applicato ai packet prima della registrazione.

`stream_storage` non significa asset malformati: il percorso originale ammette attributi diretti con indirizzo nullo e uno stream PNMTXIDX dedicato, mentre il prototipo richiede array residenti con indirizzo, stride e dimensione non nulli. Allo stesso modo, gli handle texture possono includere binding conservati nei sei slot: il contatore non misura quante texture vengano effettivamente campionate dalla GPU.

### Distribuzione delle viste e dei programmi

| Vista | early1 | steady1 |
| --- | ---: | ---: |
| Characters | 13.607 (42.65%) | 60.680 (44.38%) |
| Shadowed | 13.475 (42.24%) | 55.515 (40.60%) |
| UnsortedPerspective | 238 (0.75%) | 948 (0.69%) |
| Anark / HUD | 1.072 (3.36%) | 5.703 (4.17%) |
| CoPlanar0 | 1.103 (3.46%) | 3.825 (2.80%) |
| CoPlanar | 656 (2.06%) | 2.234 (1.63%) |

Characters + Shadowed: **84.89%** nel primo intervallo e **84.98%** nel secondo. Le due viste ammesse dal prototipo sono UnsortedPerspective e FrontEnd; FrontEnd non ha draw negli intervalli osservati. Aprire soltanto altre viste lascerebbe ancora i tre rifiuti al 100%.

| Programma GLX | early1 | steady1 |
| --- | ---: | ---: |
| 2d_unlit | 1.072 (3.36%) | 5.827 (4.26%) |
| 3d_unlit | 6.905 (21.64%) | 28.209 (20.63%) |
| 3d_pointlit | 13.377 (41.93%) | 54.398 (39.78%) |
| 3d_pointlit_dirt | 9.922 (31.10%) | 46.281 (33.85%) |
| 3d_crowd_lit | 627 (1.97%) | 1.902 (1.39%) |

Sono frequenze di callback, non percentuali di tempo CPU/GPU. La distribuzione indica dove cercare copertura, ma non quantifica il guadagno ottenibile. Il censimento non conta riconversioni di texture: i rifiuti del prototipo non dimostrano che gli asset nativi vengano riconvertiti.

## Passo successivo verso una variante utile

1. Estendere il contratto della ricetta a **materiali texturizzati, normali e UV**, con configurazione TEV/texgen, binding e revisioni delle risorse verificati. Iniziare da una classe esplicita e misurabile; gli altri packet conservano il fallback GX.
2. Gestire separatamente lo stato **per istanza/per frame**. Il codice originale interpreta `userData` come tabella di skin, luce, scissor, viewport e altri modificatori. La sua presenza nell'83–84% delle callback non prova che tutti quei packet usino skinning. Mappare i tipi presenti prima di ammetterli; conservare gli aggiornamenti di camera, pose, luce, scissor e matrici invece di ignorare il filtro.
3. Ammettere le viste Characters/Shadowed solo con quel contratto, preservando FIFO/EFB, snapshot immutabili, invalidazione e ripristino completo a BeginScene. Verificare nei test le transizioni reali e confrontare sul dispositivo immagini e contatori di copertura. Avviare poi A/B quiet ripetuti con stessa scena, clocks, warm-up e finestra.

La cache rimane **8 MiB**. Aumentarla non rende idonei questi packet; la prova a 16 MiB che aveva bloccato la console resta senza una causa attribuita. Nei campioni di memoria di questi run, `free_cdram=39845888` (38 MiB) e `fallbacks=0`, ma questa telemetria campionata non dimostra sicurezza di una cache più grande in tutte le scene.

Il riferimento quiet della prova precedente è **21,122 FPS / 47,344 ms** (SELF diverso, censimento disattivato), descritto in `NATIVE_MODEL_RECIPE_2026-10-09.md`. Per 60 FPS il budget è 16,667 ms: nessun guadagno di quella dimensione è dimostrato da questo censimento. La successiva estensione deve dimostrare sia copertura sia un vantaggio misurabile nel confronto quiet.

## Ripristino finale ed evidenze navigabili

L'app è stata terminata, l'INI originale ripristinato e poi il SELF originale promosso atomicamente. **Entrambi coincidono byte per byte con i backup iniziali**; il readback finale e l'elenco dei coredump confermano il ripristino e l'assenza di nuovi dump. Il ripristino dell'eboot è riuscito dopo kill, senza il problema FTP della sessione precedente e senza un nuovo riavvio. Non è stato riaperto il titolo dopo il ripristino.

- [Risultato consolidato e identità finale](../ab-artifacts/native-admission-20261009/experiment-result.json).
- [Manifest del candidato](../ab-artifacts/native-admission-20261009/candidate/manifest.json).
- [Identità finale sul dispositivo](../ab-artifacts/native-admission-20261009/final-device-identity.json).
- [Primo campione: motivi e conteggi](../ab-artifacts/native-admission-20261009/na-diag-early1/admission-summary.json).
- [Secondo campione: motivi e conteggi](../ab-artifacts/native-admission-20261009/na-diag-steady1/admission-summary.json).
- [Snapshot gameplay live 120](../ab-artifacts/native-admission-20261009/na-diag-early1/debug_frame_play_120.png).
- [Cattura USB dopo la seconda finestra](../ab-artifacts/native-admission-20261009/na-diag-steady1/usb-live.png).
- [Analisi riproducibile dei campioni archiviati](../ab-artifacts/native-admission-20261009/analyze-admission.py).
- [Test host 26/26](../ab-artifacts/native-admission-20261009/host-tests.log).

Gli artefatti locali sono conservati nel checkout; nessun commit o push è stato eseguito.
