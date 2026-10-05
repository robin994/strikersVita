# Strikers Vita: dump GPU del 5 ottobre 2026

Il dump conferma un **fault di memoria GPU in Strikers**. Il problema non è
ancora corretto: la lettura dei registri identifica la pagina coinvolta, ma non
il draw o il buffer logico che ha causato l'accesso. Il confronto FPS resta sospeso.

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

## Stato della console e prossima diagnosi

Il dump, i registri estratti, l'eboot, l'INI del crash e i simboli sono conservati
localmente in `ab-artifacts/native-workflow-20261005/gpu-crash-1791191504/`.
Questa directory è ignorata da Git; il push pubblica il report e i sorgenti,
senza dump, binari, log o dati retail.

L'INI originale è stato ripristinato e letto nuovamente via FTP: SHA256
`7cb8317f6e1f77347d6cf3ba8bb3afb69eb251bffbc92678e6b00481109f8b50`.
Il candidato che ha prodotto il crash rimane installato; il binario precedente
è conservato localmente e nei backup FTP. Nessuna nuova partita è stata avviata
dopo l'analisi per dichiarare il problema risolto.

La prossima verifica deve associare allocazione, mapping, vita GPU e draw al
fault: prima un run controllato con i percorsi di riferimento, poi abilitazioni
separate e registrazione degli indirizzi dei buffer. Eventuali check degli
indici devono coprire anche Release. Un risultato senza crash in un singolo
run non basta a identificare la causa o a dimostrare le prestazioni.
