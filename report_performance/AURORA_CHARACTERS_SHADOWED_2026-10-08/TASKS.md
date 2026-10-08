# Incarichi di sviluppo

Tutti gli incarichi sono **da implementare**, salvo la baseline host descritta nel report. Nessun nome di nuova opzione o nuovo test qui proposto va scambiato per una funzionalità già disponibile.

## Contratto da includere in ogni incarico

1. Leggi `REPORT.md`, `TEST_PLAN.md` e l'`AGENTS.md` del checkout. Prima di editare verifica HEAD, diff staged/unstaged e file non tracciati. Mantieni le modifiche preesistenti.
2. Implementa un solo sottoincarico. Non cambiare simulazione, callback, frequenza delle animazioni/ombre, risoluzione, draw order, formato degli asset o topologia dei thread per ottenere un risultato migliore.
3. Parti dall'Aurora incorporata; concorda tecnicamente l'integrazione delle patch equivalenti nella standalone. Non aggiornare il submodule come scorciatoia e non importare file interi che cancellino lavoro locale.
4. Ogni candidato ha selezione OFF/ON, controllo conservato e contatore che dimostri l'attivazione. Preferisci un flag dedicato di default OFF; assegna il bit solo dopo aver verificato quelli disponibili. Il bit generale di fallback esistente non sostituisce il controllo della sola nuova modifica.
5. Mantieni FIFO, EFB, scissor esatto, `BeginScene` reset, lifetime fino a execute e invalidazioni di guest memory/TLUT/mode switch. Non introdurre accessi a memoria guest dal thread sbagliato.
6. Scrivi prima il test che esprime la proprietà indipendente, poi l'ottimizzazione. Un test che ricalcola il risultato usando gli stessi helper del candidato non è un oracle sufficiente.
7. Consegna diff, comandi/esiti, fixture/seed, artifact identity, fallback, limite noto e ticket successivo. Distingui `host superato`, `hardware da verificare`, `hardware accettato`. Non dichiarare 60 FPS senza il gate A7.

Template di prompt da copiare insieme al singolo ticket:

> Implementa esclusivamente il sottoincarico [ID] di TASKS.md. Rispetta il contratto comune e i test indicati in TEST_PLAN.md. Consegna una modifica circoscritta e reversibile, con test contro l'oracle di riferimento e stato di validazione esplicito. Non estendere lo scope, non promuovere default e non modificare il gioco per ridurne il contenuto. Se l'ipotesi viene smentita, consegna il test o la misura che la smentisce e lascia il percorso di produzione invariato. Se manca la Vita, completa la parte host e identifica esattamente il gate hardware ancora aperto.

## Sequenza e dimensione degli incarichi

| ID | Sottoincarichi da assegnare separatamente | Dipendenze | Complessità |
|---|---|---|---|
| A0 | A0a schema/parser; A0b tag consumer e raccolta | Nessuna | Bassa per parser; media per FIFO |
| A1 | A1a corpus state/vertex; A1b corpus shader; A1c replay hardware | A0 per fixture reali; unità sintetiche subito | Media; review del replay |
| A2 | A2a scritture equivalenti; A2b domini di una famiglia BP/XF | A0, A1a | Media se una famiglia per patch |
| A3 | A3a builder/snapshot; A3b payload vertex preparato | A0, A1a; rivalutare dopo A2 | Media/alta; due patch distinte |
| A4 | A4a prova alpha costante; A4b intervalli conservativi | A0, A1b, A1c | Media; partire dalle sole costanti |
| A5 | A5a liveness pura; A5b emissione; A5c fetch condivisi | A0, A1b, A1c | Media per analisi, alta per integrazione |
| A6 | A6a inventario scene; A6b un solo split evitabile | A0 dimostra il costo | Alta, condizionata |
| A7 | A7a artifact e prove; A7b combinazione e integrazione | Candidati singoli accettati | Review di integrazione |

Gli incarichi indicano dipendenze, non richiedono agenti paralleli né file condivisi editati contemporaneamente. Per limitare il costo dei modelli, congelare l'interfaccia e le fixture prima di assegnare il successivo sottoincarico. A3b, A5b e A6b non vanno affidati come refactor autonomi senza revisione.

## A0 Attribuzione affidabile per vista e shader

**Problema.** I timer della vista lato producer non identificano da soli il costo del consumer asincrono. `diag_draw_gpu` registra la chiave del packet, ma il programma attivo può essere una variante diversa. Non esiste una classifica corrente completa legata a `Characters`/`Shadowed`.

**File da leggere/modificare.** Strikers `src/NL/gl/glPlat.cpp`, `include/port/vita_performance_capture.hpp`, `tools/analyze_vita_performance.py`; Aurora `lib/gx/fifo.*`, `platforms/vita/gx/aurora_vita_draw_sink.*`, `platforms/vita/integration/vita_gx_capture.*`, `platforms/vita/gxm/gxm_renderer.cpp`, `platforms/vita/gfx/vita_telemetry.*`. Il registro shader corrente è utile solo come catalogo; non sostituisce l'attribuzione temporale.

**A0a.** Definire un record versionato e un parser offline. Campi minimi: build/INI/cache identity, frame producer e frame completato, sequenza, vista numerica, draw logico, target, pipeline richiesta e attiva, hash Cg vertex/fragment realmente compilati, native/reference effettivo, indexed-PN, stage TEV, blend/depth/alpha/scissor, vertici/indici, mask texture/texgen, upload uniform e relative cause, cache hit/miss, finish reason. Non usare puntatori come identità persistenti. Per GXP, registrare byte e metadati disponibili senza chiamarli “numero istruzioni” o “pressione registri” se non misurati.

**A0b.** Trasportare i confini di vista nell'ordine FIFO o in metadati immutabili associati ai job; leggerli nel consumer che esegue i draw. Non leggere una variabile globale “vista corrente” aggiornata dal producer. Il marker non deve introdurre flush/finish. Separare catalogo passivo/counter, timer diagnostico CPU e probe serializzato GPU. Bufferizzare in memoria ed esportare fuori dal tratto misurato, con tetto fisso e contatore di record persi. Riutilizzare le strutture di cattura esistenti quando adeguate.

**Test obbligatori.** Due viste accodate mentre il consumer è bloccato; una vista vuota; cambio frame con lavoro in volo; clear/copy fra viste; fallback CPU; variante scissor; fallimento native seguito da reference; marker persi, CSV troncato, counter reset e revisioni ripetute. Il parser deve rifiutare statistiche “complete” se perde record. Con diagnostica OFF, sequenza dei comandi, contenuto dei draw e immagini devono essere invariati; nessuna scrittura di log per draw.

**Accettazione.** Report separati per `Characters`, `Shadowed`, altre viste e totale; distribuzione dei costi e top shader per frequenza/volume e latenza diagnostica. Se le medie per vista non riconciliano con il totale dello stesso scope/frame, spiegare i draw fuori vista e i record mancanti. Non sommare latenze di finish per predire gli FPS. Misurare l'overhead della raccolta rispetto al controllo; non promuovere la raccolta a quieta solo perché usa pochi timer.

## A1 Corpus e oracoli prima delle ottimizzazioni

**Problema.** I test esistenti coprono parti del contratto, non una scena Vita completa. Il test nativo TEV valuta espressioni scalari host; non esegue il compilatore e il rasterizer GXM, né verifica integralmente alpha/kill/fog/sampler.

**File.** Aurora `tests/vita_frontend_translation_test.cpp`, `tests/vita_geometry_recipe_test.cpp`, `tests/vita_fixed_uniform_builder_test.cpp`, `tests/vita_native_material_test.cpp`, `tests/vita_submission_test.cpp`, `tests/vita_backend_contract_test.cpp`, `platforms/vita/integration/vita_gx_capture.*`, `vita_gx_replay.*`, `cmake/aurora_vita.cmake`. Nuove fixture nella cartella tests con formato documentato e versione.

**A1a.** Catturare sequenze piccole di scritture GX, primitive, layout, matrici e invalidazioni; produrre un risultato canonico del percorso completo: descrittore semantico, vertici/indici, uniform consumati, binding e ordine. Canonicalizzare padding, handle e revisioni senza ignorare campi osservabili. Per dati guest, copiare bytes e timeline delle modifiche: i soli puntatori o `PipelineDesc` non consentono replay.

**A1b.** Bloccare un insieme di materiali reali delle due viste e un insieme sintetico avverso. Memorizzare tutti gli input di shader, sampler e render state. Estendere il reference con alpha compare e output finale; documentare espressamente quanto resta non coperto dal reference scalar. Congelare le fixture o usare un PRNG e una conversione numerica definiti: `uniform_real_distribution` non garantisce gli stessi campioni fra librerie standard. La correzione standalone `262e8e1` va esaminata, senza ridurre la tolleranza per nascondere un errore.

**A1c.** Validare/adattare il replay esistente affinché ricrei dati guest e risorse mutabili, matrici, cache iniziale e barriere in modo autosufficiente; l'esistenza del reader non prova un replay autonomo. Ottenere render deterministici A/A dello stesso stato, poi A/B. Per regressioni del renderer, immagini “allo stesso minuto della demo” non sono fixture sufficienti. Conservare privatamente i dati di gioco e usare fixture sintetiche redistribuibili nella suite pubblica.

**Test obbligatori.** Replay due volte con risultati uguali; truncate/CRC errato; invalidazione tra due draw; alias di texture/copia EFB; reset dello stato e riuso di indirizzo; mismatch di versione rifiutato; coda ritardata con sorgente mutata dopo enqueue. Fare mutation testing locale: disattivare intenzionalmente un'invalidazione, un reset o una riga di palette deve far fallire il relativo test.

**Accettazione.** Ogni fixture specifica vista, motivo della selezione e proprietà verificata. Nessuna dipendenza dalla posizione di allocazione o dallo stato di un run precedente. A/A hardware deterministico prima di usare una differenza immagine per promuovere o bocciare A/B.

## A2 Invalidazioni GX più precise

**Ipotesi.** Scritture che cambiano un solo dominio, oppure lasciano invariato lo stato consumato, innescano ricostruzioni di vertex/fragment/texture non necessarie. Il costo stato è misurato; la quota attribuibile a queste scritture va misurata da A0.

**File.** Aurora `lib/gx/command_processor.cpp`, `lib/gx/state_revisions.hpp`, `lib/gx/gx.hpp`, `platforms/vita/gx/aurora_vita_draw_sink.cpp`; test di frontend. Evitare modifiche al generatore shader nello stesso ticket.

**A2a.** Misurare scritture ricevute, già soppresse dal cache BP, semanticamente uguali e realmente differenti per registro. Scegliere una famiglia dominante. Conservare il parsing e gli effetti laterali; saltare l'invalidazione solo quando è provata l'identità di tutti i valori e gli effetti osservabili. Non modificare globalmente `mark_pipeline_state_dirty_if(false)`.

**A2b.** Per una famiglia a volta costruire la tabella `registro/campo → pipeline, layout, vertex, fragment, textures, raster, clear`. Le scritture TEV inattive possono diventare visibili quando cresce `stageCount`; le matrici possono influenzare sia posizione sia texgen. Una scrittura di sampler può influenzare texture binding, wrap nativo e la variante shader. Le scritture di stato e le operazioni con effetti laterali non vanno confuse.

**Test obbligatori.** Emettere i comandi GX reali e decodificarli nei test: i test che chiamano direttamente `mark_dirty(StateDomain::Fragment)` non verificano la classificazione BP/XF. Per ogni caso confrontare candidato e rebuild completo dopo **ogni draw**: scrittura uguale, singolo bit/campo cambiato, campo inattivo poi attivato, write mask, texture/TLUT, reset di `GXState`, cambio formato/stride, alpha/fog/depth, passaggio fixed GPU→CPU→GPU. Includere un comando sconosciuto/non classificato che mantenga il fallback completo.

**Accettazione.** Identità degli output canonici; contatori mostrano meno rebuild nel dominio atteso senza perdere aggiornamenti. Ripetere quiet A/B secondo il gate comune; se il costo della comparazione assorbe il risparmio, mantenere il test e scartare il fast path. Un minor numero di revisioni non è da solo un miglioramento prestazionale.

**Fallback.** Flag specifico del candidato OFF; `GxmDisableStateDomains` resta un riferimento più ampio per diagnosi, non il controllo principale della singola patch.

## A3 Ridurre preparazione e upload degli uniform vertex

**Ipotesi.** Tra personaggi e materiali si ricopiano palette/illuminazione e si ripetono upload nonostante cambi solo una parte degli input. È già presente un builder incrementale: non ripartire da una seconda implementazione equivalente senza misurare cosa manca.

**File.** Aurora `platforms/vita/gx/aurora_gx_bridge.cpp`, `aurora_vita_draw_sink.cpp`, `platforms/vita/gfx/vita_fixed_vertex.hpp`, `vita_fixed_uniform_pool.hpp`, `platforms/vita/gxm/gxm_renderer.cpp`; test fixed builder, frontend, submission e nuovo test di payload se necessario.

**A3a, solo CPU.** Separare prima i costi `translate_fixed_vertex_state`, builder, confronto snapshot e pubblicazione. Il percorso indexed-PN usa palette da 10 slot: analizzare quali righe vengono realmente riscritte e consumate. Implementare aggiornamenti per parte in uno scratch privato usando revisioni affidabili o confronto esatto, includendo identità dello stato e variazioni dei requisiti della pipeline. Pubblicare sempre uno snapshot immutabile completo o referenziare uno già equivalente. Non usare il solo pipeline key/versione come prova: il sorgente documenta precedenti errori di riuso.

**A3b, payload GXM.** Solo se A0/A3a mostrano che upload/prepare dominano ancora. Prototipare una preparazione vertex per programma riflesso analoga, nel contratto di lifetime, a `FragmentPrepareCache`: layout/capacità del programma sono parte dell'identità; dati CPU posseduti con budget; buffer GXM appena riservato riempito integralmente. L'aggiornamento parziale avviene sul payload CPU completo, mai assumendo che una nuova prenotazione contenga uniform precedenti. Per un programma sconosciuto o una preparazione fallita tornare agli upload di riferimento.

Non rilassare insieme il criterio di riuso basato sul pipeline key e l'ownership dei programmi. Se si vuole studiare il riuso fra pipeline con lo stesso vertex program, farne un ticket successivo: bisogna provare identità del programma patchato, layout, dati e generazione della scena, non solo del testo Cg.

**Test obbligatori.** Tutti i 10 slot PN, normali inverse transpose, matrici non uniformi, texgen/postmatrix, due palette differenti sullo stesso mesh, indexed/current-PN, lit/unlit, luci sparse, bump, riuso di indirizzo, cambio shader con medesimi valori, fallback CPU. Per A3a confronto col builder completo e verifica che modificare lo scratch non alteri packet accodati. Per A3b recorder API: reserve→riempimento completo→draw; cambio programma, End/BeginScene, clear/copy, pool pieno, fallimento reserve/upload, distruzione ed eviction. Canary sulle estremità dei payload.

**Accettazione.** Bytes semantici equivalenti; risparmio totale di prepare/upload documentato includendo hash/confronti/memcpy; budget cache dichiarato e nessuna crescita dopo warmup. Immagini di pose e ombre coincidenti, almeno 10 cicli suspend/resume, quiet A/B ripetuto. Un hit-rate elevato senza riduzione del tempo non basta.

**Fallback.** A3a e A3b devono potersi spegnere indipendentemente. Non rimuovere le prenotazioni richieste e non mantenere stato GXM valido attraverso `BeginScene`.

## A4 Rimuovere soltanto alpha test dimostrati superflui

**Ipotesi.** Parte dei fragment dominanti potrebbe contenere discard che il generatore può evitare dimostrando l'alpha finale. Il generatore già elimina combinazioni di compare costanti; il nuovo lavoro riguarda l'alpha prodotto dai TEV.

**File.** Aurora `platforms/vita/gxm/gxm_shader_gen.cpp/.hpp`, `tests/vita_native_material_test.cpp`, `tests/vita_backend_contract_test.cpp`; shader cache solo se necessario per distinguere gli artifact del candidato.

**A4a.** Aggiungere un'analisi conservativa delle sole costanti alpha, separata dall'emissione. Primo caso: valore alpha finale letterale provato dopo bias, scale, clamp/wrap e registri. Valutare i due compare e l'operatore usando la semantica esistente. “Opaco nel texture viewer” o “ref=0” non costituiscono una prova.

**A4b.** Soltanto dopo A4a, estendere a intervalli con stato `unknown`. In presenza di registri signed, wrap, compare TEV, indirect o alias RGB/alpha non coperti, conservare il generatore attuale. La sovrascrittura destination alpha avviene dopo il test: non usarla per dedurre un pass prima del test. Se il risultato è sempre falso, preservare `discardAll` e la gestione senza color/depth write; non aggiungere un unconditional kill che il compilatore può rifiutare.

**Test obbligatori.** Tutti gli 8 compare, 4 operatori, tutti i ref 0–255 per ciascun compare; coppie di ref complete sulle costanti e casi avversi per quelle dinamiche. Alpha 0, 1, signed e fuori range prima del wrap; ogni soglia `k±0,5` nello spazio 0–255 con `nextafter` sopra e sotto; alpha letta dal RGB, alpha test prima di dstAlpha, blend/write mask, scissor parziale. Decisione keep/kill esattamente uguale all'oracle. Compilare e renderizzare su GXM i materiali dominanti A/B: l'interprete double host non basta.

**Accettazione.** Dimostrazione delle precondizioni, nessuna variazione alpha/depth, diminuzione dei programmi con kill effettivo dove la prova lo permette, eventuale guadagno quieto. Meno caratteri Cg non è prova di meno lavoro GPU. Conservare fog e texture sampling invariati.

**Esclusioni.** Non sostituire indiscriminatamente `floor(result.a*255+0.5)` con confronti float: uguaglianza algebrica reale non garantisce identità FP32 alle soglie. Non sostituire alpha test con blend, non imporre early-Z, non rimuovere fragment scissor parziale.

## A5 Ridurre il lavoro effettivo della catena TEV

**Ipotesi.** Uno shader dominante può contenere stadi/RGB/alpha/fetch non necessari al risultato finale. Il compilatore potrebbe già eliminarli: A0 decide se vale la pena procedere.

**File.** Aurora `platforms/vita/gxm/gxm_shader_gen.cpp`, `platforms/vita/gfx/vita_pipeline_key.*`, `vita_vertex_pipeline.*` solo se cambia un requisito di input, test shader/material/geometry recipe. Ogni cambiamento di layout richiede review separata.

**A5a, analisi pura.** Costruire una rappresentazione degli stadi con versioni distinte dei registri RGB e alpha. Percorrere le dipendenze all'indietro da output color/alpha, alpha compare e side effect osservabili. Letture RGB possono consumare alpha e viceversa tramite gli argomenti disponibili; preservare read-before-write e clamp/wrap a ogni stadio. Per la prima patch restare nei casi ammessi da `native_material_supported`; casi non riconosciuti hanno liveness conservativa.

**A5b, emissione.** Eliminare solo espressioni dimostrate morte. La prima versione mantiene layout vertex e chiavi di risorsa coerenti col controllo per limitare lo scope; la riduzione di texgen/varying è un seguito distinto con rigenerazione della recipe. Una riga TEV eliminata non autorizza a rimuovere un texture binding ancora letto da indirect o da un altro stadio.

**A5c, fetch condivisi.** Proporre riuso solo con stessa unità/sampler, coordinate dopo trasformazioni, LOD/derivate, copy mode, force opacity e nessuna dipendenza indirect che li renda differenti. Non fondere due campionamenti soltanto perché puntano alla stessa texture. Limitare temporanei e verificare il codice compilato: aumentare la vita dei valori può peggiorare i registri.

**Test obbligatori.** 1–16 stadi, tutti i registri di uscita, consumo di alpha intermedio da RGB, overwrites, swap table, bias/scale/clamp, signed accumulator D, texture/ras/konst, alpha compare finale, fog, native/reference fallback. Fixture reali e almeno 10.000 programmi sintetici validi con input riproducibili; includere i casi di fallimento compilatore. Confrontare output ed effetti osservabili, non soltanto il numero di stadi generato.

**Accettazione.** Equivalenza scalar del sottoinsieme supportato e replay GXM, cache fredda/calda coerente, riduzione del costo del materiale nel probe e poi nel run quieto. Versionare/identificare correttamente la variante; conservare file Cg/GXP e fallback usato. Se il compilatore produce già codice equivalente senza beneficio misurabile, fermarsi dopo l'analisi.

## A6 Scene EFB e copie solo se il profilo le rende prioritarie

**Problema.** Le ombre e altri effetti possono introdurre target switch/copy/clear. Nel campione recente non sono però il costo CPU dominante misurato. È una riserva del piano, non il primo refactor.

**File.** Aurora `platforms/vita/gxm/gxm_renderer.cpp`, `platforms/vita/gfx/vita_efb.*`, `platforms/vita/gx/aurora_vita_draw_sink.cpp`, test backend/command stream/regression. Strikers `RenderShadow.cpp` serve per capire la sequenza, non per diminuirne la qualità.

**A6a.** Inventario per vista di Begin/EndScene, target, clear, copy, formato, rettangolo, depth load/store, transfer sync, finish con ragione, letture CPU e alias. Distinguere EndScene da finish e `draw flush` da attesa GPU. Attivare A6b soltanto se emerge una transizione ridondante nel percorso critico.

**A6b.** Eliminare una sola transizione quando target, ordine, contenuto color/depth e dipendenze restano identici. Non trattenere lo stato GXM tra due scene; non cancellare load/store se un draw successivo può leggere depth. Vietato unire oltre copy/clear/barriera, spostare `GXCopyTex`, aggiornare ombre meno spesso o cambiare shadow resolution.

**Test obbligatori.** Draw→copy→sample→clear→draw; depth conservato tra target; color-only→depth-tested; scissor non allineato; RGB565/R4/A8 e orientamento; clear con write mask; readback CPU; fallimento allocazione; risorsa riusata mentre in volo; sospensione. Confronto byte/pixel per copia e immagini al gate `TEST_PLAN.md`.

**Accettazione.** Una ragione di split/finish eliminata provata dai contatori, immagini identiche e miglioramento quieto. Se non c'è un costo misurabile, chiudere con evidenza negativa senza cambiare il renderer.

## A7 Integrazione e verifica del risultato

**A7a.** Per ogni candidato singolo: build Strikers nel profilo di controllo, audit ELF/map GXM, verifica `eboot.bin` nel VPK e sulla console, manifest completo e backup INI. Catture diagnostiche e quiete separate. Applicare i gate G0–G5 del piano test.

**A7b.** Combinare solo candidati accettati: controllo → A2 → A2+A3 → combinazione con il candidato shader. Ripetere correttezza e prestazioni della combinazione; risparmi isolati non sono additivi. Raccogliere di nuovo il profilo se il collo di bottiglia cambia. Confrontare la baseline iniziale e il controllo intermedio.

**Consegna.** Tabella baseline/candidato con media, mediana, P95/P99, quota nel budget 60, pause/transizioni, uso memoria e almeno tre coppie A/B; artifact completi e configurazione finale ripristinata. Integrare la patch Aurora nella standalone e nel pin Strikers solo dopo aver verificato le differenze e le prove. Nessun commit/push/deploy è stato eseguito da questo audit.

**60 FPS.** Dichiarare il target raggiunto solo per le scene e condizioni realmente verificate, con frame presentati e cadenza misurati, `vita_frameskip=0`, tutte le viste/shader attivi e game time coerente. Per il gate proposto: almeno il 99% dei frame gameplay entro 16,667 ms e assenza di rallentamento della simulazione; il restante 1% va descritto. Per promettere 60 FPS senza eccezioni serve anche un limite ai massimi, non soltanto P99. Se questi criteri non passano, riportare il progresso e il nuovo limite, senza promuovere un numero FPS medio a garanzia di stabilità.
