# Risultati dell'audit Aurora Vita

## Decisione tecnica

La prima ottimizzazione da perseguire è la riduzione delle invalidazioni e delle ricostruzioni dello stato di rendering. La seconda è ridurre la preparazione e il caricamento degli uniform dei personaggi, mantenendo la proprietà immutabile dei dati. In parallelo logico, ma con esperimenti separati, va classificato il costo degli shader per decidere quali specializzazioni TEV abbiano valore. Una riscrittura generale del renderer non è giustificata dai dati disponibili.

`Characters` e `Shadowed` sono bersagli plausibili e sostenuti dai profili storici. La frase «ultimo blocco per 60 FPS» rimane però un'ipotesi: l'ultimo controllo verificato è intorno a 40 ms/frame, e manca una cattura contemporanea per vista legata agli shader effettivamente eseguiti. I guadagni indicati nei ticket sono obiettivi di accettazione da misurare, non millisecondi già recuperabili con certezza.

Per consumi si intendono qui tempo CPU/GPU, copie di memoria, upload e chiamate di rendering. Non sono stati misurati watt o consumo della batteria.

## Perimetro e provenienza

| Elemento | Stato rilevato |
|---|---|
| Strikers | HEAD `89d020269a45bdbf1282e3fe6ec45604c1634e13`, working tree con modifiche staged e unstaged |
| Aurora incorporata | HEAD `a1d9b6624353686f9a507863f28dcc13f1d75308`, working tree modificata |
| Aurora standalone | HEAD `262e8e1e55ee238decd1d0220db7937c92526edf`, working tree pulita all'ispezione |
| Controlli quieti usati | SELF `4836a4096dcfcf0285bddff758393d0797847cfbadddaf5ade050cefa011ef48` |
| Baseline host dell'audit | Configure, build e CTest Aurora completati: 18/18, 4,34 s di CTest |
| Esecuzione hardware dell'audit | Nessun deploy, cambio INI o nuovo run |

Gli hash dei sorgenti e delle catture sono in [EVIDENCE.json](EVIDENCE.json). I cinque CSV recenti usati corrispondono ai rispettivi hash riportati nei file di analisi; i tre CSV quieti contengono ciascuno 1.200 righe. Le misure non si trasferiscono automaticamente alla working tree corrente né al prossimo SELF.

Draw sink, bridge GX, command processor, shader generator e fixed vertex helper risultano identici fra le due copie Aurora esaminate. `gxm_renderer.cpp` differisce, anche per il lavoro corrente sul debug degli shader. La standalone contiene inoltre una correzione del test dei materiali: usa l'ordine di valutazione di `lerp` per evitare dipendenza dal rounding host. Non significa che tutta la correttezza shader sia provata. L'integrazione va effettuata per patch e rivalidata dentro Strikers.

## Cosa significano le due viste

[gl.h](../../smstrikers-port/include/NL/gl/gl.h) identifica `GLV_Shadowed=3` e `GLV_Characters=11`. [glAppAttach.cpp](../../smstrikers-port/src/NL/gl/glAppAttach.cpp), [glPlat.cpp](../../smstrikers-port/src/NL/gl/glPlat.cpp) e [RenderShadow.cpp](../../smstrikers-port/src/Game/Render/RenderShadow.cpp) mostrano viste separate per `ShadowTexture`, `Shadow0/1`, `ShadowBlend0/1` e `WorldShadowed`.

Quindi `Shadowed` non è un sinonimo di generazione della shadow map. Misurare o disabilitare solo quella vista non isola l'intera catena delle ombre. Viceversa, cambiare la frequenza di aggiornamento di `RenderShadow` non è un'ottimizzazione trasparente di Aurora: modifica il contenuto temporale del gioco.

Il percorso rilevante è: vista e callback di Strikers → scritture GX/display list → decodifica FIFO → `DrawSink::submit` → traduzione stato e geometria → command stream → renderer GXM → programmi vertex/fragment. Le matrici skinned sono caricate da `glud_Skin` in `glxSend.cpp`; Aurora riceve matrici e comandi GX, non un concetto universale di “personaggio”. Le ottimizzazioni devono dipendere da stato e requisiti effettivi, non dal nome della vista o del modello.

## Evidenza prestazionale

### Controlli quieti

Fonte: `ab-artifacts/performance-plan-20261006/`, sottocartelle omonime. In questi run: 1.200 frame live, intervallo 600–1799, diagnostica e overlay OFF, frameskip 0, clock effettivi CPU/GPU 444/222 MHz, shader WARM. I manifest conservano i dettagli, compreso `gxm_disable=0x8`.

| Run | Media ms | Mediana ms | P95 ms | FPS sul tempo totale | Frame entro 16,667 ms |
|---|---:|---:|---:|---:|---:|
| token-control-quiet | 40,779 | 40,504 | 45,717 | 24,522 | 0% |
| token-control-quiet-repeat | 39,478 | 39,455 | 43,890 | 25,331 | 0% |
| token-native-quiet | 39,889 | 40,108 | 43,921 | 25,070 | 0% |

La variazione fra i due controlli è circa 1,30 ms sulla media. Piccoli vantaggi isolati di CDRAM, sidecar o compressione texture non sono una prova di miglioramento. Il controllo usa il bit `0x8`, che disabilita il riuso degli snapshot fixed: non usare `gxm_disable=0` nel candidato chiamandolo lo stesso controllo. Una promozione di quel riuso sarebbe un esperimento separato.

### Attribuzione recente del consumer

Il run `token-profile-native` ha 300 snapshot completati distinti e 299 intervalli, nessuno snapshot ripetuto o frame non campionato. È diagnostico e usa native assets. Il run scene appartiene allo stesso SELF ma a un'altra configurazione.

| Scope o contatore | Valore | Lettura corretta |
|---|---:|---|
| Lavoro GX / frame completato | 32,462 ms | Delta cumulativo / 299, non GPU pura |
| Draw frontend, mediana | 25,562 ms | Scope inclusivo, non sommare alle sottoparti |
| Traduzione stato, mediana | 7,235 ms | Bersaglio CPU concreto |
| Decode vertici, mediana | 2,985 ms | Residuo dinamico da classificare |
| Command build, mediana | 2,203 ms | Include costruzione dei pacchetti/snapshot |
| Submit, mediana | 4,565 ms | Tempo CPU di invio, non tempo shader |
| Cache geometria, mediana | 1,831 ms | Lookup/validazione; miss cumulativi nuovi zero |
| Resolve texture, mediana | 0,927 ms | Non coincide con upload |
| Resolve pipeline, mediana | 0,114 ms | La compilazione non domina il tratto caldo |
| Draw, mediana | 282 | Non è ancora la distribuzione delle due viste |
| Vertici totali / GPU, mediane | 72.313 / 70.285 | Rapporto delle mediane circa 97,2%, non media dei rapporti |
| Upload texture, mediana / P95 | 0 / 0 | Non c'è upload continuo dominante |

`token-profile-scenes` riporta 30,916 ms di attesa residua scene mediana e 32,539 ms di lavoro GX per frame completato. Il finish diagnostico serializza CPU e GPU: quei numeri non si sommano e non stimano il tempo GPU non perturbato.

Nel run native il delta `finish_calls` è zero e `efb_copy_us` mediano/P95 è zero. Questo riduce la priorità di una riscrittura EFB rispetto a stato/uniform/shader, ma non esclude un costo in altri stadi o transizioni, né prova che ogni costo GPU delle copie sia registrato da quel timer.

### Evidenza storica specifica per vista

Fonte: [analisi 2 ottobre](../../ab-artifacts/aurora-update-20261002/batching-off-20261002-085831/analysis.json), 25 finestre attive da 120 frame, 3.000 frame complessivi. SELF `f294e41140a336a4be3fcee4df6031cbe6a9e14d02fb4612fc65dea0e0b53922`, diverso da 4836.

| Vista | Wall medio nei frame senza timer packet | Scope draw nei frame campionati | Quota draw del callback campionato |
|---|---:|---:|---:|
| Characters | 15,193 ms | 13,380 ms | 84,7% |
| Shadowed | 18,498 ms | 14,164 ms | 75,3% |

Non sommare colonne provenienti da frame campionati e non campionati. Questi dati mostrano che il costo arrivava soprattutto ai callback draw, ma non separano shader GPU, frontend GX e attese. Sono una motivazione per A0, non la fotografia corrente né una promessa di recuperare 33 ms.

Il probe P6 più recente non ha prodotto una classifica completa utilizzabile. I vecchi `gxm_draw_gpu.log` locali appartengono ad altre sessioni e il probe spezza la scena e chiama `sceGxmFinish` dopo ogni draw campionato. Le sue latenze comprendono gli effetti della serializzazione; il primo draw può ereditare lavoro precedente. Non sono millisecondi per draw sommabili al frame normale.

## Risultati dell'ispezione dei sorgenti

I numeri di riga sono quelli dell'audit; usare anche i simboli, perché la working tree può avanzare. I percorsi Aurora nella tabella partono da `smstrikers-port/extern/aurora-vita/`.

| Punto del codice | Fatto verificato | Sviluppo proposto |
|---|---|---|
| `lib/gx/command_processor.cpp:286`, `mark_pipeline_state_dirty_at`, macro a circa 309; `lib/gx/gx.hpp:480` | `mark_dirty()` senza argomento aggiorna tutti i domini. Anche il ramo falso di `mark_pipeline_state_dirty_if` usa questa forma. | A2: distinguere scritture ripetute, pipeline, vertex, fragment, texture e raster, registro per registro. Non rendere il ramo falso un no-op globale: può esserci un cambiamento uniform valido. |
| `platforms/vita/gx/aurora_vita_draw_sink.cpp:528` e `:824` | Le revisioni comandano riuso texture e ricostruzione vertex/fragment; un'invalidazione troppo ampia ne riduce l'efficacia. | Misurare chi invalida e dimostrare equivalenza contro il percorso completo. |
| `platforms/vita/gx/aurora_gx_bridge.cpp:201` | La memoizzazione byte-exact di circa 2,5 KiB era stata rimossa perché più costosa della traduzione diretta, secondo il commento del codice. | Non reinserire una grande hash map per draw. Ridurre le ricostruzioni prima di cache aggiuntive. Il vecchio costo di ~21 µs resta storico, non rimisurato nell'audit. |
| Bridge `:542`; draw sink `:1074`; `gfx/vita_fixed_vertex.hpp`; `gfx/vita_fixed_uniform_pool.hpp` | Esistono già traduzione selettiva, builder incrementale e confronto esatto degli snapshot. Gli indexed-PN possono comunque copiare palette da 10 matrici. | A3a: preparazione incrementale delle sole parti consumate, mantenendo un oracle completo e dati pubblicati immutabili. |
| `gxm/gxm_renderer.cpp:1303–1479`, `bind_pipeline` | Se non può riusare lo stato vertex, carica palette position/normal fino a 120 float ciascuna più altri uniform. Il riuso e le capacità riflesse sono già presenti. | A3b: payload CPU preparato per programma e copia completa in una prenotazione fresca, dopo misura di chiamate/byte e hit potenziali. Nessuna scrittura parziale in un buffer appena riservato. |
| `gxm/gxm_shader_gen.cpp:64`, `:331`, `:346`, `:668` | Già presenti semplificazione AND/OR/XOR/XNOR delle costanti alpha, materiali aritmetici, costanti 0/1 e fallback TEV. | A4: propagazione conservativa di valori/intervalli alpha per provare ulteriori passaggi senza discard; non rimuovere alpha test in generale. |
| Shader generator `:582` | Emette uno stadio alla volta e i fetch dipendono dall'uso locale della texture; non c'è una passata esplicita di liveness all'indietro dell'intera catena TEV. | A5: eliminazione di lavoro morto e riuso di fetch equivalenti, prima analisi pura poi emissione. Il compilatore potrebbe già farlo: misurare il GXP e il device. |
| `gxm/gxm_renderer.cpp:1501` | Esiste già la variante senza fragment scissor quando copre l'intero target. | Verificare frequenza d'uso, non riproporre la sua implementazione. Lo scissor parziale resta esatto. |
| Renderer `:483`, `diag_draw_gpu` | Scrive `packet.pipelineKey`, ma il programma attivo può essere la variante scissor-free. Il dump rigenera Cg in base all'opzione globale, non attesta da solo il fallback realmente compilato. | A0: registrare chiave logica, variante attiva, hash reali vertex/fragment, native/reference effettivo, vista e sequenza consumer. |
| Renderer `:586` | Invalida lo stato a ogni `BeginScene`, a seguito di un crash hardware in suspend/resume. | Invariante obbligatorio di A3/A6/A7; non eliminare il reset per guadagnare setter. |
| `gfx/vita_draw_batch.hpp`; `tests/vita_submission_test.cpp` | Esiste batching locale con requisiti stretti di adiacenza, buffer, stato, matrici e indici. | Non proporre ordinamento globale o merge di personaggi con palette diverse. Valutare ulteriori merge solo con una distribuzione di ammissibilità favorevole. |

### Rischio GPU da verificare

La documentazione primaria Imagination descrive il costo potenziale degli alpha test con discard: la scrittura depth può attendere l'esecuzione del fragment shader, riducendo l'efficacia del rigetto anticipato. Questo motiva una verifica sui materiali dominanti; **non dimostra che sia la causa principale su Strikers/Vita**. [Imagination, Do Not Use Discard](https://docs.imgtec.com/starter-guides/powervr-architecture/html/topics/rules/do-not-use-discard.html).

In questo port sostituire alpha test con alpha blending oppure cambiare l'ordine dei draw non è semanticamente neutro. L'unico intervento ammesso in A4 rimuove una condizione la cui verità è dimostrata per tutti gli input ammessi. La comparazione alpha, le maschere di scrittura e il depth rimangono osservabili anche se il risultato finale sembra opaco.

Le API GXM per riservare uniform, associare programmi e aprire scene sono documentate da VitaSDK. Il piano mantiene separati payload CPU riutilizzabile e buffer GXM associato alla prenotazione corrente; la sola disponibilità delle API non prova la sicurezza di un riuso tra scene. [VitaSDK, SceGxm User](https://docs.vitasdk.org/group__SceGxmUser.html).

## Priorità e limiti

| Priorità | Sviluppo | Motivazione | Criterio per procedere |
|---|---|---|---|
| P0 | A0 attribuzione e A1 corpus/oracoli | Evitano ottimizzazioni al materiale sbagliato e falsi positivi nei test | Classifica collegata agli artifact, catture riproducibili |
| P1 CPU | A2 invalidazioni selettive | Esiste un costo stato mediano di 7,235 ms e un meccanismo di invalidazione ampia verificato | Meno rebuild/byte senza differenze semantiche; beneficio quieto ripetuto |
| P1 CPU | A3 uniform dei personaggi | Copie di palette e upload ripetuti sono visibili nel codice; costo specifico ancora da separare | Risparmio in build/upload superiore al costo della cache |
| P1 GPU condizionata | A4 alpha e A5 catena TEV | Possono incidere sul lavoro GPU, ma mancano pesi per shader aggiornati | Shader dominanti, oracle esatto per kill/alpha/depth e device A/B |
| P2 condizionata | A6 EFB e scene | Nel run native non emerge un costo EFB caldo dominante | Nuovi dati dimostrano split/finish ridondanti |
| Chiusura | A7 integrazione | Miglioramenti isolati possono interagire | Correttezza, stabilità e frame time della combinazione |

Non promettere 60 FPS sommando guadagni CPU e GPU. Con lavoro sovrapposto conta il percorso critico: serve una nuova cattura dopo ogni candidato. Prewarm shader, nuovi asset offline, cache geometria più grande e altri worker non hanno qui la stessa priorità. Circa il 97% del volume di vertici del campione è già sul percorso GPU; “portare tutto lo skinning sulla GPU” non identifica il residuo reale.

Disattivare viste o shader resta una prova diagnostica di sensibilità: elimina anche depth, occlusione, binding e contenuto, quindi non misura il guadagno di una sostituzione equivalente. Il registro shader presente nella working tree può aiutare l'indagine; i suoi draw soppressi non valgono come risultato ottimizzato.

Il report propone equivalenza rispetto al comportamento di controllo e ai contratti GX coperti. Il codice ha già fallback/approssimazioni dichiarate per alcuni casi GX: passare i test non certifica un'emulazione bit-exact universale dell'hardware originale. I nuovi interventi non devono aggiungere approssimazioni silenziose.
