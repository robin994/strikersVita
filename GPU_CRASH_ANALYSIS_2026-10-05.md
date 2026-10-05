# Strikers Vita: dump GPU del 5 ottobre 2026

Il dump conferma un **fault di memoria GPU in Strikers**. L'analisi dei sorgenti
ha individuato e corretto un'invalidazione incompleta delle recipe dei vertici.
Il test di regressione fallisce prima della patch e passa dopo; la nuova build
ha completato su Vita il percorso con screenshot al play-frame 60 e 1.200 frame
di gioco campionati senza un nuovo dump. Anche il secondo test lungo, senza
screenshot, ha completato 1.200 frame: **tre sessioni, 2.580 campioni live-play**.
Il dump identifica la pagina coinvolta, ma non il draw responsabile: il legame
causale tra il fault originale e il difetto corretto resta un'ipotesi.
Il confronto delle prestazioni tra candidato e riferimento resta da completare.

## Identità verificata

| Elemento | Evidenza |
| --- | --- |
| Dump | `psp2core-1791191504-GPUCRASH.psp2dmp` |
| Provenienza | FTP Vita `192.168.1.79:1337`, `ux0:/data/` |
| SHA256 dump | `cc35c0f9162ae8ada1a11a9c738e67951d16baf32935fee3364831bd42f24421` |
| Integrità | Gzip completo, ELF interno valido, 31 note e 31 PT_LOAD; nessun problema strutturale rilevato dal parser |
| Processo | `SMSVITA01`, PID `0x006b263d`, `ux0:/app/SMSVITA01/eboot.bin` |
| Eboot letto dopo il crash | `0563a5c5e7fd9c12fdd8b3305859d71132778874f94bbe95ae5664145c0bd4c4` |
| ELF candidato conservato | `91fd7ba1f77abd7230d3369d7d51ac8a0e1c7eb5931336649ec7b25b1f5e7b5e` |
| INI del run letto dopo il crash | `d90b3718a4bce66088caf558b293a5823182f27feda2c953f8cb02a6fbd09f76` |
| Configurazione del run | `gxm_disable=0x8`, WARM, diagnostica e overlay OFF, match automatico, filtro live-play, screenshot play-frame 60 |

L'eboot scaricato è identico al SELF candidato. Le dimensioni dei segmenti del
modulo `strikers` nel dump corrispondono all'ELF conservato: RX `0x4d3264`, RW
`0x108f68`. Questo sostiene l'associazione dei simboli; il dump non contiene
un hash crittografico dell'intero eseguibile caricato al momento del fault.

## Registri GPU e memoria

La TTY contiene `appmgr_aborthandler.c(793) render gpu crash`. Nessuno dei nove
thread CPU ha uno stop reason di eccezione. La classificazione generica
`NO_CRASH_THREAD` del parser descrive questo limite e non smentisce il crash GPU.

La nota `GPU_INFO`, versione 5, è completa e contiene `0x3404` byte. Il parser
la inventaria senza decodificarla. La tabella osservata di registri a quattro
colonne è stata estratta manualmente; la terza colonna contiene:

| Registro | Valore | Campi estratti |
| --- | --- | --- |
| BIF_INT_STAT (`0x0c04`) | `0x00090400` | request mask `0x0400`, fault type code `1`, flush-complete impostato |
| BIF_FAULT (`0x0c08`) | `0x72e14061` | pagina `0x72e14000`, CID `1`, sideband `6` |

Le altre tre colonne hanno request mask zero e BIF_FAULT zero. I valori sono
ai byte `0x1f70` e `0x1f84` della nota; gli ordinali delle colonne non sono stati
verificati come numerazione fisica dei core.

Le maschere sono quelle delle [definizioni SGX543 di Imagination pubblicate
nel kernel Android](https://android.googlesource.com/kernel/omap.git/+/9c15339f4f6060417c29f62ab7b739a8eb2244cb/drivers/gpu/pvr/sgx543defs.h).
È un'interpretazione del layout osservato, non una decodifica ufficiale completa
del formato Sony. Non è stata attribuita una semantica più specifica al request
bit o al fault type code senza una definizione verificata.

La pagina cade nel blocco `aurora-gxm`, UID `0x40010fcb`, intervallo
`0x72e14000..0x72e18000`, dimensione 16 KiB. Il blocco è presente nella mappa
CPU del processo. Questo non dimostra che il mapping o i permessi GPU fossero
validi al momento dell'accesso. Il contenuto del blocco non è catturato.

## Stato CPU e limiti dell'attribuzione

`melee_gx_frontend` gira su CPU2, priorità 160, PC `0x814945b8`
(`sceKernelAllocMemBlock`), LR `0x81289b29`
(`aurora::vita::gxm::Renderer::create_buffer`). La scansione dello stack trova
candidati in `StaticGeometryCache::get`, `BufferPool::create_vertex`, packing
dei vertici e `DrawSink::submit`. Non è un unwind causale verificato.

Il thread principale aspetta il frontend GX. I worker CPU1/2/3 risultano in
attesa; il worker CPU3 è effettivamente associato a CPU3. Non c'è evidenza di
utilizzo totale CPU3 entro il limite del 70% in questo dump.

La CPU stava allocando geometria quando il dump è stato catturato. Un fault GPU
è asincrono: quel PC non prova che l'allocazione corrente, la nuova recipe,
gli shader materiali o le texture siano la causa. Il dump non conserva i dati
dei buffer GPU né l'identità del draw responsabile. Il log memoria recuperato
è cumulativo e privo di identità di sessione: non viene usato per attribuire
esaurimento memoria al run del crash.

## Conservazione del dump originale

Il dump, i registri estratti, l'eboot, l'INI del crash e i simboli sono conservati
localmente in `ab-artifacts/native-workflow-20261005/gpu-crash-1791191504/`.
Questa directory è ignorata da Git; il push pubblica il report e i sorgenti,
senza dump, binari, log o dati retail.

Al termine della prima analisi l'INI originale era stato ripristinato e letto via FTP: SHA256
`7cb8317f6e1f77347d6cf3ba8bb3afb69eb251bffbc92678e6b00481109f8b50`.
Il candidato originale e il binario precedente restano conservati localmente
e nei backup FTP. La nuova build descritta sotto sostituisce il candidato del crash.

## Difetto riprodotto e patch

I registri XF dei canali colore e dei texgen possono aggiornare
`vertexProgramStateGeneration` senza cambiare `pipelineStateGeneration`.
Il ramo di aggiornamento parziale di `DrawSink::submit` rinfrescava il programma
dei vertici ma conservava `cpuRecipe_` e `gpuRecipe_`. Le informazioni derivate
su attributi, stride e semantiche da decodificare potevano quindi descrivere
il programma precedente.

La regressione alterna otto volte il materiale del canale 0 tra registro e
colore del vertice, mantenendo invariata la generazione della pipeline base.
Il programma richiede alternativamente uno stride di 16 e 20 byte. Prima della
patch: **30.891 check, 8 errori**, con layout e dimensione della geometria non
coerenti con i requisiti del programma. Dopo la patch: **30.891 check, 0 errori**.

La correzione ricostruisce la recipe CPU e invalida quella GPU quando cambia
la generazione del programma dei vertici. Il ramo esistente ricalcola quindi
layout e recipe GPU dopo avere applicato i flag di espansione della primitiva.
Le ottimizzazioni materiali, texture, uniform, cache geometrica e GX asincrono
restano attive. Il reset dello stato GXM per BeginScene è conservato.

Non si deduce da questa regressione un accesso oltre il buffer nel draw del
dump: i controlli di creazione delle pipeline possono respingere alcuni layout
obsoleti. La patch corregge un difetto verificato; il dump originale non contiene
gli indici, i buffer o l'identità del draw necessari per una prova causale completa.

## Nuova build e verifiche

| Elemento | Identità |
| --- | --- |
| Versione | `1.3.0-gpu-fix-20261005` |
| Base sorgente principale | `fcbc550ca7a4c3931c31078e53ddff301750360e` |
| Base Aurora | `39fb53e916f00036a20ddd734de960d6b40554fd`, più patch locale conservata |
| SELF installato e letto via FTP | `be6d1a0ebe17889181ff1de623c63e1864a81a9df3227be46d83f209ba9d643f` |
| ELF conservato | `c366c03e27937d1321c9322c59437cafa9051dd873931ba0d9cc18bec96b616d` |
| Map conservata | `961ff94e12571d752e9acf77ee35246c2a37cd10809aaa60b5ef20893a10b687` |
| VELF conservato | `33352b8cc59f8da236231421b900337edea482e9cf5f28323e7f4c2523547a90` |
| VPK | `b8310600014f59bc9ebcbd7b06e19f84258769ef5a91653b609a660b03679f80` |

Artefatti e manifest: `ab-artifacts/gpu-fix-20261005/fix-1/`. Il manifest
conserva patch, hash dei sorgenti, opzioni, SELF e simboli. L'installazione usa
upload temporaneo, confronto dei byte, rinomina e verifica della rilettura.

- Suite Aurora: **16/16 pass**.
- Regressione frontend sotto ASan/UBSan: **30.891 check, 0 errori**.
- Audit ELF/map: **12.686 simboli eseguibili**, GXM nativo, nessun GL/vgl/vita2d.
- Build Release completa e VPK generato; GX asincrono ON, geometria statica 8 MiB.

| Sessione hardware | SELF | Impostazioni | Cattura verificata |
| --- | --- | --- | --- |
| `gpu-reference-all-1` | `0563a5c5…` | mask `0x70008`, diagnostica ON, nessuno screenshot | 180 frame live-play, 0–179; log raggiunge present 8192, nessun nuovo dump osservato |
| `gpu-fix-diag-1` | `be6d1a0e…` | mask `0x8`, diagnostica ON, nessuno screenshot | 180 frame live-play, 0–179; campo, giocatori e HUD osservati via USB |
| `gpu-fix-snapshot-1` | `be6d1a0e…` | mask `0x8`, diagnostica OFF, screenshot play-frame 60, skip iniziale 600 | screenshot salvato e 1.200 frame live-play consecutivi, 600–1799; nessun nuovo dump osservato |
| `gpu-fix-quiet-1` | `be6d1a0e…` | mask `0x8`, diagnostica OFF, nessuno screenshot, frameskip esplicitamente OFF | 1.200 frame live-play consecutivi, 0–1199; nessun nuovo dump osservato |

Tutti i run usano seed `0x53545249`, `fixed_dt=16.666666667`, overlay OFF,
CPU effettiva 444 MHz e GPU 222 MHz. Il secondo run lungo senza screenshot
(`gpu-fix-quiet-1`) è completo. I replay e
l'introduzione non consumano campioni live-play. I CSV sono scritti una volta
sola al completamento: un file ancora assente non prova da solo un blocco GPU.

Il run con screenshot misura 23,923 frame/s nel tratto campionato, mediana
40,431 ms e p95 45,751 ms. Il run senza screenshot misura 24,932 frame/s,
mediana 39,852 ms e p95 44,733 ms. Non è un confronto A/B della patch e non dimostra
un raddoppio o fullspeed. Un test senza crash non prova l'assenza di ogni fault:
in caso di recidiva servono il nuovo dump e i simboli esatti di questa build,
insieme a diagnostica Release degli indici e dell'identità dei buffer.

Durante il controllo di riferimento FTP ha rifiutato i trasferimenti con
`550 Could not allocate memory`. Dopo riavvio e ritorno della console online,
i dati sono stati recuperati e il candidato installato. Questo errore del
servizio FTP non è stato classificato come un nuovo crash GPU del gioco.

## Impostazioni consegnate

Il nuovo SELF rimane installato. Dopo i test è stato chiuso il gioco e
ripristinato il contenuto dell'INI originale, aggiungendo soltanto due controlli
commentati e modificabili prima del blocco gestito:

```ini
vita_test_match = 0
vita_frameskip = 0
```

`vita_test_match=1` avvia la partita automatica CPU contro CPU; `0` conserva
il frontend normale. `vita_frameskip=1` abilita il catch-up già presente nel
gioco; `0` lo disattiva. Entrambi richiedono riavvio dell'applicazione. Il
frameskip è escluso da un `fixed_dt` diverso da zero; per il gioco normale
lasciare il timestep non impostato oppure usare `fixed_dt=0`.
La demo originale del menu dopo inattività non è modificata, come richiesto.
L'INI finale e gli hash riletti sono in `ab-artifacts/gpu-fix-20261005/final-device/`.
La rilettura finale conferma il SELF `be6d1a0e…` e l'INI SHA256
`14473a76e92bfc2f8af49be028b2c091d2cd889a6dae0146ba011c9d5051d93a`.
L'elenco finale dei dump in `ux0:/data/` è vuoto; il dump originale resta nella
copia locale preservata. Il gioco è stato chiuso al termine dei test.
