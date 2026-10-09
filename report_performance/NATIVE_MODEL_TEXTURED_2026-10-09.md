# Strikers Vita: ricette native per draw texturizzate

Data: 2026-10-09. Richiesta: proseguire l'estensione dopo il censimento che aveva osservato zero draw ammesse dal prototipo statico/unlit.

## Esito

Il percorso è ora attivo anche con display list nella RAM cached e la snapshot OFF/ON confrontata sul dispositivo è identica. **La variante peggiora le prestazioni nelle quattro prove quiet ABBA: OFF 20,391 FPS / 49,041 ms; ON 16,762 FPS / 59,660 ms.** Variazione ON: **−17,80% FPS, +21,65% tempo medio**. Non è promossa come ottimizzazione e il default resta disattivato.

SELF e INI iniziali sono stati ripristinati e verificati byte per byte dopo un riavvio controllato. Nessun nuovo coredump. Candidato, simboli, patch, configurazioni, catture e analisi sono conservati per riprendere il lavoro senza attribuire risultati a un binario diverso.

## Implementazione

`gxm_native_model_draw=2` seleziona una variante sperimentale collocata nei due punti in cui `glx_DrawPacket` invia effettivamente la display list. Tutto il codice GLX precedente e successivo rimane eseguito: callback, userdata, skin, luci, matrici, texture, viewport e scissor. La ricetta conserva soltanto la draw. Il default resta `0`; `1` conserva il prototipo statico/unlit precedente.

Sono ammesse al tentativo le viste Characters, Shadowed, UnsortedPerspective e FrontEnd e i programmi 3D unlit, unlit2x, pointlit, pointlit_dirt, crowd e crowd_lit. Il consumer verifica lo stato corrente, l'identità immutabile della display list, la geometria residente, il layout e la pipeline. Se una verifica fallisce esegue la draw GX originale nel medesimo punto della FIFO.

La ricetta del consumer ora può contenere texgen, texture e geometria GPU con normali/PN indicizzati e illuminazione. Ogni invio risolve i binding texture correnti attraverso lo stesso resolver del percorso ordinario, comprese revisioni sorgente, TLUT, EFB, sampler e invalidazioni. Verifica inoltre i flag texture incorporati nella pipeline. Matrici, luci, costanti fragment, viewport e scissor provengono dallo stato corrente; gli uniform sono pubblicati in snapshot immutabili. Non vengono conservati pointer agli uniform di un frame precedente.

Questa variante evita una parte della preparazione GX sul consumer quando la ricetta è riutilizzabile. Non elimina il lavoro GLX per istanza, non rimuove callback e non equivale a compilare un materiale completo fuori dal frame.

Cache geometrica **8 MiB**, arena sorgente **48 MiB RAM cached**. Nessun aumento a 16 MiB. Limiti delle ricette invariati: 64 stamp sul produttore, 64 entry sul consumer e 96 ricette vive. Copertura e possibile churn devono essere misurati sul dispositivo.

## Verifiche host e binario

- Suite Aurora host **27/27** superata.
- Nuovo test: equivalenza di draw texturizzate attraverso la FIFO, aggiornamenti di pose/scissor e costanti, revisione texture/sampler/TLUT, ricreazione EFB, fallback per raster e modifica degli array sorgente.
- Limite del test host: il backend host usa l'eligibilità vitaGL. Il caso GPU lit con PN indicizzati è verificato tramite confronto indipendente degli uniform con il traduttore GX completo; non è una prova di esecuzione del driver GXM o di pixel equivalenti su Vita.
- Test configurazione superato: default `0`, override `2` esportato correttamente.
- Analizzatore **4/4** superato.
- Build gioco Vita e probe GXM standalone riuscite.
- Audit del candidato finale: **12.830** simboli eseguibili, entry point draw/present GXM, nessuna entry point o libreria GL/vgl/vita2d. Audit standalone: **2.270** simboli.
- Eboot nel VPK byte-identico al SELF; `diff --check` root e Aurora superati.

## Identità e stato delle prove

Root HEAD `031923fbe23240eea4b698e5c9befc893e006372`; Aurora embedded HEAD `6acc6a889d0bec1fb206b1c73cb693a434c132df`. Il working tree conteneva modifiche precedenti: gli HEAD non bastano a identificare il candidato. Patch e sorgenti non tracciati sono conservati insieme al binario.

- SELF candidato finale con pin RAM: `173f4dfb2d8c1c56503aff9668f86aa2f46ac54480429fae94bfac338855e705`.
- VPK candidato finale: `03eacb5172d1c7406bd8c38344ad7f9b51624f59a4823438e1383addedca87cb`.
- SELF candidato iniziale, conservato per la diagnosi: `8749747bf40520d3b714bd8f7baa0a2b00f06e90c5970a8bdb766dd9ae531fdb`.
- VPK candidato iniziale: `d895b94e701afb3dd01cd86b9edbb57bad20d0fea4351e65da2dc5586fadef3d`.
- SELF originale: `0de405e4fa3891e04b298d6e8b8198ca8fda7e7ddb3b57d08444f635337e45f3`.
- INI originale: `efb59a686ba0c3e0ffca1fbbb952d053859361ef556e0d59872fe2ff7bb0a40c`.

Prima prova `nt-visual-on1`: acquisizione interrotta senza CSV completati. L'immagine USB mostrava la Home, ma riportava le 16:15 ed era stata acquisita alle 16:19, dopo il lancio alle 16:17. Non può stabilire da sola un'uscita del processo. Il successivo readback del log contiene `gxm_native_model_draw=2`, inizio partita e centinaia di present; nessun messaggio di uscita normale, nessun nuovo coredump. L'utente conferma assenza di interventi sulla console. Gli artefatti e la classificazione iniziale sono conservati; nessun risultato FPS è attribuito a questa prova.

SELF e INI originali sono stati ripristinati e verificati byte per byte. Il ripristino del SELF ha richiesto un riavvio: FTP rifiutava la scrittura nella cartella app anche dopo kill.

Controllo diagnostico `nt-control-fixed1` con SELF originale e gli stessi override: **120 frame live 60–179**, **120 snapshot consumer distinte** ai frame 1718–1837, snapshot gameplay live 120 completata. Nessun nuovo coredump. Il passo fisso `16.666666667` prolunga l'introduzione rispetto alle prove precedenti con tempo reale. Questo controllo serve alla diagnosi dell'avvio; non è il controllo FPS della nuova variante.

Le acquisizioni USB successive attendono 60 frame prima di conservare l'immagine. Il confronto della ricetta OFF/ON sul medesimo SELF candidato e i campioni quiet ripetuti sono completati nelle sezioni seguenti.

## Seconda acquisizione e blocco individuato

`nt-visual-on2`, SELF iniziale del candidato: 120 campioni live 60–179, 120 snapshot consumer ai frame 1718–1837, nessun nuovo coredump. La snapshot live 120 è byte-identica al controllo originale: SHA-256 PPM `d337d4c42df229505fc91f51733605fe512e65b5ed2e84001b5c4e05b8204219`. I quattro contatori delle ricette sono tutti zero anche agli endpoint cumulativi dall'avvio. Questa equivalenza non prova il nuovo percorso, perché nessuna ricetta viene accodata.

`nt-route-on1` aggiunge censimento e diagnostica 3D già disponibili, usa dt reale e completa 120 campioni. Il log mostra `route=display-list/compiled`; il censimento osserva 34.042 draw callback, tutte con trasporto disponibile, ma ancora zero ricette. Non c'è un problema generale di assenza del worker o di conversione delle draw a emissione immediata.

Nel codice `display_list_shadow_pin()` rifiuta tutti gli indirizzi fuori dalla CDRAM su Vita, mentre gli array e le display list osservati sono nell'arena RAM cached (`0x8f...`). `write_native_model_recipe()` richiede proprio quel pin; il suo fallimento impedisce la chiusura della registrazione, che riproduce la draw GX originale. Il miglioramento della memoria sorgente aveva quindi lasciato incompatibile questo requisito di trasporto.

La correzione ammette sorgenti RAM **solo quando una ricetta richiede il pin immutabile**. Le display list GX ordinarie in RAM conservano la politica precedente. Si usa la stessa cache shadow con limite 8 MiB; la cache geometrica GPU rimane 8 MiB. Il pin nativo verifica anche i byte correnti per rilevare modifiche senza notifica di scrittura, aggiorna l'identità e conserva immutabili i byte già accodati.

Prima di ogni slot nativo `GXFlush()` emette gli aggiornamenti VCD/VAT/BP pendenti. Avviene fuori dalla registrazione e anche prima del riuso della ricetta. Solo ricette senza comandi `before`/`after` vengono conservate dal produttore: lo stato della prima istanza non deve sovrascrivere quello di un'istanza successiva. Un tracciamento per vista e i contatori degli stadi sono attivi soltanto con il censimento; i motivi di rifiuto del censimento restano riferiti alla politica del prototipo mode 1, mentre gli stadi possono includere il mode 2.

I test includono ora limiti CDRAM/RAM, pin con modifica senza notifica, identità aggiornata e conservazione dei byte immutabili; il test delle draw texturizzate usa il vero `GXCallDisplayList` con dirty state emesso fuori dalla ricetta. Suite host finale **27/27**. La prima ricompilazione diagnostica era partita dalla build sincrona `build-vita`: è stata fermata e non è stata installata. Il candidato corretto proviene da `build-vita-gxm-latest`, con i flag asincroni originali.

## Diagnostica hardware del candidato finale

`nt-pin-diag1`: SELF finale verificato tramite readback; `gxm_native_model_draw=2`, diagnostica e censimento attivi, dt reale, cache geometrica 8 MiB, arena sorgente 48 MiB RAM cached. Gli altri esperimenti (replay, vertex reuse probe, streamed vertex GPU) sono disattivati. Demo e seed sono controllati dall'INI temporaneo.

La cattura completa 120 campioni live 60–179 e 120 snapshot consumer distinte ai frame 391–678. Nell'intervallo consumer ci sono 168 frame non campionati; i seguenti conteggi sono differenze tra endpoint cumulativi, non la somma delle sole righe osservate:

| Contatore | Delta |
|---|---:|
| Tentativi consumer della ricetta | 32.988 |
| Draw eseguite dalla ricetta nativa | 2.543 |
| Fallback GX | 30.445 |
| Nuove ricette compilate dal produttore | 30.414 |
| Begin attempt / rejected | 59.394 / 28.980 |
| Capture sealed / aborted | 30.414 / 0 |
| Riuso già noto accodato / rifiutato | 2.574 / 0 |

Le draw native sono il **7,71% dei tentativi consumer**. Il blocco del pin RAM è quindi risolto. La vicinanza tra 30.414 compilazioni e 30.445 fallback, insieme ai limiti 64/64/96 e ai rifiuti `recipe_limit`, indica pressione e ricreazione frequente delle ricette. È un'attribuzione supportata dai contatori e dal codice, non un conteggio completo delle cause di ciascun fallback. Le osservazioni del produttore lette dal consumer possono includere lavoro futuro e non sono un denominatore esatto per la copertura delle draw consumer.

Nessun nuovo coredump. La cattura USB con riscaldamento di 60 frame mostra gameplay, personaggi e HUD. Il log campionato riporta CDRAM allocata 73.400.320 byte, CDRAM libera 39.845.888 byte, user RAM libera 29.360.128 byte e nessun fallback di allocazione. Sono osservazioni della prova, non una garanzia sul massimo impiego di memoria.

Il censimento dei motivi di rifiuto continua a descrivere la vecchia politica static/unlit mode 1: i suoi contatori `eligible=0` non negano le 2.543 draw mode 2. Gli stadi mode 2 e i contatori consumer sono riportati separatamente.

## Equivalenza delle immagini sul dispositivo

`nt-pin-visual-off1` e `nt-pin-visual-on1` usano il medesimo SELF finale, seed e configurazione; cambia solo `gxm_native_model_draw` (0/2) e il nome dell'acquisizione. Per la ripetibilità delle immagini il passo è fisso a 50 ms. Ogni prova completa 120 frame live 60–179 e 120 snapshot consumer consecutive 618–737. Snapshot framebuffer al frame live 120, 960×544: **identica byte per byte**, zero pixel differenti, SHA-256 PPM `c2da0760011373d66a1f1a2cf0e7e3639cf1c7a16ec22c1b12521153f3a780b4`.

OFF ha zero tentativi nativi; ON ha 13.450 tentativi, 1.215 draw native, 12.235 fallback e 12.234 compilazioni nel delta consumer. Il percorso è quindi effettivamente attivo nella prova ON. Nessun nuovo coredump in entrambe le prove. L'immagine è stata anche ispezionata: campo, personaggi e HUD presenti. Questa prova valida il singolo frame confrontato; non stabilisce equivalenza di tutte le scene, input, audio o FPS in tempo reale.

## Prestazioni hardware senza diagnostica

Quattro avvii OFF–ON–ON–OFF sullo stesso SELF finale. Ogni prova cattura 300 frame live consecutivi 600–899, dopo 600 frame di riscaldamento gameplay. Gli override sono identici salvo mode 0/2 e nome CSV. `diagnostics=0`, `fps_overlay=0`, censimento/profili/snapshot/replay/probe/streamed GPU disattivati, `fixed_dt=0`, seed `0x53545249`, frameskip 0. Cache geometrica 8 MiB, arena sorgente 48 MiB RAM cached, maschera GXM `0x8`, shader profile WARM.

I quattro CSV confermano i clock effettivi **444 MHz CPU, 222 MHz GPU/bus, 166 MHz xbar**. Il log diagnostico registra la richiesta CPU 500 MHz rifiutata (`rc=-2144665600`) e fallback 444 riuscito. Il confronto non va etichettato come una misura a 500 MHz.

| Run | Modalità | Campioni | FPS su tempo totale | Media ms | Mediana ms | P95 ms |
|---|---|---:|---:|---:|---:|---:|
| nt-pin-q-off1 | OFF | 300 | 19,719 | 50,711 | 48,178 | 66,471 |
| nt-pin-q-on1 | ON | 300 | 17,126 | 58,390 | 58,032 | 70,060 |
| nt-pin-q-on2 | ON | 300 | 16,412 | 60,929 | 58,280 | 78,223 |
| nt-pin-q-off2 | OFF | 300 | 21,110 | 47,371 | 45,976 | 58,658 |
| OFF aggregato | OFF | 600 | **20,391** | **49,041** | 46,852 | 64,287 |
| ON aggregato | ON | 600 | **16,762** | **59,660** | 58,191 | 75,066 |

FPS aggregati = numero di campioni diviso somma dei tempi dei frame, non media degli FPS istantanei. Il rilevatore include l'attesa di acquisizione framebuffer; scrive il CSV una sola volta alla fine, con quell'I/O escluso dal campione. Nessun campione delle quattro prove rientra nel budget 16,667 ms per 60 FPS. I tempi dei frame includono le attese e non equivalgono a una misura isolata della CPU o della GPU.

Lo span tra le due medie OFF è 6,81%, quello ON 4,26%; entrambe le prove ON sono più lente di entrambe le OFF. L'effetto osservato supera questi span. Sono comunque due avvii per modalità: dt reale e seed comune non rendono identiche pose, eventi e carico. ABBA limita solo in parte la deriva d'ordine; non è una garanzia su tutte le partite. I contatori diagnostici provengono da acquisizioni separate e non si sommano ai tempi quiet.

## Decisione e prossimo esperimento

Tenere `gxm_native_model_draw=0`. La correzione del pin rende possibile la ricetta, ma il vantaggio non compensa il costo della variante corrente. Il risultato non dimostra una riconversione integrale degli asset: il nuovo costo comprende creazione/ricerca/validazione/trasporto delle ricette e aggiornamento dello stato per istanza. Il rapporto di riuso basso orienta la prossima indagine; non quantifica da solo il costo CPU di ciascuna causa.

La prossima modifica deve riguardare **la cache delle ricette e dei metadati in RAM**, mantenendo il budget geometrico GPU a 8 MiB:

1. Contare separatamente miss del produttore, nuove identità, sostituzioni, `recipe_limit`, svuotamenti consumer, invalidazioni pin e guardie consumer. Registrare massimo di ricette vive, byte RAM e occupazione della coda. Evitare logging per draw e lasciare il censimento fuori dalle prove quiet.
2. Sostituire lo svuotamento totale della mappa consumer a 64 entry con sostituzione selettiva. Valutare una capacità del produttore coerente con il numero di ricette effettivamente ricorrenti e con quelle in volo; non aumentare soltanto un limite lasciando gli altri a 64/96. L'array dei 64 stamp nel binario ARM occupa 12.288 byte: il conteggio dei metadati è distinto dagli 8 MiB di geometria, ma le altre strutture e i pin vanno contabilizzati separatamente.
3. Conservare identità e revisioni valide, verifica esatta delle display list modificate, stato GX corrente, snapshot immutabili, FIFO/EFB e fallback nel medesimo slot. Le entry consumer referenziano geometria della cache: ammettere la ricetta solo finché quella entry è residente e validata, senza aggirare budget o retirement.
4. Ripetere test host, confronto immagini su più transizioni e ABBA quiet. Accettare l'esperimento solo con vantaggio oltre la variabilità e senza regressioni osservate; il target 60 FPS richiede ridurre gli attuali 49,041 ms OFF a 16,667 ms, circa **66% di tempo in meno**. Il beneficio di questa futura modifica non è ancora misurato.

La prova storica a 16 MiB aveva bloccato la console e la causa resta non attribuita. Queste acquisizioni a 8 MiB completate, senza nuovi dump, non dimostrano che 16 MiB siano sicuri o utili.

## Ripristino finale

Dopo l'ultima cattura l'app è stata terminata e l'INI iniziale ripristinato. Un riavvio controllato ha reso di nuovo scrivibile la cartella app; il SELF originale è stato promosso con staging e readback. Il controllo finale verifica SELF `0de405e4fa3891e04b298d6e8b8198ca8fda7e7ddb3b57d08444f635337e45f3` e INI `efb59a686ba0c3e0ffca1fbbb952d053859361ef556e0d59872fe2ff7bb0a40c`, entrambi byte-identici ai backup. I nove coredump presenti sono gli stessi iniziali. Il gioco non è stato riaperto dopo il ripristino.

## Evidenze

- [Manifest candidato finale](../ab-artifacts/native-textured-20261009/candidate-ram-pin/manifest.json): ELF, map, VELF, SELF, VPK, build cache, patch e nuovi sorgenti.
- [Audit artefatti finali](../ab-artifacts/native-textured-20261009/final-artifact-audit.json): hash e identità eboot/VPK; le patch correnti coincidono con quelle archiviate.
- [Risultato consolidato](../ab-artifacts/native-textured-20261009/experiment-result.json).
- [ABBA quiet](../ab-artifacts/native-textured-20261009/quiet-abba-summary.json) e [analisi riproducibile](../ab-artifacts/native-textured-20261009/analyze-phase.py).
- [Contatori del percorso nativo](../ab-artifacts/native-textured-20261009/nt-pin-diag1/consumer-summary.json).
- [Confronto immagini](../ab-artifacts/native-textured-20261009/visual-comparison.json), [snapshot ON](../ab-artifacts/native-textured-20261009/nt-pin-visual-on1/debug_frame_play_120.png).
- [Identità finale sul dispositivo](../ab-artifacts/native-textured-20261009/final-device-identity.json), [ripristino](../ab-artifacts/native-textured-20261009/final-restore/restore-identity.json).
- [Manifest candidato iniziale](../ab-artifacts/native-textured-20261009/candidate/manifest.json).
- [Prima acquisizione incompleta](../ab-artifacts/native-textured-20261009/nt-visual-on1/failed-run-identity.json).
- [Ripristino verificato dopo riavvio](../ab-artifacts/native-textured-20261009/restore-after-reboot/restore-identity.json).
- [Controllo di avvio](../ab-artifacts/native-textured-20261009/nt-control-fixed1/capture-identity.json).
- [Snapshot controllo](../ab-artifacts/native-textured-20261009/nt-control-fixed1/debug_frame_play_120.png).
- [Test host finali](../ab-artifacts/native-textured-20261009/ram-pin-exact-host-tests.log), [audit gioco finale](../ab-artifacts/native-textured-20261009/ram-pin-game-audit.log), [audit standalone](../ab-artifacts/native-textured-20261009/ram-pin-standalone-audit.log).

Nessun commit o push eseguito. Variante conservata per diagnosi, disattivata per default e non promossa: regressione FPS misurata, 60 FPS non raggiunti.
