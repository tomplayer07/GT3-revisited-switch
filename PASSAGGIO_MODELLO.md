# Passaggio al nuovo modello — GT3 Revisited Switch

**2026-11-08 — profilo unificato Deko3D:** `src/app/main.cpp` imposta ora `GT3R_DATA_DIR=sdmc:/switch/gt3r/`, lo stesso percorso dell’NRO GLES; log e `config.ini` condivisi. Build `build-switch-deko-app/gt3r-deko-app.nro` riuscita e caricata in `sdmc:/switch/gt3r-deko-app/gt3r-deko-app.nro`. Profilo Deko precedente lasciato intatto. Dati uniti senza sovrascrivere i progressi: configurazione/playlist/cache Deko installate nel profilo comune; copie della vecchia config e playlist/cache OpenGL in `config.opengl-backup.ini`, `music_playlist.opengl-backup.txt`, `music_songs.opengl-backup.txt`. Save OpenGL attivo `GAME.sav` e `current.txt` restano invariati; save Deko copiato, con backup, come `saves/Deko Alpha.sav[.bak]` per poterlo caricare dal gioco. Nessun commit/push.


**Ultima build 0.6.1-dk-cache — OC rettificato / ottimizzazioni, hardware pending:** utente scopre OC acceso e precisa spento DOPO ~20s della stessa gara. Log `/private/tmp/deko-prof-oc-stock.log` è MISTO: NON chiamare cadence22.425ms/44.6FPS una misura stock; non assumere che vecchia demo/race5 fossero stock, clock non letti allora. Profiler GPU validato (invalid_reports0), UI/arena wait0. Dati misti indicativi: renderCPU16.245ms, sim2.285; GPUelapsed main18.795/scenery14.020/carsEffects4.808 (frame diversi e CPU feeding incluso, NON tempi busy né sommabili). Nuova build ottimizza: InstanceTransformCache riusa world*local quando placement identico; invalida su updateGeometry e PRIMA applicazione nuova per nonvalidare stati parziali falliti. Car pose/camera restano aggiornate. Sync texture salta scansione famiglia se TextureUpdates.serial invariato, serialreset0 a ricostruzione modello; GPU animazioni perbanco restano aggiornate con pixel CPU persistenti. Salta look_at/perspective/trig placeholder se camera esplicita (calcoli prima sovrascritti). Intervento principale static Scene primitive culling: bounds da ModelMeshInfo.primitive_spheres già assemblate (cubo che contiene sphere); dentroModel.submit dopo phasefilter e prima drawstates, usa stessaViewCuller. Solo frontend frustumCull, roleScene, nonbackground/helper, modelviewScale==1, nessun updateCalls; auto ruoloCar, moving/dynamic e backdrop scalato esclusi. Mantiene ordine/far/distanze/effetti. Counters Native static primitive culling tested/rejected e Native CPU cache prodotti/skips/scans. ClockReader legge CPU/GPU/EMC via clkrst (HOS>=8, session unk3 come documentatoSDK) o pcv vecchio: init lazy al primo frame live, polling1s, NESSUN setClockRate. Fallimenti espliciti unknown_mask, niente stock inventato. Servicerates configurati, NON frequenzimetro fisico. ClockChange frame-tagged, GPU sample tiene snapshot relativo al suo frame anche se callback arriva dopo cambio; aggregati restano misti e log avverte. Cap128 clock changes; GPUworst16 ha copia dei clock. Nuovi test worldcache invalidate/failure/instancing, serial atomic/no-op, clock retrospective tagging, primitive frustum crossing/mirror/scale e meshvisibile con primitive fuori vista.629 host passati,235 skip, SDK native/GLES compilano. NRO Switch/Desktop aggiornato, roundtrip FTP byte-identico verificato. Profilatore precedente e race5 validata mantenuti; ELF cache-debug. Prossima prova90-120s stessaStage5 con OCSPENTO DALL'AVVIO E PER TUTTA LA GARA, chiudereapp e ftpd. Non promettere60stock o improvement prima hardware. Nessun commit/push.


**Ultima build 0.6.1-dk-prof — profilazione mirata, hardware pending (2026-11-06):** utente autorizza ripresa da race5 validata. Nessuna ottimizzazione o riduzione effetti in questo turno: preparata misura live per scegliere il collo rettilineo/avversari. NativeRenderer.profile_race_frame/cpu chiamati solo guida live (no quick preview/menu/pause/demo/replay/finished; race_time>=0); scope CPU parte PRIMA upload animazioni. RaceCadenceGate esclude intervalli non consecutivi fra gara e menu/caricamento. Model SubmissionTiming aggiunge draw/triangoli; ProfilePass su options (Env,WetScene,WetBlur,Main,Mirror,Post), ProfileRole in coda DrawItem (Car su CarVisual body/wheels/shadows, Effect smoke; Scene significa scenery_other). Counts body sono ISTANZE main-body con draw, non auto uniche/pixel visibili; split può duplicarle. Summary live: cadence, sim/render/env/collect/draw/glows CPU, acquire/UI/present CPU, per-pass/role invii/draw/triangoli, bins bodies0-1/2-3/4+, top16 frame peggiori con x/z/speed e CPU. GPU: FrameArena emette 2 ReportCounter Timestamp per campione (SDK docs {counter64,timestamp64}, allineamento16; usiamo slice256); campiona1/32frame solo dopo128warm, ruota8 range envface/wetScene/wetBlur/mainScenery/mainCarsEffects/mainAll/mirror/post. Uno solo range per frame; mainScenery finisce al primo item Car, mainCarsEffects da lì a fine scena incl effetti/blended residui (non puro shader auto). Range diverso di frame diverso NON sommabili; elapsedGPU include attese/CPU feeding, non exclusive busy. Report forza tile-cache flush (Primer SDK verificato), costo diagnostico noto anche se raro. GPU sample letto solo quando banco riusabile dopofence, nessun nuovo CPU wait; FrameArena.end fence flushcacheTRUE solo se report presenti; codequery mem ownedarena e oggettoCPU perbank, dati32bytes inizializzati0. Dtor drena report dopo waitIdle prima di distruggere chunk; raceProfile.report dopoarena.reset includeultimi. invalid_reports contati, zero/reversed GPUinterval scartato. Log RAM come race5: serve CHIUDERE APP prima ftpd. Protezione ABI: features high16 layoutRevision1 per offset/vtable cambiati anche sizeof uguale. Host623 passati,235 skip, SDK native/GLES riusciti. NRO Switch/Desktop aggiornato e roundtrip FTP byte-identico verificato. ELF dk-prof-debug e fallback race5-hardware-validated.nro già preservata. Nessun commit/push. Prossimo test unico90-120s Stage5 conavversari, guidare normalmentee chiudereapp. Profilazione PERFORMANCE va validata (marker GPU ancora mai provati sul dispositivo): se crash usare matchingELF; non perderebaseline race5.


**RACE5 CONFERMATA HARDWARE — nuova baseline:** utente: «gira moooolto meglio», carico/uscita più veloci; cali residui sul rettilineo principale e con avversari. Log `/private/tmp/deko-race5-driven.log`: route5_dry / _787b_91 single_race G_80sports_n_0002, Race loaded4736ms; ZERO UI/target pressure, ZERO frame-arena bank waits, 675185 lease ritirate, nessun draw failed/callback errore. Arena high-water243269632B (~232MiB), non memoria per fotogramma cumulativa. Model summaries normalizzati: waitmedio/invio0.0005ms contro0.162ms race4;719439 invii su9754present (sessioni durata diversa, non confrontare totali o inferire FPS gara dalla cadence globale). Cadence globale19.451ms include menu/load,max5267ms; NON chiamarla FPS pista. SDK/host617 test della baseline già passati, non ricompilare o ritestare le stesse invarianti senza cambio. Preservare NRO/ELF race5 come fallback e non sovrascrivere app produzione. Nessun nuovo codice renderer in questo turno: raccolta evidenze e consolidamento baseline. Prossimo collo da profilare: rettilineo/avversari, separando CPU raccolta/invii, numero primitive e carico GPU effettivo; evitare tagli grafici o dare per certo un collo GPU solo da bank_waits0. Resta parità rendering incompleta come sotto (replay DOF/trail/dissolve, guardband/AFAIL/gradeglows). Non commit/push senza richiesta.


**Ultima build 0.6.1-dk-race5 — frame arena / log RAM, hardware pending:** race4 confermata ancora lenta. Log `/private/tmp/deko-race4-driven.log`: zero pressure UI/target (fix reader riuscito), 259736 lease ritirate, 291091 invii modello, ~47.15 s fence wait aggregati; cadence media include menu/caricamenti, NON FPS gara. Analisi: i due slot per Model venivano riusati tra fasi/ruote/viste nello stesso frame con flush/wait serializzante; centinaia di modelli allocavano propri buffer e facevano queueIdle a distruzione; trace printf+fflush per riga su SD in load/unload. Ora FrameArena CONDIVISA GraphicsState: tre banchi, porzioni immutabili diverse per ogni invio (comandi, uniformi, fog/luci, descrittori, snapshot vertici dinamici). Si aspetta solo al riuso del banco dopo fence fine 3D; end chiamato in present prima UI (UI non usa questi buffer). Nuovi blocchi 4 MiB riusati, dimensionamento controllato, nessun ZeroFill per regioni sempre scritte. Modelli native non allocano più command/uniform/fog/light buffers propri a caricamento; tre DkCmdBuf piccoli creati lazy. Sampler/descriptor set copiati per invio, niente overwrite tra viste. Vertici dinamici snapshot arena per generazione/versioni; texture animate conservano immagini per banco e aspettano solo nel raro cambio revisione nello stesso banco e frame. Model.wait_submissions ritira fence specifiche, non tutta queue per ogni pezzo; legacy senza frameArena invariato. NativeReadState ora FencedReadList per tenere TUTTI gli invii dello stesso modello nel banco (nessun overwrite risorse con tre slot). Registry weak resta main-thread. Diagnostica native in RAM (cap4MiB) scaricata a fine contesto/app o callback Deko fatal; GT3R_DEKO_SYNC_LOG env riattiva sincrono per debug. Per prossimo log chiudere APP, non solo gara; crash CPU senza cleanup può perdere trace RAM. Test FrameBankCycle/reset-after-fence, fallimento wait senza reset, frame_slice anti-overflow/non-overlap, reader multipli stesso frame. 617 host passati,235 skip; SDK native/GLES compilano. Allineamenti SDK verificati DK_CMDMEM_ALIGNMENT4, DK_UNIFORM_BUF_ALIGNMENT256, slice256. Ultima revisione riduce barriere native tra modelli della STESSA vista: bind RT/barriera solo al clear iniziale, niente barriera finale per ogni modello; completion fence View con flush e barriera iniziale della vista successiva conservate. Invalidate descrittori native None invece di Fragments, upload texture mutate conservano proprie barriere. Build finale /private/tmp/deko-race5-barrier-build.log riuscita. NRO finale Switch/Desktop aggiornato; rilettura FTP byte-identica verificata. ELF race5-debug; fallback race4-before-frame-arena. Nessuna promessa 60FPS prima hardware. Prossimo test stessa gara, valutare anche carico/uscita. Produzione e branch switch-port invariati; siamo experiment-deko3d.


**Ultima build 0.6.1-dk-race4 — performance fix, hardware pending:** race3 prima gara giocabile confermata, ma utente riporta prestazioni pessime e distanza ridotta. Log `/private/tmp/deko-race3-driven.log`: 3673 present (include menu/caricamenti), 2795 early UI batch, 2828 reader waits; sei viste cube ~450–479 pressure waits ciascuna e `Native view images still retained by UI or native texture consumers`. Non stimare FPS gara dall'avg cadence 54.452 ms: include max50.48 s caricamento. Sommari modelli: 367016 invii, ~42.6 s CPU fence wait aggregati, molte passate con last_draws=0. Causa concreta: sampledTextures manteneva i cube snapshot fino al prossimo draw del modello; modelli non più visibili bloccavano cinque versioni per faccia. Fix: FencedReadSlots<DkFence,2>, copie fence invio, registry WEAK in GraphicsState solo registrato al primo draw main (mai worker); raccolta non bloccante prima riuso e dopo pressure wait. Risorse campionate si rilasciano al completamento effettivo anche modello inattivo. Modello possiede lo stato, nessun ciclo owner forte nel registry. Due test CPU su reader inattivo e condivisione multislot. Saltate passate solid/blended senza primitive del LOD attivo; clear iniziale e depth_reset restano eseguiti anche senza geometria. Rimosso cap Switch650m SOLO per native, GLES conserva650; native segue far piano GT3 e fallback2500, env capture200 e mirror120 sono intenzionali. Ombre vengono dalla CarVisual dell'app, geometrie già preparate dalla logica esistente, non feature nuova della demo. 613 test host passati,235 skip; SDK Deko/GLES compilano. NRO Switch/Desktop aggiornato e download FTP byte-identico verificato. Versione race4; ELF race4-debug e fallback race3-before-reader-fix. Niente commit/push. Prossima prova ancora stessa gara; misurare effetto reale prima promesse 60FPS.


**Ultima build 0.6.1-dk-race3 — caricamento da verificare:** race2 utente riferisce ancora blocco, ma chiarisce attesa MENO DI UN MINUTO. Nuovo log `/private/tmp/deko-race2-failed.log` si ferma ancora negli upload auto (ty0139), PRIMA di Race: loaded e PRIMA della scena/checkpoint del race1. Quindi questo tentativo NON riproduce documentatamente lo stesso blocco GPU; serve lasciare completare il caricamento, fino a 2 minuti per una prova. Non dichiarare che fosse sicuramente solo loading né che il fix race2 risolva. Ridotto buffer command upload iniziale: frontend 64 KiB + 512 B/immagine allineati 4 KiB, massimo ~580 KiB, anziché 4 MiB per ogni parte auto; geometrie derivate condivise hanno zero copie texture. Legacy mantiene 4 MiB anche per readback. Il buffer era già liberato dopo upload: beneficio atteso costo di allocazione/azzeramento, NON riduzione della memoria GPU persistente o FPS misurati. Nuovo log Model initial upload command pool bytes. Test budget copre zero immagini, piccole parti, max1025, allineamento e invalid count. Build native riuscita, host suite /private/tmp/deko-race3-tests.log: 611 passati, 235 skip, zero failure. NRO Switch/Desktop aggiornato; download FTP byte-identico verificato. ELF race3-debug, fallback race2-before-upload-budget.nro. Stessa gara dai menu, nessun viewer o config di produzione.


**Ultima build 0.6.1-dk-race2 — freeze fix, hardware pending:** race1 NON confermata: utente riferisce blocco senza uscita quando avvia single_race route5_dry / _787b_91. Log `/private/tmp/deko-race1-failed.log`: caricamento completato in 32100 ms, riflessi e wet buffer preparati, prima scena 88 item inviata; ultima riga `Native frontend target preparation begin`, nessun draw failed o callback Deko. 279 upload, ~290.6 MiB allocazione residua stimata sottraendo command buffer upload già rilasciati (cumulative non memoria live). Non diagnosticare OOM usando 1406 MiB cumulativi. Ipotesi attuale: retirement/unmap risorse UI durante nuova allocazione con lavoro graphics ancora buffered. Fix: dkQueueFlush prima preparazione target e prima collect_ui_images nel percorso upload_ui_texture; nessuna queue idle per frame, solo invio prima nuovo upload. SDK MemBlock::destroy fa unmap; la causalità specifica del freeze richiede conferma hardware. Aggiunti checkpoint prima/dopo invio, retirement, allocazione colore e CPU buffer. Build native riuscita, nessun codice GLES cambiato in questo fix; 610 test precedenti validi, non spacciarli per verifica del freeze. NRO Switch/Desktop aggiornato; rilettura FTP byte-identica verificata. ELF race2-debug, fallback race1-before-queue-fix.nro. Args viewer già assenti: prossimo test stessa gara da menu. Se persiste, usare ultimo checkpoint e verificare GPU error/completamento scena, non aggiungere workaround casuali o tornare a viewer.


**Ultima build 0.6.1-dk-race1 — hardware pending:** utente conferma che pista demo andava bene e chiede meno prove tecniche. Il log scene `/private/tmp/deko-scene-hardware-success.log` conferma tutti i modi 0..4, 1410 frame, zero draw failed/pressure. Ora rimosso il blocco start() native nella RaceScreen: stesso flusso gioco/ISO/fisica/AI/HUD/audio dei menu. Env cube e wet road usano esclusivamente risorse native tipizzate (inclusi blur e composite via alpha destinazione). View3D ha vista postprocess native per grade finale con aritmetica GT3 originale; overlay/vignette composti SDL dopo grade. Adapter scene: max256 item, scheduling background → solidi → blended degli in_order/join prima dell'item successivo → blended globali (ordine per item, non mesh) → late; depth_reset solo fine gruppo. Modelli possono inviare solo fase solidi/blended mantenendo target e Z comuni. Collegati kAlphaOnly/kKeepAlpha/kDestAlphaAdd e composite wet 0x54/0x58, emissione lamp materiali da item.lamp. Shader env_kind 2/3 per vertice + sheen specular_local, GS bilinear per offscreen. Ancora NON parità completa: guard band usa clipping GPU, mancano suddivisione VU/far_whole e AFAIL RGB_ONLY, replay DOF/trail/dissolve, grade dei glows (ora dopo grade); wheel spin blur forzato off solo native. Non dichiarare renderer finito né prestazioni misurate. 610 test host passati, 235 skip (disc non configurato), build SDK Deko/GLES riuscite in /private/tmp/deko-race-final-build.log; ultimo controllo cache blend/keep-alpha ricompilato in /private/tmp/deko-race-blend-final-build.log. NRO finale Switch/Desktop aggiornato e download byte-identico; args.txt del viewer rimosso, listing verificato. ELF gt3r-deko-race1-debug.elf; fallback gt3r-deko-scene-before-race.nro. Build script stage shader in /tmp container prima UAM: compilazioni dirette mount hanno dato EOF inattesi, copie locali riuscite. Nuova prova richiesta: singola sessione di gara dai menu con avversari, pausa/rientro; non altri viewer. args --screen/reflections RIMOSSI: avvio normale dai menu. Produzione gt3r.nro non trasferita, profilo sperimentale separato.


**Ultima build 0.6.1-dk-scene — hardware pending:** utente conferma viewer riflessi funzionante; log `/private/tmp/deko-reflect-hardware-success.log`: 1550 frame, camera 1364 draw, sei facce ~259 ciascuna, pressure drains/waits e early UI batches tutti zero; nessun draw failed. Ora backend accetta ItemLights (copiati subito, 0..4 luci validate) e background; helper scene_state.h impacchetta luci/fog per slot e scambia culling nelle viste mirrored. Shader: nebbia lineare/GT3, no_fog/helper/background rispettati, far plane frammenti (non ancora far_whole), quantizzazione RGB5 e lookup cube GT3. Frustum culling frontend opt-in e draw_distance per item collegati. Background senza Z-write/test/cull/fog, trasformazione caller conservata; nessuna garanzia di sorting globale della gara. Viewer reflections esteso con cinque modi sinistra/destra: Base, Luci per auto (rosse/blu animate solo Mazda), Nebbia lineare, Nebbia GT3, Vista specchiata (monitor single_sided esercita winding). Fondale colorato volutamente non annebbiato. L/R/A/+ come prima. 605 test host passati (235 skip), zero failure; SDK native/GLES riuscite. NRO Switch/Desktop aggiornato e download di verifica byte-identico. Args resta --screen reflections nel solo profilo Deko. Preservare fallback reflect-before-scene.nro ed ELF scene matching. Gare app ancora bloccate, frame-alpha/GS scene semantics/overlay e consumer gara restano da integrare.


**Ultima build 0.6.1-dk-reflect — hardware pending:** prova dynamic confermata dall'utente. Log `/private/tmp/deko-dynamic-hardware-success.log`: 1501 frame, malformed vertex/batch entrambi rejected=1, R cicla correttamente; nessun fallimento rilevato. Corretto A/B solo nei tool Switch (Circle conferma, Cross indietro). Ora API tipizzate NativeView.render_texture, NativeRenderer.create_cube, View3D.render_native_offscreen/render_native_env_cube/native_env_face; DrawItem.native_texture e DrawOptions.native_env_cube. Cubo da sei texture 2D, cattura iniziale/resize completa, aggiornamenti mascherati transazionali. Target con cinque versioni trattenute da shared_ptr oltre alle lease SDL; risorse campionate trattenute fino al riuso fenced dello slot modello. Otto descrittori esterni per slot: sphere, sei facce cube, override immagine. ABI include sizeof(DrawItem). Viewer `--screen reflections`: Mazda async, sfera riflettente, monitor 3D con seconda camera, sei facce mostrate via SDL; L riflessi, R congela monitor, A pausa, + esce. 600 test host passati, 235 skip; build SDK native e GLES riuscite. Args del solo profilo Deko sostituiti da dynamic a reflections; rimuovere dopo test per avvio normale. NRO Switch/Desktop aggiornato e verificato byte-identico; ELF gt3r-deko-reflect-debug.elf, fallback gt3r-deko-dynamic-before-reflect.nro. Gare app ancora bloccate: API legacy SDL_GPU restano non supportate sul backend native, consumer gara da adattare. Versioni trattenute a lungo da consumer inattivi possono esaurire pool: strategia conservativa, non ancora raccolta separata dei reader GPU.

**Ultima build 0.6.1-dk-dynamic — hardware pending:** collegati NativeModel.update_vertices/update_geometry/update_textures/share_model_textures. Fonti GPU originali condivise tramite famiglia forte con root Model; aggiornamenti texture CPU copiati atomicamente e propagati ai consumatori, ciascuno con sostituzioni animate proprie per slot/fence (non cache GPU animata unica). Tiny textures pad a H16 come uploader statico. Layout stabile: ring vertici esistente, niente riallocazione GPU; cambio indici/materiali/numero vertici: sostituzione transazionale geometria con immagini originali prese in prestito, uploadQueue serializzata. Topologie variabili frequenti NON ancora ottimizzate: sostituzione/empty/destructor ritirano invii con queue idle per proteggere sphere leases quando famiglia tiene vivo il vecchio Model. Empty update valid=true, draw clear-only senza vecchia geometria. Context upload ora shared in FrontendUploads e creato main; root/derivati lo tengono vivo. Helper CPU deko/dynamic_model.h + same_model_geometry_layout testati. 597 test passati, 235 skip; SDK Deko/GLES compilano. NRO Switch/Desktop aggiornato. Creato args.txt nel SOLO profilo Deko con --screen/dynamic (prima assente): rimuoverlo dopo prova. Screen mostra tre bandiere (vertici diretti/geometria stabile/topologia variabile) con colori condivisi, Mazda 787B ma0095 di confronto caricata async e invariata. A pausa, L colori, R cicla terza bandiera 4 strip/12 strip/vuota/4; + esce, B torna ai menu. Prove malformed vertex/batch texture solo native, log rejected=1 atteso. ELF gt3r-deko-dynamic-debug.elf; fallback prizes-before-dynamic.nro.

**Conferma hardware premi:** utente conferma tutti i premi corretti tranne unfortunate bianco. Log `/private/tmp/deko-prizes-hardware-success.log`: 55 caricamenti presentazioni, 3798 present, zero draw failed, composizioni 2 item per patenti, zero backpressure. `unfortunate.prm` è documentato segnaposto disco bianco non texturizzato (docs/formats/prm.md); nel log menu/unfortunate 91 draw completati, quindi non mancata submission. Nessun riferimento esplicito unfortunate nei flussi premi src/game; consolation usa consolation.prm. args.txt galleria RIMOSSO via FTP e listing verificato: prossimo avvio normale. Native NRO rimane dk-prizes. Prossimi lavori: risorse dinamiche/condivisione texture, texture native generali e integrazione scene gara; non ripetere test premi già confermato.

**Ultima build 0.6.1-dk-prizes — da testare:** utente conferma async «quasi perfetto, va bene così». Log `/private/tmp/deko-async-hardware-success.log`: 2458 present, 11 early batch, 16 reader waits, nessun draw failed; view released con 3209/1633 draw. Non convertire tempi CPU/cadence in FPS GPU. Implementati più DrawItem nella stessa vista (max64, prevalidati), colore/profondità condivisi e clear solo primo modello. Slot uniformi per modello anziché per vista; guard fence copre anche invii parziali. Parametri item: LOD, alpha, tint, light_scale, reflection_scale, depth_reset; resource/effects non supportati rifiutati. Shader native supporta sphere map propria dei PRM (distinta da environment auto), additive e MultiplyDest; materiali/culling PRM e ombre su depth comune. Abilitati premi nei menu (singola passata native). Nuovo viewer sviluppatore `--screen prizes` con coppe e patenti reali da ISO, niente salvataggi modificati. NRO Switch/Desktop aggiornato. Creato nel SOLO profilo Deko `/switch/gt3r-deko-app/args.txt` con `--screen` e `prizes`: rimuoverlo dopo questa prova per tornare all’avvio normale (non esisteva prima). Controlli: sinistra/destra premio, A pausa, L sfondo, + esci, B torna ai menu. 593 test passati, 235 skip; SDK Deko/GLES compilano. ELF gt3r-deko-prizes-debug.elf; fallback async-before-prizes.nro.

**Ultima build 0.6.1-dk-async — prova hardware pending:** utente riferisce ancora lag nella dk-load. Log `/private/tmp/deko-load-hardware.log`: 20 caricamenti, total main-thread 180.38–227.86 ms (media 195.44); uploader backend ~99–117 ms, GPU wait <2 ms. Ora lettura/decodifica/GpuModel native su worker con `render::MenuCarLoader`: un job per picker, polling non bloccante, niente backlog; MenuModelRenderer conserva cache per chiave, CarTurntable scarta risultati superati. `MenuCarRequest` possiede opzioni/copiedeDB, niente puntatori sessione nel job. Backend opt-in `background_model_upload`; una coda upload Deko separata creata all’avvio, serializzata da mutex; shader/device letti, coda UI mai usata dal worker. Upload completato sulla coda dedicata prima di pubblicare modello. Distruzione modello frontend mai disegnato evita coda render. Join di tutti i job prima di chiudere/cambiare VFS e prima di modificare mod/lingua; release widget e picker drenano i rispettivi job. 590 test host passano (235 skip), SDK native e GLES compilano (GLES riuscita al recheck dopo lettura incompleta App.cpp nel mount Docker). NRO Switch/Desktop aggiornato; non dichiarare fluidità risolta prima del test. ELF matching gt3r-deko-async-debug.elf.

**Trasferimento autorizzato dopo ricarica:** build 0.6.1-dk-load caricata su 192.168.1.97 in `/switch/gt3r-deko-app/gt3r-deko-app.nro`; download di verifica byte-identico. Copia Desktop `gt3r-deko-app.nro` aggiornata. Prova cambi auto e tempi di caricamento ancora da ricevere. Il vincolo precedente di non trasferire era dovuto alla batteria ed è stato revocato dall’utente.

**Ultimo stato 0.6.1-dk-load — SOLO LOCALE:** utente dice Switch scarica e chiede di non inviare nulla. Nessun upload dopo questa richiesta; Switch/Desktop restano alla build pool. Log ricevuto `/private/tmp/deko-pool-hardware.log`: nessun warning pool/fallimento draw, ma caricamento auto 186–285 ms, GPU upload wait <2 ms. Nuova build locale riduce allocazioni texture a due arene (immagini/staging), buffer statici dimensionati ai draw reali e due slot senza cattura, niente readback diagnostici nello showroom, cache native LRU (non clear-all). Aggiunti tempi read/parse/upload nel log menu. `model_allocation.h` testa packing allineato, overflow e budget. SDK Deko e GLES compilano; 586 test host passati, 235 skip. Non dichiarare miglioramento misurato: prova hardware pending. NRO locale `build-switch-deko-app/gt3r-deko-app.nro`; ELF corrispondente `gt3r-deko-load-debug.elf`.

**Ultima build 0.6.1-dk-pool — da testare:** corretto esaurimento pool immagini anteprima. Prima del present il backend tratteneva lease nei comandi UI ancora CPU; ora sul contesto native-only invia i batch pendenti sulla stessa immagine swapchain acquisita una volta per frame. Ring command-memory distinto dall’indice swapchain (`SubmissionFrame`). Se necessario aspetta fence dei soli batch UI e poi della versione scritta, senza queue-wide idle né crescita pool. Demo gara conserva percorso present precedente. Log di backpressure riepilogato e primi eventi tracciati, cinque versioni invariate. Nuovi test host: acquisizione singola per frame con molti batch, ring indipendente, riprova dopo errore. Suite zero errori, 235 skip; build SDK riuscita. NRO separato Switch/Desktop aggiornato; conferma hardware nuova build ancora necessaria.

**Conferma hardware 0.6.1-dk-fence:** utente: «Ok fatto, funziona». Log `/private/tmp/deko-showroom-fence-success.log` conferma numerosi cambi auto, invii e riuso slot dopo dispatch coda. Freeze risolto nella prova. Restano 206 warning `Native view versions still in use; no unsafe overwrite performed`: pool immagini può esaurirsi transitoriamente; non dichiarare log pulito né uscita pulita (log termina sul warning). Prossimo lavoro: backpressure tra viste/SDL e poi supporto più DrawItem, premi animati e gare.

**Correzione blocco showroom — da ritestare:** l’utente segnala freeze con audio attivo. Log `/private/tmp/deko-showroom-freeze.log`: auto caricata, primo NativeView inviato, seconda vista preparata, nessun primo invio seconda vista. Il modello riusa lo slot 0 alla terza passata prima del present. `dkQueueSignalFence(..., true)` emette flush cache GPU, NON il kickoff della coda CPU (verificato nel sorgente SDK ufficiale, copia `/private/tmp/deko-sdk-queue.cpp`). Aggiunto `dkQueueFlush` prima delle attese CPU bloccanti di modello, UI, pressione upload e distruzione target; polling non bloccante invariato. Build `0.6.1-dk-fence`, compilata; nuova prova hardware necessaria.

**Ultimo stato — showroom nativo (da testare):** menu reali confermati dall’utente, log `/private/tmp/deko-real-menu-hardware.log` (3804 frame). Build nuova `0.6.1-dk-cars`: GpuContext nativo attivo, auto statiche nei menu con luci showroom, sphere map tipizzata e specchio sul pavimento; una passata senza accumulation AA. Gare bloccate esplicitamente prima di caricare risorse; premi animati non ancora supportati. NRO Switch/Desktop `gt3r-deko-app.nro`, profilo/log dedicato `/switch/gt3r-deko-app/`. NativeView usa cinque versioni; mappe trattenute per slot GPU. Test host zero errori; conferma visiva ancora da ricevere. Stato precedente sotto è storico.

**Stato aggiornato 2026-11-05:** frontend indipendente confermato su hardware (365 frame, cinque resize, uscita pulita). Ora compilata e installata app reale con menu SDL Deko, titolo `GT3 Revisited Deko`, NRO `/switch/gt3r-deko-app/gt3r-deko-app.nro`, log nella stessa cartella `gt3r.log`. Copia Desktop `gt3r-deko-app.nro`. Conferma hardware di questa nuova app ancora da ricevere. Profilo/config separati, ISO condivisa tramite percorso assoluto. GPU app intenzionalmente nulla: anteprime 3D e gare ancora da integrare; attract demo disabilitata. GLES produzione invariato. Vedi ultima voce DEKO3D.md. Le voci sotto sono storiche.


**Nuova prova indipendente dalla gara (da confermare su hardware):**
factory pubblica `render::deko::create_renderer(window)` e startup
native-only che salta Session/Route 5/pacchetti. NRO separato
`gt3r-deko-frontend`, titolo GT3 Deko3D Frontend, log
`/switch/gt3r-deko-frontend.log`. Parte sul cubo; A ridimensiona la
vista, + esce. Entrambe le build passano. La demo pista installata
resta quella confermata; nessuna sostituzione di gt3r.nro.


**Confermato hardware:** dopo fix flag ABI e callback SDL, l'utente vede
il cubo. Log /private/tmp/deko-native-view-success.log: 224 draw frontend,
tre target 350x197, 279 frame gara, uscita pulita. Un picco ~159 ms alla
prima preparazione con A; prova breve, non benchmark. Ora esiste una
vista base reale attraverso GpuContext/GpuModel/View3D, ancora dentro
la demo. Prossimo passo: avvio device indipendente dalla scena di prova.


**Crash A in verifica:** corretto prima il flag `__SWITCH__` mancante
nella demo (layout ABI diversi). Il nuovo log conferma ABI corretta ma
un secondo crash è stato simbolicato: SDL_FlushRenderer chiama
InvalidateCachedState nullo. Driver corretto e flush provato all'avvio
nella build `0.6.1-native-flush`; conferma hardware ancora da ricevere.
ELF corrispondente salvato in build-switch-deko-sdl/gt3r-native-flush-debug.elf.
Non dichiarare ancora il cubo funzionante su console.


**Adapter concreto iniziale:** la demo ora adotta un NativeRenderer Deko
reale per un cubo procedurale, attraverso GpuContext/GpuModel/View3D.
A mostra il cubo rotante nel pannello oltre alla miniatura della pista.
Pool di tre versioni colore/profondità, wrapper persistenti e riuso
protetto da riferimenti UI e fence. Build `0.6.1-native-view`; vedere
coda diario per upload e conferma hardware. Rimangono avvio device
indipendente dalla demo, modelli dinamici, più oggetti e parità effetti.
Il gioco completo continua a usare GLES.


**Nuovo percorso frontend:** `render/native_renderer.h` e
`GpuContext::adopt_native` permettono a GpuModel/View3D di inoltrare
modelli e viste a un backend nativo tipizzato. Test con provider software
sintetico e suite host passano (579/235 skipped). Non esiste ancora
un'implementazione Deko3D di NativeRenderer: l'app non la seleziona,
e texture/offscreen/riflessi devono ancora essere collegati. La demo
hardware rimane sul percorso Deko diretto; nessun nuovo upload per
questo passaggio frontend. Prossimo lavoro: adapter nativo concreto.


**Integrazione core Deko in corso:** driver SDL, contratto immagini e
allocatore ImageArray sono in `src/render/deko`. Confermati su Switch
(log `/private/tmp/deko-core-images-run.log`, 5.245 frame, uscita pulita).
Ora GpuModel e uploader nativo usano `render/model_geometry.*`; geometria
comune con policy di merge originale per produzione e packet-order per
Deko. Test host/ISO passano; questo nuovo percorso non è ancora stato
caricato sulla console. GpuContext/View3D non sono ancora collegati al
backend nativo. Conservare queste modifiche locali e distinguere gli
ultimi test hardware dalla nuova implementazione CPU.


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
