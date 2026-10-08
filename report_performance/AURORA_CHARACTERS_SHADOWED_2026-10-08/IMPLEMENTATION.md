# Implementazione incrementale - 8 ottobre 2026

Questo documento integra, **senza sostituire**, `REPORT.md`, `TASKS.md` e
`TEST_PLAN.md`. La modifica nasce nell'Aurora incorporata in Strikers, non nel
checkout standalone. I sorgenti sono ancora dirty: non sono stati effettuati
commit, push, build Vita, installazioni o benchmark hardware in questa tranche.

## Stato dei ticket

| Ticket | Stato | Modifica verificabile | Gate mancante |
|---|---|---|---|
| A0a | Validato su hardware | `analyze_vita_view_draw.py`, `seal_vita_view_draw.py`; cattura 8 ottobre con 12 frame, 3590 record, 0 dropped, hash SELF/INI/GXP attestati | Comparazioni A/B deterministiche |
| A0b | Acquisito su PS Vita | Marker di vista FIFO, 3218 draw GXM attribuiti, 2683 nelle view SHADOWED/CHARACTERS; vedere `FIRST_HARDWARE_CAPTURE_2026-10-08.md` | Profilo GPU distinto dal tempo CPU |
| A1a | Prime fixture host | Decoder XF reale, 10 palette PN, signed zero/NaN, matrici normal/texture/post, reset, FIFO asincrono bloccato | Corpus autonomo di draw/texture/EFB dal device |
| A1b | Oracle alpha host ampliato | Predicati sempre veri/falsi, XOR e XNOR identici, 768 materiali TEV sintetici esistenti | Compilazione GXP, soglie FP32, alpha/depth su Vita |
| A1c | Confronto draw + firme payload opzionali | `compare_vita_view_draw.py` rifiuta eventi/view/draw differenti; v2 confronta anche byte dei vertici indicizzati, indici, uniform GPU e viewport/scissor/sampler con `--require-payloads` | Cattura GXM device, verifica contenuti texture ed equivalenza immagini/depth GPU |
| A2a | **Candidato OFF di default** | Soppressione esatta delle riscritture XF delle sole matrici posizione; 24 snapshot canonici identici nel test; contatori `xf_pos_*` | Replay device e tre A/B quiete senza regressione |
| A2b | Da misurare | Nessuna altra famiglia di invalidazione modificata | Top registri e oracle specifico |
| A3a | Misurazione aggiunta | `fixed_uniform_build_us`, `fixed_uniform_publish_us` | Distribuzione nella partita, poi eventuale patch isolata |
| A3b | Candidato hardware senza guadagno percepito | Buffer GXM fresco, restore di snapshot CPU completo, setter differenziale per gruppi identici. Rendering corretto con flag ON, nessun incremento FPS percepibile; mantenerlo OFF nei test A4 | A/B numerico resta disponibile se necessario |
| A4 | **Candidato Cg OFF di default** | Riuso dei fetch tex2D con stessa texture/TEV coord e rimozione di canali TEV non osservabili; flag `gxm_a4_fragment_opt=0/1/2/3`, fallback cache GXP originale; dettagli in `A4_FRAGMENT_TEV_OPT_2026-10-08.md` | Build Vita manuale, hash/GXP reali per i quattro shader dominanti, 3 A/B quiete, shader/alpha/depth/goal/replay invariati |
| A5 | Non iniziato | Nessuna eliminazione TEV/varying/fetch | Shader dominanti A0 e oracle A1c |
| A6 | Condizionato, non iniziato | Scene, clear, copy e depth invariati | Prova che EFB/split sono nel percorso critico |
| A7 | Test host parziali | Fallback per A2a, config di controllo intatta | Build manuale, A/A, A/B, stabilita, confronto quieto |

### Semantica dei marker di A0

`STRIKERS_VITA_VIEW_DRAW_CAPTURE=1` abilita un campione **diagnostico**.
Il producer emette un marker di vista nella stessa coda ordinata che contiene
i comandi GX. Il consumer **svuota i draw differiti del DrawSink prima di
registrare la view successiva**, perché la submission GXM potrebbe altrimenti
avvenire dopo il marker FIFO. Questo flush non attende il completamento GPU,
ma modifica i confini dei batch: la cattura A0 è soltanto diagnostica e non
deve mai essere usata come confronto quieto degli FPS. Il consumer registra
la vista effettivamente corrente al momento del draw. L'indice 3 indica
`Shadowed`, 11 `Characters`, 255 lavoro esterno
alla vista nominata. Il record contiene pipeline richiesta/attiva, hash del
codice sorgente Cg realmente compilato/cachato (non puntatori), stage TEV,
maschere, stato alpha/depth/blend/scissor, indici/vertici e bytes di uniform
caricati o copiati dalla cache, quando osservabili.

Il contatore `submit_cpu_us` campiona il renderer GXM attorno alla chiamata di
draw, **non** la GPU ne l'intera traduzione FIFO. Non usarlo come FPS o sommarlo
al tempo della view producer. Non ci sono finish diagnostici nel percorso A0.
Il formato raw attuale e **aurora-vita-view-draw-raw-v2**; il sealing produce
`aurora-vita-view-draw-v2`. La versione v1 precedente viene rifiutata perche
non contiene i nuovi campi con firme di payload.
L'output e limitato a 65536 record in RAM; qualsiasi overflow, marker perso,
draw soppresso o errore frame invalida la cattura. Il file raw non dichiara
falsamente SHA del SELF, INI o cache: il sealing usa i **bytes scaricati**.
Il parser richiede inoltre draw ordinali consecutivi per frame e un consumer
frame coerente, così una traccia incompleta non può essere classificata come
misura valida. Il test host copre l'ordine dei marker asincroni e il protocollo
di traccia, ma l'attribuzione completa con geometria GXM resta da verificare
sull'hardware.

### Attivazione temporanea su Vita

Nel `strikers.ini` del run diagnostico, fuori dal blocco dei default validati:

```ini
vita_view_draw_capture=1
vita_view_draw_skip=600
vita_view_draw_frames=12
vita_view_draw_capacity=8192
vita_view_draw_payloads=0
gxm_xf_equal_pos_writes=0
vita_frameskip=0
fixed_dt=0
```

Lo strumento produce `ux0:data/strikersVita/view-draw-raw.jsonl` dopo il warmup
e 12 frame di gameplay. Un raw senza footer o con `dropped>0` e inutilizzabile;
se 8192 record non bastano, aumentare il limite fino a 32768 per un'altra
cattura **diagnostica**, non cambiare il protocollo dei test quieti.

In una successiva acquisizione A1c, impostare
`vita_view_draw_payloads=1` in entrambe le configurazioni da confrontare.
Questo fa leggere al consumer GXM i byte dei vertici **effettivamente
indicizzati** e degli uniform al momento della submission: il costo su RAM
uncached puo essere elevato, percio il default resta **0**. Gli hash escludono
indirizzi dei buffer, revision ID e handle texture temporanei, ma includono
sampler, conversione EFB, UV, viewport e scissor. Una differenza prova una
variazione nel payload osservato; un'uguaglianza di hash FNV-1a non prova che
texture, pixel finali, depth e output GPU siano identici. Le catture con
indici fuori dai limiti o byte non accessibili vengono invalidate dal footer.

Dopo aver recuperato i file esatti usati nel run, su Mac:

```sh
python3 smstrikers-port/tools/seal_vita_view_draw.py \
  /percorso/view-draw-raw.jsonl \
  --self /percorso/strikers_vita.self \
  --ini /percorso/strikers.ini \
  --shader-cache /percorso/program_cache \
  --capture-id strikers-a0-01 \
  --out /percorso/view-draw-sealed.jsonl
python3 smstrikers-port/tools/analyze_vita_view_draw.py /percorso/view-draw-sealed.jsonl
```

Se sono disponibili due catture **della stessa scena e dello stesso intervallo
logico di frame**, il comparatore individua divergenze nell'ordine dei draw,
pipeline, shader, TEV, depth/blend/alpha e geometria visibile:

```sh
python3 smstrikers-port/tools/compare_vita_view_draw.py \
  /percorso/control-sealed.jsonl /percorso/candidate-sealed.jsonl \
  --allow-ini-change --require-payloads
```

L'opzione `--allow-ini-change` è consentita soltanto dopo aver confrontato
manualmente i due INI e verificato che differisca solo il flag A/B voluto;
il comparatore esige SELF e shader-cache identici. I tempi di submission,
gli upload uniform e gli ID assoluti dei frame sono intenzionalmente esclusi
dal confronto strutturale. Senza `--require-payloads` e un controllo solo di
topologia e stato. Con tale opzione include anche le firme dei payload inviati,
ma **non** dimostra identita delle texture campionate, profondita o pixel e non
sostituisce i gate visivi A1c/G3.

I percorsi sono segnaposto: vanno sostituiti con bytes/hashes attestati per
quella sessione, evitando di mescolare SELF, INI o cache di run diversi.
`--shader-cache` ammette una directory non vuota o un manifest/cache binario.

### Esperimento A2a isolato

`gxm_xf_equal_pos_writes=0` (default) mantiene il decoder originario.
`gxm_xf_equal_pos_writes=1` confronta i dodici word delle matrici posizione XF
contro la memoria dello stato GX, bit per bit. Soltanto se *tutti* sono identici
omette il `mark_dirty(Vertex)`. Cambiamenti minimi, NaN con payload diverso,
signed zero e reset continuano il percorso completo. Matrici normal, texture,
post e registri BP/XF non posizione non sono toccati.

Se la diagnostica standard e attiva, i tre contatori cumulativi
`xf_pos_inspected`, `xf_pos_unchanged`, `xf_pos_changed` vengono esportati nel
consumer CSV. **11/24 write evitati nel test sintetico non sono una stima del
risparmio Strikers.** Usare lo stesso SELF candidato con flag OFF/ON per l'A/B,
abilitando tutte le view/shader e disattivando la cattura A0.

## Gate di validazione

- Aurora preset `vita-host-tests`: includere `vita_view_draw_capture`,
  `vita_draw_payload_hash` e
  `vita_shader_debug_registry`, piu `vita_frontend_translation` e
  `vita_native_material` con i nuovi casi. Il preset testa il backend host,
  **non** il driver GXM su Vita.
- Python: `python3 smstrikers-port/tools/test_vita_view_draw.py`,
  `python3 smstrikers-port/tools/test_vita_performance_analysis.py` (i test
  includono anche la comparazione offline A/A-A/B).
- Config: il test host di `vita_default_config_test.c` deve passare con il
  numero aggiornato di chiavi e preservare gli override utente.
- Test successivi sulla console: G0-G5 del `TEST_PLAN.md`, 600 frame di warmup,
  1200 frame misurati, almeno 3 coppie di run OFF/ON a tutte le view attive,
  tre stadi, goal/replay e suspend/resume; niente frameskip e niente downgrade
  degli shader/materiali.

Non considerare una riduzione di `xf_pos_unchanged`, `fixed_uniform_*` o del
numero di chiamate prova di aumento FPS: per l'accettazione serve la riduzione
del frame time presentato, fuori dal rumore A/A e senza regressioni visive.

## Controlli finali di questa tranche

- `cmake --build --preset vita-host-tests` e `ctest --preset vita-host-tests`
  eseguiti in `smstrikers-port/extern/aurora-vita`: **21/21 PASS**, incluso il
  nuovo oracle host `vita_draw_payload_hash`.
- `test_vita_view_draw.py`: **12/12 PASS** (parser, sealing, comparatore,
  firme dei payload e rifiuto delle catture senza draw).
- `test_vita_performance_analysis.py`: **2/2 PASS**.
- `vita_default_config_test.c`: **PASS** (prima generazione, refresh dei
  default, override utente).
- `git diff --check` sulla root e sul submodule: **PASS**.
- Build Vita, test GXM, installazione, misura FPS e screenshot comparativi:
  **NON eseguiti**, come da policy del progetto.

Comando di build Vita **manuale**, soltanto dopo la revisione del diff:

```sh
cd /Users/robin994/Documents/Code/strikersVita/smstrikers-port
STRIKERS_GX_THREAD=ON ./build-vita-native-gxm.sh
```

Primo gate sull'hardware: build GXM di controllo, `vita_view_draw_capture=1`
per i 12 frame diagnostici e **tutti gli shader e le view abilitati**; scaricare
il raw con footer valido. Ripristinare `vita_view_draw_capture=0` per tutte le
misure quiete. Non attivare insieme l'esperimento A2a e l'acquisizione A0 per
interpretare il vantaggio FPS. Confrontare A/A prima di A/B e annotare SELF,
INI e cache shader della sessione: la documentazione sopra spiega il sealing.
