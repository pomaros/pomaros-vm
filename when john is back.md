# Quand John revient

## Mandat

Reprendre les briques utiles de Macaros et AROS dans un dépôt autonome pour
exécuter AROS en VM sur macOS. L'invité suit l'ABI AArch64 native : x18 est
un registre temporaire utilisable par le compilateur. Le programme hôte suit
l'ABI macOS. Les choix d'implémentation sont délégués.

## Choix

- Dépôt `macaros-vm`, privé, séparé du Macaros hébergé existant.
- Reprise du chargeur UEFI MIT et des outils de bundle existants.
- Noyau AROS conservé dans son dépôt APL, branche isolée `codex-macaros-vm`,
  checkout `../aros-core-vm`. Aucun changement dans les builds partagés.
- QEMU `virt` + HVF pour qualifier le noyau sur les vrais cœurs du Mac.
- Application Swift/AppKit + Virtualization.framework pour le véhicule final.
- Les deux machines virtuelles ont des périphériques différents : un succès
  QEMU ne prouve pas le démarrage sous Virtualization.framework.

## Résultats mesurés

- Invité EL1 via Hypervisor.framework : x18 vaut 0x1234 avant un retour hôte,
  puis 0x1235 après reprise. Test exécuté et réussi sur ce Mac.
- Lanceur Virtualization.framework : compilation réussie, signature ad hoc.
- Premier démarrage AROS/HVF : chargeur UEFI et bundle vérifiés, puis exception
  sur l'instruction de semihosting utilisée par les tests QEMU.
- Adaptation en cours : PL011 découvert par FDT, console sans semihosting,
  suppression de `-ffixed-x18` du bootstrap de la branche VM.

Les résultats de boot et les limites seront complétés après les essais.

## Premier démarrage complet sous virtualisation matérielle

- HVF : contrat complet de démarrage AROS et Wanderer PASS, 64 modules.
- Capture QMP vérifiée automatiquement : 800 × 600, 466812 pixels de bureau
  sur 480000, sans requester. Artefacts `artifacts/hvf-verified/`.
- x18 est effectivement utilisé par le compilateur dans `stage2_loader.o`.
- Test x18 avec contrôle négatif : retirer son initialisation fait échouer
  le vérificateur. Six cas FDT, ASan/UBSan : PASS.
- Virtualization.framework : le firmware démarre le même chargeur UEFI et
  vérifie le bundle. Il fournit ACPI, pas de FDT. Tables mesurées : FACP,
  GTDT, APIC et MCFG ; console virtio PCI, sans fonction emergency-write.
  Son adaptation est un travail distinct du démarrage HVF validé.
- Le bureau validé est le scénario de qualification borné. L'interactivité
  permanente, les interruptions GIC et les périphériques virtio restent à
  intégrer avant de présenter cela comme un remplacement du Macaros livré.

## Adaptation Virtualization.framework en cours

Le firmware EFI fourni par Apple ne publie ni FDT, ni protocole GOP dans cette
configuration. Le chargeur construit un FDT de passage depuis ACPI et les
capabilités PCI réellement observées. Un bundle sans écran de 22 modules permet
de qualifier le noyau sans prétendre que virtio GPU est déjà intégré.

Le transport de console virtio accepte la négociation VERSION_1, les adresses
DMA et DRIVER_OK. La première transmission n'est pas encore consommée : le
compteur used reste à zéro. Les registres et journaux sont conservés dans
`artifacts/vz-console-*`. Les fichiers de cette adaptation restent en travail ;
le jalon poussé `221f170` / core `be48add44f` est le boot HVF validé.

## Deuxième jalon : AROS démarre dans Virtualization.framework

La console est débloquée. Le périphérique moderne annonce MULTIPORT : le pilote
négocie cette fonction et échange DEVICE_READY, PORT_READY et PORT_OPEN sur ses
files de contrôle. Les essais de cache n'étaient pas la correction ; ils ont
été retirés. Le canal est ouvert avant les premières transmissions.

- `artifacts/vz-qualified-final/verdict.json` : PASS, boot AROS sans écran,
  22 modules, environ 1,1 seconde jusqu'au contrat complet.
- Aucun appel au pilote console depuis le firmware n'est nécessaire : son
  initialisation se fait dans AROS après ExitBootServices.
- `artifacts/hvf-qualified-final/` : Wanderer et image du bureau PASS après
  les mêmes modifications du noyau.
- Image volontairement corrompue : refus par le chargeur, verdict FAIL attendu,
  contrôle négatif PASS. Un simple lancement réussi de la VM ne valide rien.
- Tests de parsing ACPI/PCI : 9 cas ASan/UBSan. Les en-têtes partagés et le
  schéma d'allocation sont vérifiés automatiquement.
- Le lanceur s'arrête proprement sur SIGTERM ; les tests travaillent sur des
  copies privées de l'image et de la NVRAM.

Prochaine intégration : virtio GPU pour l'affichage VZ, puis les interruptions
GIC et l'entrée clavier/souris. Le mode VZ qualifié reste sans écran ; le bureau
qualifié visuellement reste le mode QEMU/HVF. Les deux sont de vraies VM.

## Revue et état livré

- La revue a demandé de limiter x18 libre au profil VM : corrigé dans le core
  `8a61e1eba2`, avec `AROS_VM_GUEST=1`. Le profil physique conserve son ABI.
- L'attente PL011 est bornée depuis `d913c3d2ca` ; un test injecte une FIFO
  bloquée et vérifie que le chargeur rend la main, puis teste une émission.
- Code VZ sans diagnostics temporaires de capture ou de forçage ACPI ;
  libération des allocations sur les chemins d'échec de découverte.
- Dernier test VZ : `artifacts/vz-reviewed/`, PASS, environ une seconde.
- L'essai Wanderer avec un module AFS supplémentaire échoue au stage-two :
  le profil documenté conserve les 64 modules déjà qualifiés. Ce n'est pas
  une qualification de montage de disque AFS sous VZ.

Le dépôt a une base exécutable de VM et deux chemins de qualification. Il ne
livre pas encore la parité du Macaros hébergé : affichage virtio GPU sous VZ,
interruptions GIC, entrées clavier/souris, stockage invité et bureau permanent
restent à intégrer et qualifier. Les composants du bundle de référence sont
réutilisés ; seul le bootstrap et le noyau modifiés sont reconstruits dans
le build isolé. Le build complet depuis un clone neuf reste à automatiser.

## Reprise guidée par le plan commun AArch64

John a exécuté le premier jalon et confirmé qu'il fonctionne. Sa consigne pour
la suite : réutilisation sur bare metal et Raspberry Pi, décisions autonomes,
respect du plan et de la roadmap. J'ai relu AARCH64, le track aarch64-generic,
la spec device-tree et ROADMAP dans aros-next. L'ordre retenu est lecteur FDT,
interruptions/timer/console, puis virtio et bureau interactif.

### Ce qui a été réutilisé et livré

- Fusion de l'origin Apple courant dans la branche VM isolée, dont le lancement
  de Wanderer choisi par Startup-Sequence. Aucun changement au checkout natif.
- `libfdt` 1.7.2 sous BSD, avec provenance et licence conservées. Adaptateur
  sans allocation dans `arch/aarch64-all/boot/fdt`, réutilisé par le boot VM.
- Adresses de bus traduites par `ranges`, alias de console, état disabled hérité,
  cellules reg et fournisseurs d'interruptions. Les cas hors périmètre, dont
  adresses PCI à trois cellules et nexus interrupt-map, sont refusés.
- Le chargeur EFI VZ utilise le constructeur libfdt borné. Un tampon trop petit
  échoue sans écrire au-delà. Le choix de console passe par `/chosen`.
- GICv2 extrait des opérations BCM2711 existantes et GICv3 ajouté avec bases
  FDT, sélection du redistributor par MPIDR et attentes bornées. Core commits :
  `bc273b6ac5` (lecteur), `6166339d7f` (GIC), `1669b1a090` (intégration VM).

### Mesures et limites

- Corpus hôte ASan/UBSan : Pi4 325 nœuds / 116 entrées reg, J313 324 / 233,
  plus arbre synthétique à bus imbriqués. Comparaison avec fdtget ; chaque
  longueur tronquée est refusée et les mutations sont parcourues sans accès
  hors tampon. `artifacts/portable-fdt/corpus.json`.
- Même binaire invité sur virt/HVF (GICv3) et raspi4b/TCG (GICv2) : découverte,
  128 ticks CNTV, une self-SGI, attente WFI, restauration de x18 après que le
  gestionnaire l'a volontairement écrasé. `artifacts/gic-hvf-x18` et
  `artifacts/gic-rpi4-x18`. Ce sont des probes avant noyau, pas un boot complet
  AROS sur Pi et pas une preuve sur Pi physique.
- Contrôles négatifs : faux numéro d'IRQ = timeout et registres QMP conservés ;
  instruction de restauration x18 remplacée par NOP = échec explicite.
  `artifacts/gic-controls/verdict.json`.
- Boot complet AROS VZ sans écran et Wanderer HVF restent PASS après le nouveau
  lecteur et la fusion : `artifacts/vz-generic-fdt`, `artifacts/hvf-generic-fdt`.
- CNTP_TVAL est indéfini sous QEMU/HVF : exception capturée avec PC/ESR, puis
  utilisation explicite du timer virtuel CNTV, IRQ résolue depuis l'arbre.
- QEMU raspi4b modifie l'arbre et y laisse des FDT_NOP légaux, que `fdtget -l`
  refuse. Le vérificateur conserve le blob exact et le normalise avec dtc pour
  les requêtes de référence. La console PL011 est sélectionnée dans une copie
  de l'arbre pour ce scénario, et cette modification est enregistrée.

La suite est l'intégration de ce contrôleur et du timer dans le noyau VM, avec
les bindings et la description VZ correspondants, puis virtio GPU. Le pilote
GIC initialise un seul CPU ; la découverte de plusieurs CPU ne signifie pas
que SMP est prêt. Les gates DT0/DT2 de la roadmap complète restent distincts de
ces preuves de composants. Le build depuis un clone neuf et le bureau permanent
restent du travail explicite, pas des capacités déjà livrées.

## x18 dans le vrai ordonnanceur VM

Le passage suivant a révélé une limite des premières preuves : elles couvraient
le bootstrap et les interruptions, mais le dispatcher Apple remplaçait encore
x18 par le pointeur de tâche. Le core `39446191f2` corrige le profil VM :

- Sauvegarde/restauration de x18 avec tous les autres registres de tâche.
- Diagnostic d'exception via `SysBase->ThisTask`, sans traiter x18 arbitraire
  comme une adresse de tâche. Ce chemin reste mono-CPU ; TPIDR_EL1/per-CPU est
  un travail ultérieur du plan commun.
- Preuve existante du boot et de deux tâches enrichie de trois valeurs x18
  distinctes, vérifiées après six commutations. Le profil physique garde son
  ABI et sa preuve d'ancre ; aucun run M1 physique effectué ici.

Contrôle avant/après : avec la nouvelle preuve et l'ancien dispatcher,
`artifacts/x18-dispatch-negative` échoue en phase 10, retour 0x24, statut de
preuve nul malgré six dispatches et les deux nettoyages. Après correction :
`artifacts/x18-dispatch-vz` PASS (0,66 s), `artifacts/x18-dispatch-hvf` PASS
(6,97 s, Wanderer 800x600). Bundle headless SHA256
`010543be74a31168300e81187473038127dff5fcc9756ce3663da63c06ca65f3` ;
bundle Wanderer `8aecb185eac3236bebe6f88d12c34cccf0f7a2476cc1a77eee20e766c97d6bd2`.
Le bundle reconstruit après nettoyage du code est identique octet pour octet.
`make test` passe également (HVF x18 et contrôle négatif, lecteurs ASan/UBSan,
11 cas du pont VZ, synchronisation des en-têtes).

La revue 197 du lecteur/GIC est approuvée par claude-next. Ses précisions non
bloquantes sur fournisseurs implicites, IRQ spéciales et groupes GIC sont dans
le core `5d010c5112`. Le grand jalon suivant reste le raccordement IRQ/timer
au noyau VM, puis le bureau interactif ; les probes ne le remplacent pas.

## Objectif actif : atteindre l’étape 6

John demande de poursuivre jusqu’à la qualification et à l’intégration d’usage :
IRQ/timer dans le noyau, description matérielle portable, bureau VZ et entrées,
stockage persistant, build complet reproductible, puis tests prolongés, réseau,
presse-papiers et échanges de fichiers. Chaque étape exige ses artefacts et
ses verdicts ; les preuves sur modèle restent séparées du matériel physique.

Le SMP est réservé à bsek, qui doit publier du code prochainement. Ne pas
développer de SMP en parallèle ; ne rouvrir ce sujet que s’il bloque concrètement
la progression et commencer par examiner/réutiliser son travail. La VM reste
mono-CPU pour les prochaines qualifications.

La revue x18 198 est approuvée. Sa demande de fusion du correctif de masques
d’interruptions `fbbe4c1596` est traitée : merge core `97125633aa`. Les preuves
x18 et le démarrage restent PASS sous VZ et HVF après reconstruction, dans
`artifacts/readiness-merged-vz` et `artifacts/readiness-merged-hvf`. Cela prépare
le raccordement IRQ sans prétendre que le timer VM est déjà intégré au noyau.

## IRQ/timer raccordés au vrai noyau sous QEMU/HVF

- Le stage-two mappe les deux fenêtres GIC depuis le FDT, refuse les
  chevauchements RAM/console, contrôleurs ambigus et fenêtres mal alignées.
  Le tri des fenêtres ne change plus l’identité de la console. Commit mapping
  core `f134d83a07`, 15 cas ASan/UBSan.
- Core `07c36e8679` : adaptateur VM mono-CPU, timer CNTV résolu via le fournisseur d’interruptions
  du FDT, gestion de la PPI par GIC, EOI avant sortie vers l’ordonnanceur.
  Réutilise le moteur deadline et timer.device existants. Les SPIs héritées du
  firmware sont masquées avant l’ouverture de la ligne IRQ. Les IRQ de pilotes
  périphériques ne sont pas encore raccordées à cet adaptateur.
- Preuve dans le démarrage réel : trois DoIO(TR_ADDREQUEST) de 20 ms, au moins
  trois expirations noyau et au moins 60 ms écoulées, puis démarrage complet et
  bureau Wanderer. PASS sur GICv2 et GICv3 : `artifacts/kernel-irq-gic2` et
  `artifacts/kernel-irq-gic3`. Bundle SHA256
  `d55a86c18476ab7b3aefde0bdb8cc1281a19defc479fede41c8bfc4f63f7c4f1`.
- Contrôle négatif : noyau identique sauf la PPI activée XOR 2. Initialisation
  atteinte puis attente bloquée, capture QMP des registres avant terminaison.
  `artifacts/kernel-timer-enabled-ppi-control/verdict.json` PASS. Le noyau
  positif reconstruit après retrait de la mutation est identique octet pour octet.
- Première tentative négative en changeant le FDT firmware : non concluante
  pour AROS, car EFI bloque lui-même sur son timer. Capture conservée dans
  `artifacts/kernel-timer-control/negative`. Pas présentée comme preuve noyau.
- Probes portables réexécutées après masquage des SPIs : virt/HVF, Pi4/TCG et
  leurs deux contrôles négatifs PASS (`artifacts/platform-irq-quarantine`).
- VZ démarre toujours (`artifacts/kernel-irq-vz-regression`) mais reste sans
  deadline IRQ tant que le pont ACPI ne décrit pas GIC/timer. C’est la prochaine
  étape, avant GPU/entrées/persistance. Aucun travail SMP, aucun run physique.

`make test` passe après ce raccordement (15 cas MMIO, corpus FDT sous
sanitizers, 11 cas VZ et tests x18).

## VZ : interruptions et timer actifs

Le pont EFI lit MADT/GTDT, valide les enregistrements puis décrit GICv3 et
les timers non sécurisés dans le FDT. VZ ne décrit pas de timer sécurisé ;
les interruptions sont nommées phys/virt, et le noyau cherche désormais virt
par nom quand cette liste existe. Le lecteur partagé refuse noms mal formés
et cible dupliquée. Aucun numéro de PPI VZ n’est codé en dur.

`artifacts/vz-irq-required/verdict.json` PASS, timer.device trois attentes
compris, disque SHA256
`2382a315a9044e7e3dedcd803944a7ae3b574f188ae02ce1252418a40bcebb0b`.
Le runner VZ exige maintenant ces marqueurs en plus de tout le démarrage.
HVF Wanderer + timer reste PASS dans `artifacts/hvf-named-irq`. Tests ACPI
(MADT/GTDT/XSDT, erreurs de longueur, checksum, adresses/flags, doublons)
sous ASan/UBSan et lecteur FDT nommé (y compris noms mal formés/dupliqués) PASS.

La revue 199 est approuvée. Deux améliorations demandées restent à intégrer
avec les ressources IRQ : rendre explicite le contrat des fenêtres mappées
au noyau et journaliser le statut précis d’un timer défaillant. La carte de
rôles est actuellement locale au stage-two, pas encore un objet transmis
au noyau ; il faut compléter ce contrat avant d’étendre aux IRQ de pilotes.

Prochaine étape : IRQ des périphériques et virtio GPU/entrées sous VZ. Le
bureau VZ, le stockage invité persistant, le build depuis clone neuf et la
qualification d’usage de l’étape 6 restent ouverts. SMP toujours réservé à bsek.

Versions de ce jalon : core `f1c2cc8701` puis `fbcc2c6c06` (parcours des
noms linéaire, sans recherche répétée), VM `dc94ce6`. Dernière image testée :
`build/vz-irq-final.img`, verdict `artifacts/vz-irq-final` PASS, disque SHA256
`2d49c21169523e302af54cc234ff3d02f1b650816527011576c71ba0223c0b4f`.
Le corpus synthétique nommé final (21 nœuds) passe ASan/UBSan après ce dernier
changement. La validation matérielle physique et le SMP n’ont pas été élargis.

## Contrat MMIO explicite et nettoyage du build VM

Les remarques de revue 199 sont traitées dans le core `49955f33f1` : quatre
bornes GIC sont transmises dans les tags protégés du démarrage, validées
comme groupe complet, puis comparées aux deux fenêtres du FDT avant tout
accès GIC. Une erreur du moteur timer imprime son statut avant refus.
Les tests ASan/UBSan couvrent émission/capacité, absence, doublon et chevauchement.
Le négatif `artifacts/vz-missing-contract` réutilise l’ancien stage-two avec
le noyau corrigé : refus explicite « GIC mapping contract » avant initialisation.

Une limite du build copié a aussi été découverte : son target.cfg contenait
encore -ffixed-x18 pour des fichiers noyau, malgré l’ABI du dispatcher corrigée.
Le helper normalise désormais cette configuration du build VM isolé ; le code
noyau reconstruit utilise effectivement x18 (désassemblage de coldstart_proof.o).
Cela ne prouve pas une reconstruction complète : les modules du bundle et
objets non reconstruits restent issus de la base native. Le build propre de
tous les composants reste nécessaire et ouvert.

Le correctif partagé `03cbb52a0e` de claude-next est fusionné comme
`5491c7c324` : reprise d’une tâche selon son tc_IDNestCnt avant readiness.
Après cette fusion, timer, x18 et démarrage VZ/HVF PASS :
`artifacts/vz-contract-idnest`, `artifacts/hvf-contract-idnest`. Disque VZ SHA256
`5785137abb6f52dfdcedb7af783c340675f67f50d6f1df420514264686a2a9a8`.
Pas de preuve spécifique Disable/Wait ajoutée ici ; elle reste avec le
propriétaire du correctif. Prochaine étape concrète : hooks IRQ des pilotes
puis virtio GPU/entrées. Le chantier SMP n’a pas été touché.

## Chaîne de handlers IRQ utilisable par les pilotes

Core `42fae09f39` raccorde les API AROS existantes au GIC VM : configuration
explicite par KrnModifyIRQA, ajout de handlers, distribution des deux arguments
aux callbacks et désactivation au retrait du dernier handler. Une source non
configurée ne renvoie plus de faux handle valide. Les PPIs du noyau, polarités
non prises en charge, listes de tags cycliques et changements avec handlers
installés sont refusés. Ce code reste mono-CPU.

La preuve active une SPI par son registre pending : deux callbacks, retrait
du premier puis seconde livraison au seul survivant, retrait du dernier et
vérification du bit matériel de masquage. PASS VZ et HVF GICv2/GICv3 :
`artifacts/vz-irq-spi`, `artifacts/hvf-irq-spi-gic2`, `artifacts/hvf-irq-spi-gic3`.
Disque VZ SHA256 `f8366a5595f7dff6116f0c118034e46fb732c37499be4e3fd0c763d999072904`.
Le test initial par SGI échouait sur GICv2, dont les SGIs peuvent être activées
en permanence ; le test a été corrigé pour mesurer une source désactivable.

Contrôle négatif : suppression du seul appel de distribution des callbacks,
timer toujours PASS puis refus explicite IRQ handler chain dans
`artifacts/vz-irq-spi-negative`. Source restaurée et noyau reconstruit identique
octet pour octet, SHA256 `82c380179f0f08357850d72092dd722e0c3559e017a92b742f6e45dc4b741d3e`.
Résumé `artifacts/irq-spi-control.json`. `make test` PASS. Les runners peuvent
exiger cette preuve (obligatoire VZ, option --require-irq-chain sous QEMU).

Limite : pas encore d’IRQ provenant d’un périphérique, de MSI ni de preuve DMA.
La revue 201 approuve le contrat GIC précédent. Prochain travail : intégrer
la règle de masques de retour de claude-next, puis PCI et virtio. Le transport
PCI moderne de `rom/devs/virtio` existe et sera réutilisé ; le pilote ECAM du
port RISC-V fournit aussi une référence APL, avec ses dépendances à isoler.

## Retours de tâches et idle : correctifs partagés

Le merge `e89c648944` prend les changements de claude-next jusqu’à `67b5ca3f0d`.
La qualification VM a révélé deux problèmes corrigés séparément :

- `ceee139ac0` : le dispatcher peut remplacer un frame EL0 par un retour idle
  EL1h sans changer le numéro de vecteur. Appliquer ensuite le nesting de la
  tâche endormie coupe les interruptions idle. La règle de masques vérifie
  désormais le mode de retour réel. Avant correction, timeout VZ et HVF ;
  `artifacts/hvf-task-masks/registers.txt` capture l’attente EL1h à 0x3c5.
- `b3fcb7508b` : la nouvelle preuve lisait le snapshot du dispatcher, avant le
  dernier Enable interne de Wait. Elle lit maintenant le SPSR du véritable
  appelant au retour de DoIO, via Supervisor, et vérifie les erreurs DoIO.
  L’attendu suit les lignes prêtes : IRQ pour VM, FIQ pour Apple physique.

PASS final VZ (`artifacts/vz-return-mask-required`) et HVF GICv2/GICv3
(`artifacts/hvf-return-mask-gic2`, `artifacts/hvf-return-masks`) : disabled 0xc0,
plain 0x40, timer, chaîne IRQ, x18 et démarrage complet. Disque VZ SHA256
`663128090d2927db12d2f556d54cbc07157a1a8dd8b4a868f389b65b531f3df5`.
Le runner VZ et l’option QEMU --require-timer exigent cette preuve de masques.
`make test` PASS. Revues 202 (chaîne IRQ) et 203 (retour idle/preuve) demandées ;
les correctifs ont été transmis à claude-next. Pas de validation physique ici.

## Reconstruction dans un répertoire neuf

Le configure possède un profil explicite --enable-vm-guest, exporté aux
makefiles et sans réservation x18 dans les flags C/C++. Le helper
`tools/configure-core.py` refuse un répertoire existant et utilise la toolchain
croisée installée. Contrôles réels de configure PASS : profil VM sans fixed-x18,
profil natif par défaut avec fixed-x18, valeur invalide refusée ; preuves dans
`artifacts/configure-profile-control`.

La reconstruction est lancée dans `~/Build/macaros-vm/clean-core`, sans objets
copiés, cible apple-boot-payloads. Elle n’est pas encore qualifiée comme un
build complet. La première tentative s’est arrêtée parce que le script
configure avait été régénéré après sa configuration ; config.status --recheck
puis config.status ont resynchronisé le build. Journal actif :
`/tmp/vm-clean-build-retry.log`. Il faut finir ce build, reconstruire le bureau,
composer les bundles à partir de ces seuls produits puis refaire VZ/HVF.

## Le noyau et les modules de base démarrent depuis le build neuf

Profil configure core `d7df1bc0dd`, dépendances des en-têtes corrigées dans
`7d0e432ecb` puis alignées sur les huit ressources de claude-next dans
`885b09650d`. Le premier build révélait l’absence de proto/smc.h ; le suivant
révélait les sous-modules de catalogues non initialisés. Le helper configure
initialise leurs révisions épinglées ; aucune constante de traduction ni aucun
ancien fichier généré n’a été recopié pour contourner le problème.

Core `0df759723e` évite aussi de reprogrammer le trigger/routage d’une IRQ
déjà active quand un second handler est ajouté. Il documente les retours 0/1
de KrnModifyIRQA. Revues 202, 203 et 204 approuvées ; la séparation des probes
SPI 32 et du futur profil de production reste à faire avant une release.

Les 22 entrées du bundle de base, la commande ABI, le SYS FAT12 et le chargeur
EFI proviennent de cette reconstruction. Le packager ne lit aucun bundle seed.
PASS VZ et HVF GICv2/GICv3 : `artifacts/vz-fresh-core`,
`artifacts/hvf-fresh-core`, `artifacts/hvf-fresh-core-gic2`. Tests de masques,
timer, chaîne IRQ, x18, DOS et ABI inclus ; aucun bureau revendiqué par ces runs.
Bundle SHA256 `b18cb16c83fae022d26f7c4b2cbb069b272ecccb5babade14e73a4c7517d00d5` ;
disque VZ SHA256 `a21c48d81882ff079c52a9917b4ca2a1e6479e31f3aba59f70773f3f9a9bba31`.

La seconde composition du bundle est identique octet pour octet et l’EFI
reconstruit est identique au précédent. Cela ne prouve pas encore deux builds
complets indépendants identiques. Journaux, révisions de catalogues et hashes
des entrées : `artifacts/clean-core-build`. Les outils FAT/ABI sont réutilisés
avec provenance MIT enregistrée ; le constructeur ABI n’impose plus fixed-x18.

La reconstruction du bureau est lancée avec apple-wanderer-payloads dans le
même build neuf, log `/tmp/vm-clean-desktop-build.log`. Il reste à la terminer,
à composer/qualifier le bundle Wanderer frais, puis à poursuivre PCI/virtio
pour le bureau VZ, le stockage, le réseau et les échanges. L’étape 6 reste
ouverte, le SMP reste réservé à bsek.

## Bureau issu de la reconstruction complète

apple-wanderer-payloads terminé dans clean-core, sans objets natifs copiés.
Le profil --profile wanderer du packager prend 62 modules, le stage two et
le SYS fraîchement généré : 64 entrées, Wanderer et ABIPROBE issus du SDK neuf.
FAT vérifié après composition. Bundle SHA256
127b24de02821026345a8f87ee20194587ed2f501d307a2627ab827be906c3a3.
PASS HVF GICv3 et GICv2 avec timer, masques, IRQ, x18 et contrôle pixels
Wanderer : artifacts/hvf-fresh-wanderer et artifacts/hvf-fresh-wanderer-gic2.
Ce bureau utilise le framebuffer QEMU ; le GPU VZ attend son pilote.
La prochaine étape est la description PCI et les mappings MMIO/DMA explicites,
puis les périphériques virtio. Aucun travail SMP engagé.

## PCI décrit par le firmware, anomalie VZ conservée ouverte

Le pont EFI transmet désormais les fenêtres allouées par Configuration() du
root bridge, avec bus-range et ECAM. VZ annonce deux fenêtres de 1 Mio :
0x50000000 et 0x100000000. Aucune adresse de fenêtre n'est figée dans le code.
Le parseur refuse troncatures, translations non prises en charge et
chevauchements ; tests ASan/UBSan. Cette description ne prouve pas encore le
mapping par le noyau ni les IRQ/DMA des périphériques.

Premier run artifacts/vz-pci-root PASS. Avec journal des fenêtres ajouté,
artifacts/vz-pci-root-2 finit en timeout après un caractère « [ » suivant les
messages Exec, sans preuve permettant de distinguer perte de console et arrêt
CPU. Le même disque passe ensuite cinq fois : artifacts/vz-pci-repeat-0..4,
4cdd07c1f0050c9a0f5b1441dbeb094e95c45a39ce09fb3e1f1c475915c6ca33.
HVF avec le même EFI passe (artifacts/hvf-pci-root), mais utilise PL011 plutôt
que la console virtio VZ. L'anomalie intermittente reste ouverte.

Tentatives de capture indépendantes archivées : le stub GDB privé VZ refuse
le démarrage (VZErrorDomain Code=1, artifacts/vz-pci-supervised) ; la sauvegarde
publique, bien que validateSaveRestoreSupport réussisse, refuse save avec
permission denied (artifacts/vz-capture-control-running). Ces essais ont été
retirés du lanceur ; aucune modification de SIP ni d'entitlements privés.
Le prochain diagnostic doit reproduire la console virtio sous HVF supervisé
ou obtenir un canal indépendant VZ ; cinq PASS ne résolvent pas ce timeout.

Pont PCI final : validation supplémentaire des chevauchements ECAM/GIC,
make test PASS, VZ artifacts/vz-pci-final PASS ; disque SHA256
98a21eac43c02a18e8aade4264bc571f9c0a1b29d5ecb4feca6a8c96737121f3.

## Console virtio sous superviseur HVF

Le runner --console virtio active ACPI et un contrôleur PCI moderne, avec le
même EFI, le même pont FDT et le même code de console que VZ. Démarrage complet
PASS dans artifacts/hvf-virtio-console, avec toutes les preuves noyau. Les
fenêtres PCI viennent aussi du firmware QEMU, avec des adresses différentes.
Contrôle de capture indépendant : timeout volontaire 0,7 s dans
artifacts/hvf-virtio-console-capture-control, FAIL attendu et registers.txt
présent (PC firmware 0x5fd5de04). Ce contrôle valide l'accès superviseur avant
la panne ; il ne reproduit pas le timeout VZ ni ne donne son instruction fautive.

## Fenêtres PCI mappées par la MMU invitée

Core a832c8dc8d : binding ECAM partagé dans arch/aarch64-all/boot/fdt,
réutilisé par le plan MMIO du stage two. Les contrôles portent sur cellules,
bus, taille ECAM, limites 48 bits, chevauchements RAM/GIC, alignements et
chevauchements de fenêtres. Les translations PCI/CPU sont conservées ; pas
de mapping I/O ports ni de contrat DMA ajouté implicitement.
30 cas ASan/UBSan PASS dans tests/virt-fdt.py. VZ PASS
artifacts/vz-pci-mapped (disque e0f7df6b7af5dc2a75659efa3ab518d4c244fe1cfa940f9afc8f47a00c3a08f4),
HVF ACPI/console virtio PASS artifacts/hvf-pci-mapped,
HVF FDT/GICv2 PASS artifacts/hvf-pci-mapped-fdt-gic2.
Bundle f67f812258d7721f2d6549962fd0f74b936f44cc76f49528028c12b7d2be8667.
Cela prouve le démarrage avec ces mappings et la console PCI existante ;
pas encore l'énumération par pci.hidd ou les transferts disque/GPU.

L'étape suivante est l'exposition du FDT retenu à openfirmware.resource puis
l'adaptateur ECAM de pci.hidd. Le code RISC-V pcidt fournit déjà le modèle de
classes réutilisable, mais ses hypothèses Sv39 et PLIC ne doivent pas migrer
sur AArch64. Aucun travail SMP ; cette étape reste mono-CPU.


## OpenFirmware utilisable dans l’invité — 30 septembre

Core `3dfe4b14a3`, `cca1aaf1a9`, `a662121517` : construction de l’API
OpenFirmware existante à partir du FDT conservé, profil résident VM et
propagation explicite du profil VM au stage2 autonome. Le build normal inclut
maintenant la ressource. Aucun changement SMP.

Preuves : `vz-openfirmware-qualified`, `hvf-openfirmware-qualified` (ACPI et
console virtio), `hvf-openfirmware-fdt-gic2` (tree QEMU et PL011) passent le
contrat de démarrage, timer, chaîne IRQ, ABI et recherche OpenFirmware suivie
d’une lecture des identifiants PCI. Bundle
`97c5c5aae48389698ca78192ee9af0dee03b482c41aa72ec8a35e24994fcfdc6`.
Le test hôte ASan/UBSan vérifie 21 nœuds et les 72 échecs d’allocation possibles,
sans fuite. La métadonnée est allouée, les noms/valeurs empruntent le blob
immuable conservé. Le chemin historique Pi n’est pas supprimé.

Le premier probe lisait ECAM depuis EL0 : faute de permission attendue observée
sous VZ (`vz-openfirmware`, ESR 0x9200000f, FAR 0x40000000). Le probe final utilise
Supervisor, sans ouvrir les mappings aux applications. Le profil résident a
ensuite révélé un contrôle stage2 périmé puis l’absence du define VM dans ses
flags propres ; corrigés, sans désactiver le contrôle d’ordre.

Un essai HVF du probe négatif a aussi perdu sa sortie virtio après `[`. QMP a
capturé le PC dans la boucle WFE de sortie d’échec du stage2 (RETURN 0x25 dans
x20), pas dans une boucle de transmission. Cela donne une contre-épreuve utile
au silence VZ intermittent déjà conservé ; sa cause n’est pas encore établie.

Restent le PCI HIDD et l’admission des mappings/DMA/IRQ, puis les pilotes VZ
GPU et entrées. Ces lectures de configuration ne prouvent aucun transfert DMA,
aucune interruption de périphérique, aucun bureau VZ. L’étape 6 reste ouverte.


## Accès ECAM réutilisables — 30 septembre

Core `52cc57c5c9` ajoute `rom/hidds/pci/ecam/access.h`, sans dépendance Apple,
FDT, DMA ou SMP. Le mapping est fourni par la plateforme. Les adresses sont
vérifiées : bus relatif au premier bus, fonction, registre, largeur, alignement,
limites de mapping et débordement de pointeur. Les accès 8/16/32 bits sont
little-endian, avec stores de la largeur exacte. Le futur HIDD doit remplacer
les wrappers PCI par défaut qui font read-modify-write sur 32 bits pour éviter
de toucher les bits d’état write-one-to-clear adjacents au mot de commande.

Tests ASan/UBSan : limites et premier bus non nul, débordements, stores de
largeurs différentes. Ce modèle en RAM ne simule pas le W1C ; un test de code
AArch64 produit vérifie séparément une seule transaction strb/strh/str-w.
`vz-ecam-access` et `hvf-ecam-access` passent : découverte par OpenFirmware,
lectures des IDs à trois largeurs et réécriture inchangée du mot de commande.
Bundle `0173f51021ea0b17cf03afb90b9c0741ea977bc26766c8a24809e2237f4a324c`.
Ce probe tourne sous Supervisor et ne change ni le bus mastering ni les BARs.

Le HIDD n’est pas encore raccordé. Ne pas utiliser SuperState/UserState comme
raccourci sur ce port : le passage EL0 SuperState y est explicitement non
implémenté. Il faut une admission de mapping explicite ou une opération
Supervisor sérialisée. La découverte commune doit réutiliser OpenFirmware,
les classes PCI et les translations validées, sans importer les hypothèses
PLIC/Sv39/DesignWare du pilote RISC-V. Revue OpenFirmware #208 approuvée.


## Classe PCI ECAM — 30 septembre

Core `578315e343` ajoute le véritable module OOP `pciecam.hidd`, sous
`rom/hidds/pci/ecam`. Il dérive de Hidd_PCIDriver et garde la classe générique
AROS de périphérique PCI. Les six méthodes de configuration utilisent les
accès ECAM qualifiés. Le constructeur copie un descriptif de mappings déjà
admis par la plateforme ; il vérifie les plages et recouvrements. `MapPCI`
traduit une plage entière, sans dépassement. Les slots racine réservés à la
plateforme sont invisibles aux lectures et protégés des écritures, pour ne pas
reprendre la console active. La classe de base PCI a aussi reçu une vérification
NULL après l’allocation de son instance.

Le contrat impose un callback plateforme de complétion des écritures de
configuration avant accès MMIO dans une autre région, suite à la note de revue
#209 (approuvée). Le pilote ne reprend pas les allocations DMA par défaut ni
l’hypothèse d’une interruption INTx ; ces opérations refusent en attendant leur
admission explicite. Le code partagé n’embarque aucune instruction Apple/SMP.

Le module compile pour AROS AArch64. `make test-pci-ecam-class` exécute les
méthodes réelles sous ASan/UBSan, avec seulement l’allocation OOP parente et le
lookup Utility remplacés côté hôte : réservation, écriture, traduction,
refus hors plage, échec d’allocation, barrière après store accepté. Cela ne
prouve pas encore son chargement ni l’énumération OOP dans l’invité. Prochaine
étape : fournir les mappings et réserver le BDF de console depuis le binding
VM, charger les classes et vérifier l’énumération réelle avant virtio GPU.
