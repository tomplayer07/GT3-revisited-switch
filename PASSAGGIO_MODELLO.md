# Passaggio al nuovo modello — GT3 Revisited Switch

**Aggiornamento 4 novembre — merge 0.6.1:** lavoro su `experiment-deko3d`,
checkpoint locale `6ece111`, integrazione dei 19 commit upstream fino a
`f8d8c7c`. Il difetto delle facciate è già risolto e il test a sei auto è
stato confermato dall'utente; le istruzioni diagnostiche del 3 novembre
qui sotto sono storiche. Ora la demo usa `route5-061.gtdk`, rigenerato per
i nuovi colori/rim del decoder e i pass depth-only/DAT. R commuta la
foschia. Consultare l'ultima voce di `docs/dev/DEKO3D.md` per build/upload
e distinguere sempre i test host dalla conferma hardware.


Aggiornamento operativo 4 novembre 2026: branch verificato `experiment-deko3d`, HEAD `42521a4`; il lavoro sperimentale rimane non tracciato. Endpoint Switch ora **192.168.1.97:5000**. Fasce rosse, colonna multicolore e bandiere sono risolte e confermate dall'utente. Conservare padding delle texture statiche alte meno di 16 righe, ZeroFillInit e barriere di upload. I vecchi passaggi diagnostici descritti sotto sono storia, non il problema attuale: consultare la coda di `docs/dev/DEKO3D.md` per lo stato più recente.

Prossimo test: foschia originale Env0 di Route 5 (A=45, b≈0.5, RGB≈0.12/0.12/0.14), shader per vertice con interpolazione lineare in schermo, auto/pista/backdrop; cielo e catture ambiente esclusi. **R commuta la foschia**, non più ID/UV. Build `0.6.0-fog`; upload e verifica devono essere completati e registrati prima di dichiararli effettuati. Altri comandi e artefatti separati restano quelli indicati sotto. Nessun commit/push sperimentale eseguito.

Nuovo passo 4 novembre, dopo conferma hardware della foschia: demo a sei Mazda 787B, giocatore sullo slot 5 e cinque avversari IA sugli slot 0–4. `Session(bundle,6)` nella demo, default a una vettura per i vecchi test host. Geometria/texture condivise, pose/ruote/luci/frenata indipendenti. Asset sulla SD invariati; build `0.6.0-opponents`. Smoke host IA e build Switch passano; prossimo controllo hardware di avversari e prestazioni. R continua a commutare la foschia. Consultare coda del diario per stato upload/verifica.

Questo file conserva sotto il precedente passaggio del 3 novembre, preparato su richiesta di Lorenzo. Le informazioni tecniche sono ricavate dal repository e dalla documentazione locale; verificare la console prima di affermare che un file locale sia anche quello attualmente installato.

## Richiesta e modo di lavorare

Parla in italiano. L'utente prova il gioco sulla propria Switch; l'assistente modifica il codice, esegue personalmente le build, carica gli NRO via **ftpd/FTP**, recupera log e screenshot e li analizza. Non limitarti a spiegare all'utente come compilare o copiare: occupatene direttamente quando gli strumenti lo consentono. Lavora per passi verificabili, comunica cosa cambia e quali tasti usare per il prossimo test. Evita richieste di conferma ripetitive per le normali attività già richieste. Se la console non risponde, chiedi di aprire ftpd o di confermare l'indirizzo.

Non fare nuove build o upload solo perché stai leggendo questo passaggio: prima riprendi il problema attivo e gli eventuali nuovi risultati dell'utente. Non inventare conferme hardware. Mantieni distinti compilazione riuscita, verifica dell'upload e prova visiva dell'utente.

## Workspace e stato Git

- Mac, shell zsh; repository `/Users/lorenzo/Desktop/GT3-revisited` (il sistema può mostrarlo come `/Users/lorenzo/desktop/GT3-revisited`).
- Branch attuale: `experiment-deko3d`.
- HEAD: `42521a4` — Add manual SD disc image picker on Switch.
- Precedenti: `44aca31` ottimizzazioni Switch 0.6.0; `1dee8b4` integrazione upstream 0.6.0.
- **Il lavoro sperimentale è attualmente non tracciato da Git!** `docs/dev/DEKO3D.md`, `tools/gt3tool/cmd_deko_model.cpp`, `tools/gt3tool/cmd_deko_session.cpp`, `tools/switch/deko-model/`, `deko-probe/`, `deko-sdl/`, `deko-session/`, `tools/switch/devtool.cmake`. Non cancellarli, non usare git clean/reset e non ricominciare dal solo HEAD.
- Leggi soprattutto **`docs/dev/DEKO3D.md`**, dall'ultima parte per lo stato attuale, poi le sezioni precedenti per architettura e risultati. È il diario tecnico dettagliato, con prove hardware e ipotesi fallite.
- Altri riferimenti: `README.md`, `docs/dev/RENDERING.md`, `docs/dev/PLATFORMS.md`.

## Obiettivo e punto esatto da cui riprendere

Port Switch di GT3 Revisited, versione base 0.6.0, renderer di produzione GLES/Mesa/Nouveau. È in sviluppo un **prototipo separato Deko3D**, con integrazione SDL, per ridurre il costo del renderer specialmente nei circuiti notturni. Non è ancora una sostituzione completa del gioco.

Il prototipo attuale mostra Route 5 con Mazda 787B, fisica reale ExactRace, camera del motore, animazioni del circuito, ruote, luci, glow, catture per riflessi e cielo originale. Non presentare questi risultati a una macchina come prestazioni già dimostrate di una gara completa.

**Problema attivo:** grandi strisce/pannelli rossi sulle facciate e una colonna multicolore sfocata vicino alla tribuna/rettilineo di partenza. Serve identificare i materiali realmente responsabili prima di correggerli.

Ultima build locale: `build-switch-deko-sdl/gt3r-deko-sdl-probe.nro`, 8.638.571 byte, timestamp 3 novembre 11:54. Versione NACP nello script: `0.6.0-material-id`. Il file locale da solo non prova l'avvenuto upload.

**Ultima diagnostica implementata:** R cicla `normale -> ID texture di tutto il circuito -> UV di tutto il circuito -> normale`. Coinvolge tutte le primitive course/backdrop, non auto/cielo; le catture dei riflessi rimangono normali. L'output diagnostico è opaco e ignora blend/alpha discard. Servono screenshot dalla stessa camera dei pannelli difettosi in modalità normale e ID, eventualmente UV.

Codifica colore ID: `id = texture + 1` (senza texture = 0); RGB = `(15+28*(id%9), 15+28*((id/9)%9), 15+28*((id/81)%9))`. Campionare l'interno delle superfici JPEG, arrotondare i canali ai livelli della palette, ricostruire l'ID e quindi texture = id-1. Confrontare poi il materiale e la texture sorgente; non presumere che ogni colore rosso sia corruzione.

### Risultati da preservare e piste già verificate

- Preallocazione **reserve-only** di buffer e storage delle texture animate: allocare al caricamento, pubblicare i descrittori solo quando avviene il normale primo upload. L'utente ha confermato assenza di flash bianchi. Non tornare alla preinizializzazione che aveva cambiato il comportamento visivo.
- Culling course/backdrop secondo `kDoubleSided`, front face CCW: l'utente ha confermato la correzione dell'artefatto rettangolare sul terreno. Mantenerlo attivo; R oggi non lo commuta più.
- Policy depth write course/backdrop allineata a `kNoDepthWrite`: i pannelli rossi sono rimasti.
- Soglia alpha fallback 0.02 per blended alpha-tested course/backdrop senza riferimento esplicito, come produzione; 0.5 per opachi. I pannelli rossi sono rimasti.
- Le prime 12 texture rosse animate sospette erano un'associazione **errata** con i grandi pannelli. Readback delle 36 immagini inizializzate e confronto descrittori CPU erano corretti, ma questo non identifica i materiali delle superfici difettose.
- Diagnostica alpha/UV limitata a quelle 12 texture: cambiavano solo piccoli punti luminosi; grandi pannelli e colonna non cambiavano. Non reiterare quell'ipotesi.
- Ultimo test di quella diagnostica: screenshot `2026110311532600`, `1533100`, `1535400`, `1535800`; app mean 16.654 ms, max 18.505 ms. Sono tempi CPU wall-clock/app, non timestamp GPU.
- Baseline reserve-only e successivi test: circa 16.66 ms medi, zero allocazioni modello successive alla preparazione nel percorso normale. Le pause diagnostiche esplicite vanno escluse dai confronti ordinari.

## Build: eseguirle direttamente

Docker Desktop con immagine `devkitpro/devkita64:latest`, contesto `default`. Le cache CMake usano percorsi **dentro il container** `/Desktop/...`: rispettare i mount per riutilizzarle. Dipendenze locali esterne al repo: `/Users/lorenzo/Desktop/SDL3-test` e `/Users/lorenzo/Desktop/switch-portlibs` (montate read-only). Non sostituire l'archivio SDL con una versione generica: è presente la correzione AUDOUT audio.

Build del prototipo corrente, dalla root del repo:

```sh
docker --context default run --rm \
  -v "$PWD:/Desktop/GT3-revisited" \
  -v /Users/lorenzo/Desktop/SDL3-test:/Desktop/SDL3-test:ro \
  -v /Users/lorenzo/Desktop/switch-portlibs:/Desktop/switch-portlibs:ro \
  -w /Desktop/GT3-revisited devkitpro/devkita64:latest \
  bash tools/switch/deko-sdl/build.sh
```

Lo script prepara SDL Deko isolata, compila shader UAM, ricompila `gt3r_engine`, collega il prototipo e genera NRO con romfs. Output `build-switch-deko-sdl/gt3r-deko-sdl-probe.nro`. Conservare il log build e verificare errori/warning.

Per la build del gioco GLES di produzione usare gli stessi mount e directory, sostituendo il comando finale con:

```sh
bash -lc 'source /opt/devkitpro/switchvars.sh && cmake --build build-switch --target gt3r --parallel 6'
```

Output produzione `build-switch/gt3r.nro`. Il cache attuale punta a `build-switch/sdl-audio-fixed/libSDL3.a`; non rigenerare a caso la configurazione. Il prototipo non deve sovrascrivere questo artefatto. In precedenza si copiava anche la build di produzione su `/Users/lorenzo/Desktop/gt3r.nro` quando richiesto; fuori workspace può servire permesso.

Build host in `build-host`, tool `build-host/gt3tool`. Exporter sperimentali in `tools/gt3tool/cmd_deko_{model,session}.cpp`; leggere help/registrazione dei comandi prima di usarli. `deko-session` include smoke test di fisica, frenata, animazioni, luci, cielo ecc. Il suo linking aggiunge `tools/switch/deko-session/session.cpp` tramite `tools/switch/devtool.cmake`. Non è necessario riesportare i bundle per una sola modifica shader/materiale.

## FTP, file della console e verifica

Endpoint usato finora: **`ftp://192.168.1.49:5000`**, ftpd sulla Switch, normalmente anonimo. IP può cambiare. Usa timeout e operazioni puntuali; evita download ricorsivi dell'intera SD.

Percorsi FTP (senza prefisso `sdmc:`):

- Gioco produzione: `/switch/gt3r/gt3r.nro`.
- Directory prototipo: `/switch/gt3r-deko-sdl-probe/`; verificare il listing prima dell'upload e usare il nome NRO già presente/previsto.
- Bundle caricati dal codice attuale: `/switch/gt3r-deko-sdl-probe/route5-wheels.gtdk` e `route5-physics.gtds`.
- Log prototipo: **`/switch/gt3r-deko-probe.log`**, fuori dalla directory del prototipo; aperto in scrittura a ogni avvio, quindi recuperarlo prima di avviare un nuovo test.
- Screenshot Atmosphère emuMMC: `/emuMMC/RAW1/Nintendo/Album/YYYY/MM/DD/` (oggi `/2026/11/03/`). Fare listing e scaricare solo gli screenshot pertinenti.
- Config e log del gioco: controllare il listing di `/switch/gt3r/`; fare backup della config corrente prima di modificarla, preservando salvataggi e impostazioni dell'utente.

Esempi operativi (adattare il nome remoto dopo il listing):

```sh
curl --connect-timeout 5 --max-time 20 --fail --silent --show-error \
  --list-only 'ftp://192.168.1.49:5000/switch/gt3r-deko-sdl-probe/'

curl --connect-timeout 5 --max-time 120 --fail --silent --show-error \
  --upload-file build-switch-deko-sdl/gt3r-deko-sdl-probe.nro \
  'ftp://192.168.1.49:5000/switch/gt3r-deko-sdl-probe/gt3r-deko-sdl-probe.nro'

curl --connect-timeout 5 --max-time 120 --fail --silent --show-error \
  --output /private/tmp/gt3r-deko-upload-check.nro \
  'ftp://192.168.1.49:5000/switch/gt3r-deko-sdl-probe/gt3r-deko-sdl-probe.nro'
cmp build-switch-deko-sdl/gt3r-deko-sdl-probe.nro /private/tmp/gt3r-deko-upload-check.nro

curl --connect-timeout 5 --max-time 20 --fail --silent --show-error \
  --output /private/tmp/gt3r-deko-current-run.log \
  'ftp://192.168.1.49:5000/switch/gt3r-deko-probe.log'
```

Dopo ogni upload riscaricare l'NRO e confrontarlo byte per byte o via SHA-256: dichiarare upload verificato solo dopo il confronto. Non modificare/sovrascrivere il gioco mentre sta girando. L'utente avvia la prova ed esce con `+`, poi apre ftpd per far recuperare log/screenshot. Se un comando fallisce per restrizioni di rete/sandbox, usare il normale meccanismo di autorizzazione dell'ambiente; non aggirarlo.

## Comandi del prototipo attuale

- `ZR` acceleratore; `ZL` freno; stick sinistro sterzo nella guida.
- Click stick sinistro: camera inseguimento del motore / camera libera.
- `R`: normale / ID texture di tutto il circuito / UV di tutto il circuito. In camera libera R contribuisce anche allo spostamento verticale: preferire la camera inseguimento per confronti fermi.
- `+`: uscita pulita e report finali nei log.
- `-`: cattura manuale ambiente, pausa esplicita da distinguere dal frame time ordinario.
- D-pad giù animazioni, destra illuminazione puntuale, su ruote, sinistra glow lampioni.
- `A`, `B` e altri tasti possono essere ancora controlli delle prove SDL: leggere `tools/switch/deko-sdl/main.cpp` per il mapping completo, evitare di commutarli incidentalmente nei benchmark.

## File importanti e cautele pratiche

- `tools/switch/deko-probe/graphics.cpp`: integrazione scena, camera, catture, caricamento bundle e log.
- `tools/switch/deko-model/model.cpp`, `model.vert`, `model.frag`, `capture.frag`: renderer/materiali Deko3D e diagnostica.
- `tools/switch/deko-sdl/main.cpp`: loop, input e statistiche.
- `tools/switch/deko-session/session.{h,cpp}`: sessione fisica reale condivisa host/Switch.
- `tools/switch/deko-probe/{timing.h,frame_trace.h}`: statistiche e slow frame.
- Asset GTDK/GTDS e dati esportati dal disco sono privati e generati sotto build ignorate: non commetterli o distribuirli. Non rimuovere i bundle esistenti dalla SD.
- `/private/tmp/gt3r*` contiene molti backup, NRO riscaricati, log, screenshot e script diagnostici delle sessioni precedenti. Non ripulirli durante la ripresa; non fare affidamento sulla loro permanenza a lungo termine.

## Prima azione consigliata al nuovo modello

Leggere questo file e la coda di `docs/dev/DEKO3D.md`, verificare `git status`, poi riprendere la diagnostica **all-course texture ID**. Se l'utente ha già effettuato la prova, recuperare log e screenshot e identificare le texture dei veri pannelli difettosi. Se non l'ha fatta, verificare quale NRO è sulla console, caricare e verificare la build attuale se necessario e dare le istruzioni per screenshot normali/ID dalla stessa camera. Aggiornare il diario tecnico dopo ogni risultato significativo.
