# Strikers Vita: prima cattura A0/A1c su hardware - 8 ottobre 2026

## Provenienza e integrita

Questa analisi usa **dati realmente acquisiti sulla Vita** il giorno 2026-10-08
(cattura con mtime FTP `20261008154452`, UTC) e non sostituisce un quiet FPS
benchmark. I file scaricati e la cache shader stanno in:

`smstrikers-port/ab-artifacts/a1c-view-draw-20261008/run-20261008-1744/`

- `view-draw-raw.jsonl`: 2.248.258 byte, SHA-256 `dd3db8281a94d5f4120bec05bf6312db6c9bcf5e258752da4eacc38e0c79824f`.
- `view-draw-sealed.jsonl`: valida, schema `aurora-vita-view-draw-v2`, SHA-256 `b92cc6542b1af8d29295d1b02ebf398861338536560f54d39e50fc137a9c2c67`.
- `view-draw-analysis.json`: output del validator/parser `analyze_vita_view_draw.py`.
- SELF installato: SHA-256 `238307f3cc924c4dc9d2c0ce6aa2f468c5a673881d71c7a8669f5be1ecf163fc`.
- `strikers.ini` effettivo: SHA-256 `36f379edbd451bffc5a5637a54b135b598c58898d4b901dd1b22aea698b9ce77`.
- Cache GXP: **280 programmi**, 362.908 byte; hash deterministico della directory `c2219cd0b82b2017ab44d43f6fc9ef9237d124737821bd22a094211fa3f44997`. Tutti gli hash fragment usati risultano presenti nel cache dump.
- **12 frame consecutivi**, producer frame **1872-1883**.
- **3.590 eventi**, di cui 3.218 draw, 360 marker di view e 12 completamenti; **0 dropped**.
- Le quattro firme A1c (vertex/index/uniform/state) sono presenti in **tutti** i draw. Validazione `complete=true`.

## Breakdown di SHADOWED e CHARACTERS

| View | Draw nei 12 frame | Draw/frame | CPU submission GXM totale | CPU submission per frame | Vertici/frame | Uniform caricati o copiati per frame |
|---|---:|---:|---:|---:|---:|---:|
| SHADOWED (3) | 1.353 | 112,75 | 25,335 ms | 2,111 ms | 18.584 | 32,6 KiB |
| CHARACTERS (11) | 1.330 | 110,83 | 27,952 ms | 2,329 ms | 41.579 | 104,3 KiB |
| Tutte le view | 3.218 | 268,17 | 64,503 ms | 5,375 ms | 70.903 | 146,9 KiB |

Le due view producono insieme **2.683/3.218 draw, pari all'83,4%** del
totale. Il tempo misurato qui e il codice CPU nel percorso
`Renderer::draw`, **non** il tempo GPU degli shader, **non** il tempo della
traduzione GX/FIFO ed **esclude** il lavoro diagnostico di hashing eseguito
successivamente. I tempi del profiler per view di ottobre 2 riguardano un
altro binario e non possono essere sottratti direttamente.

Tutti i 3.218 draw registrati indicano `program_native=1`.

## Materiali prioritari

| View | Hash shader fragment | Draw/frame | Stage TEV | Texture mask | CPU submit/frame | GXP bytes |
|---|---|---:|---:|---|---:|---:|
| CHARACTERS | `bb9a168934f9e9dc` | 79,0 | 4 | `0x07` (3) | 1,722 ms | 1.348 |
| SHADOWED | `d35dfa69a3f7e9bc` | 40,4 | 2 | `0x03` (2) | 0,686 ms | 1.204 |
| SHADOWED | `e2b27aa4eb05ca46` | 20,0 | 5 | `0x0F` (4) | 0,378 ms | 1.464 |
| SHADOWED | `670925ff2d92a258` | 19,0 | 5 | `0x0F` (4) | 0,342 ms | 884 |

Nel menu runtime sono visibili almeno gli ultimi 8 caratteri dell'hash:
`34F9E9DC`, `A3F7E9BC`, `EB05CA46`, `2D92A258`.
Il menu OFF salta **interi draw**; un guadagno FPS in quella modalita non e una
misura del costo delle sole istruzioni fragment.

### CHARACTERS

Lo shader `bb9a168934f9e9dc` rappresenta **948/1.330 draw** dei personaggi
(**71,3%**). Tutti questi draw seguono il percorso **indexed PN nativo**, usano
una singola pipeline richiesta/attiva, e sono riconducibili a 29 diversi hash
di vertex payload e 29 index payload. Al contrario, sono stati osservati **948
uniform payload distinti su 948 draw**. Nello stesso segmento misurato sono
registrati **~96,6 KiB/frame** di upload/copie di uniform per questa pipeline
su un totale CHARACTERS di ~104,3 KiB/frame.

**Implicazione:** il semplice riuso dello snapshot intero non risolve il
problema; occorre profilare e distinguere gli uniform invarianti (materiale,
lighting) dai dati di posa/matrice che cambiano per draw. Validare eventuali
riduzioni con gli hash A1c dei payload e screenshot delle animazioni.

### SHADOWED

Tre shader contano **953/1.353 draw** della view (**70,4%**). Due usano **5
stage TEV e 4 texture** (`e2b27...`, `670925...`); il piu frequente
(`d35dfa...`) usa 2 stage/2 texture. I relativi asset GXP sono disponibili
nel dump della cache. Questo dato non dimostra ancora quale dei tre consumi
piu tempo **GPU**: servono misure per draw/materiale o un esperimento shader
equivalente, mantenendo depth, trasparenze, scissor e correttezza visiva.

## FPS: cosa non dimostra la cattura

Il file `native08-diagnostic.csv` sullo stesso device contiene 300 frame
con `match=0` e `match_frame=0`: riguarda l'**interfaccia/menu**, non il
gameplay. Non va interpretato come prestazione della partita. La cattura A0/A1c
introduce inoltre flush diagnostici ai confini delle view e letture per hash,
quindi non fornisce FPS quieti. Il riferimento empirico dell'utente resta
~22 FPS con tutte le view attive; l'obiettivo e 60 FPS a rendering completo.

## Azioni successive, in ordine di evidenza

1. **A3/CHARACTERS:** strumentare separatamente costruzione, upload e binding
   degli uniform nel percorso indexed-PN dello shader `bb9a...`. Valutare
   un fast path che aggiorni soltanto le costanti effettivamente variate, senza
   modificare il layout GPU o l'animazione; esperimento OFF di default e
   confronto A1c OFF/ON.
2. **A4/SHADOWED:** analizzare il codice GXP/TEV dei due shader 5-stage,
   4-texture e quello 2-stage piu frequente. Cercare operazioni ridondanti
   dimostrabili con oracle TEV/alpha/depth. Non eliminare passate e non
   disabilitare shader nella build ottimizzata.
3. **A2/GX:** nel frontend/draw sink misurare ricostruzione di stati e
   transizioni per gli 80+ draw ricorrenti per frame di SHADOWED e i 79
   CHARACTERS dominanti. Il valore ~5,38 ms/frame GXM CPU non cattura il
   costo precedente di traduzione GX; servono entrambi i lati.
4. **GPU:** attribuire su hardware il tempo di fragment, fill/overdraw e
   sincronizzazioni tramite strumenti diagnostici in esecuzioni separate, senza
   `sceGxmFinish` durante i benchmark FPS quieti.
5. **A7:** confrontare tre coppie quiet A/B di 1.200 frame dopo 600 warmup
   (`fixed_dt=0`, `vita_frameskip=0`, tutte le view e gli shader ON).
   Fotografare e confrontare CHARACTERS, SHADOWED, animazioni, goal/replay.

### Stato

**A0/A1c raccolti e validati su hardware.** Nessuna ottimizzazione A4/A5 e
stata attivata. Nessuna nuova build, installazione o modifica INI durante
questa analisi. I 60 FPS non sono ancora dimostrati.
