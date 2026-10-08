# Audit Aurora Vita per Characters e Shadowed

Data: 8 ottobre 2026. Stato: audit completato, interventi proposti; nessuna ottimizzazione implementata in questo audit.

Il lavoro prioritario è ridurre il costo ripetuto di traduzione dello stato, preparazione degli uniform e shader dei draw dominanti. Prima di attribuire il divario dai 60 FPS a due sole viste serve una cattura aggiornata che colleghi vista, draw, shader effettivo e frame completato.

I controlli quieti verificati sono a 39,478–40,779 ms/frame, circa 24,5–25,3 FPS. Il budget è 16,667 ms: mancano circa 22,8–24,1 ms/frame, una riduzione del 57,8–59,1%. Le misure disponibili non dimostrano che `Characters` e `Shadowed` siano l'unico limite residuo. Non sono state raccolte nuove misure sulla console durante questo audit.

Documenti da consegnare agli esecutori:

| Documento | Uso |
|---|---|
| [REPORT.md](REPORT.md) | Risultati dell'audit, evidenze, punti del codice e priorità |
| [TASKS.md](TASKS.md) | Incarichi separati con scope, dipendenze, fallback e criteri di chiusura |
| [TEST_PLAN.md](TEST_PLAN.md) | Oracoli, casi di regressione, comandi verificati e protocollo Vita |
| [EVIDENCE.json](EVIDENCE.json) | Revisioni, hash dei sorgenti e delle catture, configurazioni e statistiche |
| [HOST_TESTS.log](HOST_TESTS.log) | Esecuzione corrente della suite Aurora: 18/18 test superati |

Ordine consigliato: A0 → A1; poi A2 e A3 sul lato CPU; A4 e A5 solo sui materiali risultanti dalla classifica A0. A6 è subordinato a una misura che dimostri un costo EFB/scene rilevante; A7 integra e valida i candidati accettati. Le dipendenze dettagliate sono in `TASKS.md`.

Ogni esecutore riceve **un solo sottoincarico**, insieme a questi documenti. Deve consegnare una patch con test e risultati, senza promuovere i default dopo il solo test host. Il piano è scomponibile per modelli meno costosi; le modifiche che attraversano FIFO, lifetime e GXM richiedono comunque revisione d'integrazione. Il contratto [AGENTS.md di Aurora](../../smstrikers-port/extern/aurora-vita/AGENTS.md) prescrive un modello con reasoning alto per modifiche trasversali del renderer. Il lavoro di questo audit non delega né avvia implementazioni.

L'Aurora effettivamente usata da Strikers è `smstrikers-port/extern/aurora-vita`. La copia standalone in `/Users/robin994/Documents/Code/PSVita/aurora-vita` ha una revisione diversa. Non sostituire il submodule né copiare file interi tra checkout: leggere prima le differenze registrate nel report.
