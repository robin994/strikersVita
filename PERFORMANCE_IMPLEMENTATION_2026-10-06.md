# Strikers Vita: implementazione del piano prestazioni

Il candidato del 6 ottobre implementa i percorsi P0–P5 del piano
[PERFORMANCE_60FPS_PLAN_2026-10-06.md](PERFORMANCE_60FPS_PLAN_2026-10-06.md).
La misura successiva di P0 dimostra un carico GPU rilevante: P6 richiede quindi
l’attribuzione dei draw e dei materiali. Il generatore aritmetico è già presente. Non sono stati eliminati TEV, callback,
animazioni, passate o simulazione per ottenere un numero FPS maggiore.
LOD resta fuori dallo scope concordato.

## Cosa è cambiato

| Punto | Implementazione concreta | Controllo INI / fallback |
|---|---|---|
| P0 | Snapshot del consumer già completato, senza `run_sync`; collector limitato e scrittura finale, contatori GX/cache/upload/attese/pool | `performance_capture=<path>`, `diagnostics=1`; assente = OFF |
| P1 | Percorso GPU dinamico disponibile e confronto hardware CPU/GPU; skin packets separati | `gxm_streamed_vertex_gpu`, `vita_skin_packets`, default 0 |
| P2 | Descrittore validato e riutilizzato sul consumer GX per liste immutabili con un solo draw; CP/XF/BP precedenti mantengono l'ordine | `gxm_prepared_dl=1`; parser originale per gli altri casi |
| P3 | Quarta lane per pose/preparazione con output privati e commit nel medesimo ordine; storage skin a quattro lane, granularità adattata, pool-busy misurato | `vita_game_core3=1`; quota CPU3 esistente, default tre lane |
| P4 | Eleggibilità immutabile distinta dalla proprietà cache, budget dal layout nativo realmente preparato, conversione esatta BC1 conservativa, GLT nativi nel PSARC | `gxm_exact_bc1=1`, `gxm_native_assets=1`; conversione originale negli altri casi |
| P5 | Buffer immutabili nel pool CDRAM con fallback USER; GLG statici canonici compilati offline e consumati su ammissione cache | `gxm_resident_cdram=1`, `gxm_native_assets=1`; default USER/GX |
| P6 | Generatore aritmetico già attivo; scene finish e diagnosi per draw per individuare le specializzazioni utili | Nessuna rimozione semantica speculativa |

Le opzioni sperimentali hanno default zero. Il codice conserva l'unico
proprietario GX/GXM, le invalidazioni di sorgenti/palette, il ritiro delle risorse
in uso dalla GPU e il reset di stato ad ogni BeginScene. Non sono introdotte
riorganizzazioni di draw attraverso copie EFB, target o barriere.

## Modalità demo esclusivamente nell'INI

```ini
vita_test_match = 0
vita_frameskip = 0
```

Il valore 1 di `vita_test_match` avvia la partita automatica CPU contro CPU dei
test; zero mantiene il frontend normale. Il test helper scrive 1 in un INI
temporaneo, registra il suo hash e ripristina il file originale al termine.
Non esiste un nuovo avvio demo forzato a codice. L'attract demo originale del
titolo conserva il comportamento del gioco. Tutti i confronti usano frameskip
zero e timestep 16,666666667 ms.

Il runtime aggiorna il blocco di default gestito all'avvio: l'hash del file
scaricato dopo il test può quindi differire da quello promosso prima del launch.
Sono conservati entrambi gli hash e l'INI esatto all'avvio. I valori di tutte le
override vengono verificati nuovamente nel file scaricato secondo la regola
first-value-wins; il prefisso di test precede i default gestiti.

## Identità del candidato hardware

- SELF / eboot.bin: `4836a4096dcfcf0285bddff758393d0797847cfbadddaf5ade050cefa011ef48`.
- VPK: `c29cb7ed3e2f340c5aa07550a6a400118c8540f73a0781d4bca188b3a64cbccd`.
- ELF: `899813d6ee1c0292948ea077f31ed2414a0d4ef2741ae9712b1784d388ea7426`.
- Map: `2d0c181fb575d6fd64e50a58d57a12a6029bd7e9317506bf51813dbd3e3b0ab8`.
- VELF: `19c411f2d4e433fe731c4cf77f24ae524a21f817562a2651c84346fa46b1488a`.
- Cartella locale: `ab-artifacts/performance-plan-20261006/build-token/`, con
  snapshot SHA-256 dei sorgenti compilati e file necessari alla simbolizzazione.
- Build Release/LTO, audio e GX asincrono attivi, renderer nativo GXM; nessuna
  dipendenza GL/vgl/vita2d nel controllo dei 12.710 simboli eseguibili.
- Installazione con upload temporaneo, rilettura integrale e confronto hash;
  anche l'eboot contenuto nel VPK coincide con il SELF installato.

Il candidato include `native_asset_archive` e la cache prepared con token
immutabili. I candidati intermedi e472 e 34b50 sono conservati rispettivamente
in `build-final/` e `build-native-cache/`, con gli artefatti per simbolizzazione.
I campioni di SELF diversi restano separati dai confronti del candidato finale.

Il codice prima del nuovo piano era già stato pushato: root `89d02026`,
Aurora `55b6ddf4`. La base hardware b732 è conservata con ELF/map/VELF/VPK. Il nuovo renderer
Aurora è pushato su `vita-experiment` al commit
`2f2259b055a14cc96fc2bb40cd824e05b8017dbc`, verificato contro la ref remota.
La correzione dell’API BC1 è pushata e verificata al commit
`a1d9b6624353686f9a507863f28dcc13f1d75308`.

Un controllo successivo dell'API pubblica ha reso esplicito `Config::exactBc1`
anche per create/update texture diretti. Il percorso facade di Strikers continua
a usare il payload preparato e non cambia. La build successiva è conservata in
`build-public-bc1/`: SELF
`73a7c0f6b39486b61ceb1b106a367b2b259911b1aec27f22adb7a785ddfb2dcd`,
VPK `3470566f54b48e4276f100371b91bcb685c09786b6c9fd40beeb59f28d9341b6`.
Audit GXM: 12.713 simboli, 18/18 test host superati. Il manifest registra una
sola differenza di sorgente compilato rispetto a 4836 (`gxm_renderer.cpp`).
I confronti del SELF 4836 restano attribuiti a quel binario; la sola build non
estende automaticamente le misure FPS al SELF 73a7.

## Asset e spazio

Il compilatore AVNR e il reader Aurora sono riutilizzabili. L'adattatore GLT/GLG
è specifico di Strikers; un altro port deve fornire il suo adattatore e reader.
Il formato contiene campi a larghezza fissa, la richiesta originale completa
e un checksum del payload. Il consumer confronta esattamente la sorgente:
l'hash è solo una chiave di lookup. Tutti i file `files/` e `sys/` originali sono
conservati e verificati byte per byte. Il primo upload/pack e i materiali restano
attivi: una sidecar non rende gratuito il rendering.

| Archivio | Dimensione | Risparmio rispetto alla ISO |
|---|---:|---:|
| ISO sorgente | 1.459.978.240 byte | — |
| PSARC originale | 623.698.090 byte | 57,28% |
| PSARC con sidecar native compatte | 646.949.372 byte | 55,69% |

Il candidato occupa 23.251.282 byte in più (+3,73%); le sole sidecar native
sono compresse, risparmiando 119.480.019 byte. I blocchi di gioco originali
restano stored, così il percorso DVD normale conserva il costo precedente.
SHA-256 candidato: `791e11c0884b8a381c82860b813c781c047bfed91c917634bd3017e039182916`.

La variante `--native-only` produce `sms-native-cache.psarc` di 23.259.473 byte,
SHA-256 `6c6b9015717f9dc16740023e32f23002887b0220208bd2d0896c3b342f9c9884`.
Contiene soltanto 4.825 record unici dopo deduplicazione. Con
`native_asset_archive=ux0:data/strikersVita/sms-native-cache.psarc` il gioco legge
sempre il PSARC originale; Aurora apre la cache separata sul consumer. Il totale
è praticamente identico alla variante combinata, ma gli aggiornamenti trasferiscono
23 MB invece di 647 MB. Senza questa chiave, il reader usa l'archivio combinato.

6.954 richieste offline: 5.981 geometry e 3 texture compilate, 970 texture con
fallback RGBA escluse per evitare di gonfiare l'archivio. Tre bundle GLG legacy
sono rimasti sul percorso originale. I dati retail e gli artefatti compilati
restano locali e fuori dal commit. Vedi
[tools/NATIVE_ASSETS.md](smstrikers-port/tools/NATIVE_ASSETS.md).

## Verifiche locali

- Strikers: 21/21 test superati, inclusi l'equivalenza delle quattro lane skin,
  il reader PSARC reale, l'adattatore GLT/GLG con limiti e fallback e
  la distinzione tra contatori cumulativi, frame saltati e attese per frame.
- Aurora: 18/18 test superati; liste preparate sul consumer, aggiornamenti di
  stato/layout, sorgenti/checksum/bounds native e texture compatte.
- ASan/UBSan: 5/5 verifiche mirate superate.
- PSARC: blocchi originali stored e native compressi verificati con il reader C,
  roundtrip completo, archivio deterministico e gestione alias deduplicati.
- Vita Release/VPK e audit GXM superati. Questi risultati non provano FPS o
  equivalenza visiva sulla console.

## Evidenza P1 e profilo preliminare

Il confronto P1 usa il medesimo SELF b732, 1.200 frame live 600–1799, diagnostica
e screenshot OFF, CPU/GPU reali 444/222 MHz.

| Percorso | FPS sul tempo totale | Mediana | P95 |
|---|---:|---:|---:|
| CPU, primo controllo | 30,879 | 39,266 ms | 41,421 ms |
| GPU dinamica | 25,253 | 39,923 ms | 43,994 ms |
| CPU, secondo controllo | 24,677 | 40,532 ms | 44,966 ms |

Il primo controllo contiene fasi più rapide che alzano la media. Non dimostra
un guadagno ripetibile del percorso GPU: le mediane restano circa 39–40,5 ms.
Le finestre live e il seed non rendono identici tutti i draw e la temporizzazione
delle animazioni; non attribuire la differenza delle medie al flag da solo.
Il default rimane zero. Le schermate confermano modelli/campo/HUD visibili,
ma non costituiscono equivalenza pixel per pixel.

Una raccolta preliminare P0 (SELF 51c0, precedente ai timer alleggeriti) include
300 snapshot live. È pesantemente perturbata dai timer su ogni comando GX e
non è una misura FPS. Mediane: 290,5 draw, 72.737 vertici totali, 70.835 GPU;
zero upload texture. La cache geometria è quasi piena e ricambia risorse.
Il candidato finale sopprime i timer CP/XF/BP per comando nell'attribuzione
e legge snapshot completati senza fermare il consumer. Scope annidati e thread
sovrapposti non si sommano. Le attese scene con finish sono residuali e
serializzano la pipeline; non equivalgono al tempo completo GPU.

## Risultati hardware del candidato finale

Le acquisizioni e i manifest sono in `ab-artifacts/performance-plan-20261006/`.
I seguenti run usano lo stesso SELF 4836, PSARC originale, shader WARM,
1.200 frame live 600–1799, diagnostica/overlay/screenshot OFF, frameskip zero,
clock effettivi 444/222 MHz. Ogni opzione è cambiata separatamente.

| Variante | FPS sul tempo totale | Media | Mediana | P95 | P99 | Entro 16,67 ms |
|---|---:|---:|---:|---:|---:|---:|
| Draw preparati | 24.107 | 41.482 ms | 41.104 ms | 46.124 ms | 66.450 ms | 0.00% |
| Controllo | 24.522 | 40.779 ms | 40.504 ms | 45.717 ms | 67.039 ms | 0.00% |
| Quarta lane pose | 24.261 | 41.218 ms | 40.844 ms | 46.366 ms | 66.137 ms | 0.00% |
| Geometria CDRAM | 25.024 | 39.961 ms | 39.737 ms | 44.546 ms | 47.950 ms | 0.00% |
| BC1 esatto | 25.198 | 39.685 ms | 39.715 ms | 43.592 ms | 46.114 ms | 0.00% |
| Skin packets | 23.810 | 41.998 ms | 41.676 ms | 46.366 ms | 67.530 ms | 0.00% |

Sono acquisizioni complete, non una partita intera o tutti gli stadi. Seed e
finestra live non garantiscono draw/NIS identici: piccole differenze richiedono
ripetizioni e controlli. La configurazione normale conserva le opzioni
sperimentali OFF; nessuno dei campioni soddisfa il budget di 60 FPS.
Il controllo ripetuto passa da 24,522 a 25,331 FPS (mediana da 40,504 a
39,455 ms): la variabilità tra run supera i piccoli vantaggi osservati con
CDRAM e BC1. Questi dati non giustificano l'abilitazione di un nuovo default.
Il controllo ripetuto è anch’esso completo: 1.200 frame, 25,331 FPS,
media 39,478 ms, mediana 39,455 ms, P95 43,890 ms, P99 45,887 ms.
Le immagini USB del percorso prepared e CDRAM mostrano campo, personaggi e HUD
senza anomalie evidenti; non provano equivalenza pixel per pixel.

Il primo campione prepared del SELF e472 è uscito dall'app prima di completare
il CSV: il dump mostra una CPU data abort sul thread GX in `_malloc_r`. È
escluso, insieme al retry interrotto. Il controllo e472 completo resta una misura
separata: 24,981 FPS, mediana 40,103 ms, P95 44,225 ms.

## Dump del primo candidato prepared e correzione

- Dump `psp2core-1791272585-0x0002a7397f-eboot.bin.psp2dmp`, SHA-256
  `70803895ad262f6fd3ea86dc1a6b2920d674154779353461752b5c786468eccb`.
- SELF e472, `gxm_prepared_dl=1`, altre nuove opzioni zero, diagnostica OFF.
- CPU data abort `0x30004` sul thread `melee_gx_frontend`; gli altri thread
  non riportano un'eccezione. PC `0x814a079e`, offset modulo `0x43b79e`,
  simbolizzato sul suo ELF in `_malloc_r`, newlib `mallocr.c:2514`.
- Lo stack porta a `std::deque<DrawPacket>::emplace_back()` e `DrawSink::submit`.
  L'allocatore tenta di seguire collegamenti `0x966b14a8` / `0x79ea13ea`: la
  memoria indicata contiene un pipeline key e campi di DrawPacket, non una
  free-list valida. Non è un dump GPU e non prova da solo l'origine della
  corruzione. Il dump e il matching ELF/map/VELF/INI sono conservati localmente.

La nuova cache di descrittori non alloca più sul percorso caldo e non mantiene
weak/shared reference aggiuntive delle display list. Una tabella fissa di
2.048 slot conserva soltanto i metadati sotto un'identità monotona assegnata
alla copia immutabile sul produttore e trasportata nel job FIFO. Revisioni,
replacement, eviction e clear generano identità nuove; i pin FIFO esistenti
mantengono la proprietà dei byte. Lo stride corrente resta parte dell'ammissione.
I test coprono riuso, invalidazione e lifetime della vecchia copia. La stabilità
Vita va verificata prima di attribuire definitivamente il dump a questa cache.

SELF della correzione: `4836a4096dcfcf0285bddff758393d0797847cfbadddaf5ade050cefa011ef48`;
VPK `c29cb7ed3e2f340c5aa07550a6a400118c8540f73a0781d4bca188b3a64cbccd`.
Matching ELF/map/VELF e source identity: `build-token/`. Audit GXM: 12.710 simboli.
Le nuove raccolte controllano anche l'arrivo di dump, evitando di scambiare
un'app uscita e tornata su LiveArea per un test ancora in corso.


## Collo di bottiglia: attribuzione del consumer e della GPU

I run `token-profile-scenes`, `token-profile-native`, `token-profile-prepared`
e `token-profile-game4` usano SELF 4836, diagnostica attiva e 300 snapshot live
consecutivi dopo 300 di warmup. Il collector non introduce un `run_sync`.
I timer sui comandi CP/XF/BP sono OFF, ma i timer per draw e i log restano
perturbanti: queste finestre **non misurano gli FPS normali**.

Nel run scene, `gxm_disable=0x108` forza un completamento delle scene:
attesa GPU residua mediana **30,916 ms**, P95 32,314 ms, massimo 33,843 ms.
Il lavoro GX cumulativo cresce di 9.729.307 µs su 299 intervalli, cioè
**32,539 ms per frame completato**. L’attesa scene serializza CPU e GPU;
non va sommata al lavoro GX per descrivere il funzionamento normale e
non è un timestamp che misura l’intera esecuzione GPU.

Nel run native senza finish diagnostico (`0x8`), il lavoro GX è
**32,462 ms per frame completato**. Mediane dei suoi scope:

| Scope | Tempo | Interpretazione |
|---|---:|---|
| Draw frontend | 25,562 ms | Include altri scope; non sommare alle sue parti |
| Traduzione stato | 7,235 ms | Stato/materiali/layout e costruzione pipeline |
| Decode vertici | 2,985 ms circa | Decodifica dei draw che restano dinamici |
| Cache geometria | 1,831 ms | Lookup/validazione e recupero geometria |
| Submit | 4,565 ms | Tempo CPU di invio, non esecuzione GPU |
| Resolve texture | 0,927 ms | Lookup e binding, non upload continuo |
| Resolve pipeline | 0,114 ms circa | Lookup; le pipeline calde sono già compilate |

I valori esatti e la distinzione tra contatori cumulativi e per-frame sono
registrati in `consumer-analysis.json` di ciascun run. Nei campioni scene/native
la mediana è circa 282–290 draw e 72–73 mila vertici; circa 97% dei vertici usa
la GPU. Upload texture mediano e P95 sono zero. Nel warmup stabile ci sono
circa 229 hit cache geometria per frame e zero nuovi miss. Di conseguenza,
conversione offline e CDRAM agiscono soprattutto sull’ammissione delle risorse;
non cancellano il costo di ogni draw, dello stato, degli shader o delle passate.

La cache nativa viene realmente consumata: **295 hit geometria** sono già
accumulati al primo snapshot live, restano 295 all’ultimo, zero record rifiutati.
Il delta warm zero indica riuso della cache residente, non un reader inutilizzato.
Le liste preparate registrano 73.494 hit su 299 intervalli, senza nuovi miss o
rifiuti; il lavoro GX diagnosticato è 31,655 ms per frame, ma la misura quieta
non mostra un vantaggio ripetibile. Un numero alto di hit non prova un guadagno FPS.

CPU3 ha telemetria valida in tutti i 300 snapshot di ciascun run: massimo
osservato del carico totale 30,94% scene, 27,21% native, 35,12% prepared e
28,77% quarta lane. La quarta lane aggiunge 8.634 chunk, 17 dinieghi e un
overrun di quota nell’intervallo; gli altri tre run non hanno nuovi dinieghi,
errori di telemetria o overrun. Gli overrun accumulati prima della finestra
restano visibili nei contatori lifetime. Nessun nuovo fallback pool-busy.
Questi campioni non dimostrano il massimo tra due letture né sotto ogni
possibile carico esterno. La quarta lane rimane OFF.

Il limite verso 60 FPS riguarda quindi **il rendering continuativo CPU e GPU**,
non principalmente il caricamento degli asset. Da circa 40 ms a 16,667 ms
occorre togliere circa 23,3 ms (58%). Raddoppiare l’efficienza da 25 FPS
porterebbe a 50 FPS. La GPU e il consumer si sovrappongono: un cambiamento
che accelera uno soltanto può lasciare invariato il frame limitato dall’altro.

## Aggiornamento verificato l’8 ottobre

Il run `token-native-quiet` era già terminato: 1.200 frame live 600–1799,
SELF 4836, CPU/GPU 444/222 MHz, 25,070 FPS, media 39,889 ms,
mediana 40,108 ms, P95 43,921 ms, P99 46,091 ms, 0% entro 16,667 ms.
L’uso delle sidecar non dimostra un aumento ripetibile degli FPS.

La suite successiva si è interrotta per console offline durante
`token-geometry16-quiet`: quel confronto, il repeat CDRAM e il probe GPU per draw
restano incompleti. Nessuna specializzazione alpha P6 è stata implementata.
L’8 ottobre il SELF remoto è stato riletto e coincideva ancora con 4836.
L’INI era rimasto nel test geometry16: è stato ripristinato e verificato al
normale SHA-256 `71812ffdfddfb02962019643f2647c45e1f47f3096b978aec6f19bf081e036a8`,
con `vita_test_match=0` e `vita_frameskip=0`.

Il dump successivo `psp2core-1791277863-0x00000b21bb-eboot.bin.psp2dmp`
identifica thread/app `WICMKW001` e modulo `wiicompiled-vita-game`:
non è attribuibile a Strikers. Gli altri dump nuovi non sono ancora classificati.
Gli artefatti di recupero e l’identità del ripristino sono in
`ab-artifacts/native-assets-20261008/recovered-device/` e `restored-config/`.
Le nuove conversioni media e i loro limiti sono documentati in
[tools/NATIVE_ASSETS.md](smstrikers-port/tools/NATIVE_ASSETS.md).
