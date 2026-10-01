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

La classe refuse aussi un premier bus non nul : le scanner PCI AROS commence
actuellement au bus zéro. Le helper ECAM reste capable d’adresser un premier
bus non nul, mais ne pas confondre cette propriété avec la capacité du scanner.


## Chargement réel des classes PCI — 30 septembre

Core `9d2b9f3c9a` / `ecceb24d1a` : le profil sans écran inclut maintenant
`lddemon.resource`, chargé après DOS par le mécanisme standard AROS. Son absence
expliquait l’échec initial de OpenLibrary(oop.library) malgré la présence du
fichier. Le profil possède 22 résidents (24 entrées bundle), avec contrôles
exacts OpenFirmware à l’indice 6 et LDDemon à 21. Les profils natifs gardent
leurs vérifications propres. `apple-boot-payloads` dépend du chargeur.

Le test optionnel PCIPROBE est exécuté après la mesure ABI de chargement /
déchargement ; ses bibliothèques restent résidentes, sans affaiblir la mesure
ABI précédente. Le packaging `--pci-hidd` inclut et hache oop.library,
hiddclass.hidd, pci.hidd et pciecam.hidd. OpenLibrary passe par LDDemon normal.
Le programme crée l’objet OOP, lit les IDs par la méthode du HIDD, vérifie que
la console réservée reste invisible, puis détruit l’objet avant son propre
UnLoadSeg (donc aucun callback dans du code libéré).

PASS `hvf-pci-hidd-loader` et `vz-pci-hidd-qualified`, avec
`pci_hidd_required=true`. Bundle
`9a80334b91bfc8cf629e024387c37fde45a97a76fd3639dd0c58835e38f69399` ; disque VZ
`0fff41e0139f60954d7f0aae96b9e289078de34d38b7d9cbeec2c3694c14441d`.
Contre-épreuve `vz-pci-hidd-negative` : sans PCIPROBE le démarrage AROS passe,
mais le runner exigeant ce test refuse bien le résultat.

Premier essai conservé `hvf-pci-hidd-load` : timeout avant main, PC indépendant
capturé par QMP. Il manquait les options CLI __nowbsupport/__nostdiowin du
programme de test ; corrigées, main est atteint. Les essais suivants ont
identifié le manque de LDDemon. Revue du HIDD #210 approuvée.

Limite : les lectures OOP se font encore sous Supervisor sur ECAM EL1-only.
Ce n’est pas encore l’enregistrement du pont avec HIDD_PCI_AddHardwareDriver.
Il reste à donner aux pilotes leurs mappings admis, réserver le BDF de console
et enregistrer le pont pour l’énumération complète, puis apporter DMA/IRQ et
virtio GPU/entrées/stockage. SMP intact, étape 6 toujours ouverte.


## Enregistrement dans le sous-système PCI — 30 septembre

PCIPROBE passe maintenant par HW_AddDriver sur l’objet PCI standard, puis par
HIDD_PCI_EnumDevices. Le nombre exact de fonctions virtio non réservées doit
correspondre à la lecture directe d’ECAM ; aucune console ne doit apparaître.
HW_RemoveDriver doit réussir et une seconde énumération doit être vide avant
le déchargement du programme et de son callback. Core `80c098ec78` conserve
le code d’un probe qui échoue jusqu’à l’arrêt du boot, au cas où un callback
serait encore enregistré.

Le test accorde temporairement l’accès EL0 au premier Mio ECAM, via
KrnSetProtection, et remet MAP_Supervisor après retrait. Il admet explicitement
le bus racine plat (0) uniquement. Aucun mapping BAR ni DMA n’est accordé.
Les opérations du scanner et des classes de périphériques tournent désormais
en EL0 normal. oop.library est ouvert par son nom et les HIDD par DEVS:Drivers,
sans chemin SYS fixé. Les avertissements PCI sur l’absence d’INTLine sont
attendus : aucune interruption de périphérique n’est prétendue opérationnelle.

PASS `hvf-pci-register-assigns` et `vz-pci-register-repeat`, mêmes entrées
bundle `ba73ec2a75866240f11c9da0b447ac6156af5daeceff3a4fe5ad027e226fa0a6`,
disque VZ `5c73dee04f07a6f282248f63f31045b45c5abbff66f1c25a472a2dc761dc08c5`.
L’essai précédent `vz-pci-register` est FAIL timeout : sortie perdue au milieu
de la ligne dispatch masks, avant PCIPROBE. Le même disque passe à la répétition
bornée ; cela ne résout pas le silence virtio intermittent. HVF dispose de la
capture QMP indépendante ; VZ n’a pas encore de capture PC exploitable.

`--require-pci-hidd` exige désormais aussi le marqueur d’énumération/retrait.
La contre-épreuve avec l’ancien disque qui charge seulement la classe doit
échouer malgré son startup PASS. Prochaine étape : transformer le raccordement
qualifié en initialisation persistante et apporter les fenêtres BAR, puis
DMA/IRQ/virtio GPU et entrées. Cette preuve temporaire ne constitue pas encore
le pilote utilisable au bureau. Revue #211 approuvée ; SMP inchangé.


## Registres virtio atteints via MapPCI — 30 septembre

Le probe copie les fenêtres `ranges` du tree déjà validé au boot, puis le
constructeur du HIDD vérifie leurs bornes/recouvrements avant l’ouverture des
permissions. Les fenêtres deviennent temporairement accessibles en EL0 pour
ce test, puis reviennent en MAP_Supervisor après retrait du pont.

Pour chaque device virtio non réservé, le test parcourt les capacités PCI avec
borne/alignement et détection de cycle, vérifie le BAR et la taille de sa
configuration commune, puis appelle HIDD_PCIDriver_MapPCI/UnmapPCI. La lecture
de num_queues passe en EL0 sur l’adresse CPU retournée. Sous HVF, le premier
résultat était 0xffff : le décodage MEM PCI était désactivé par le firmware.
Le test active uniquement MEM, garde le bit bus master inchangé, lit, puis
restaure exactement le mot de commande via un store 16 bits et sa barrière.
Aucun reset de device, programmation de queue ou transfert DMA.

PASS `hvf-pci-bars-decode` (3 et 1 queues) et `vz-pci-bars-decode` (1 et 1).
Bundle `ecc4543bfd76c9e5252111c9f53e775b4187a78f6fb8c1ec92fb34a7b56880e3` ;
disque VZ `200855bb0f032b34f0a0eca7fb6a2add7ac4b3afc9923f7fba6962f36b7d2f51`.
Les deux configurations ont des translations identité ; la translation non
identité reste couverte par le test hôte de la classe, pas par un guest réel.
Les runners exigent désormais aussi le marqueur MapPCI pour --require-pci-hidd.

Point découvert avant réutilisation : rom/devs/virtio/virtio_hw.c traite encore
le BAR comme un pointeur CPU et ne passe pas par MapPCI. Il faudra adapter ce
transport existant, avec validation des capacités et libération des mappings,
plutôt que recopier cette hypothèse dans le pilote VM. L’initialisation
persistante du pont, le DMA et les interruptions restent à réaliser. Le test
reste volontairement temporaire ; aucun bureau VZ ou stockage persistant n’est
revendiqué ici. SMP inchangé.


## Transport virtio existant adapté aux mappings PCI — 30 septembre

Core `13d1f199d5` : virtio_hw_map passe désormais par le HIDD propriétaire pour
MapPCI, au lieu de déréférencer les BARs. `pci_mapping.h` porte le parcours
borné des capacités, les validations d’extent/alignment, la traduction CPU
retournée et le rollback de tous les mappings sur échec. Les types connus
répétés gardent leur première occurrence ; les types inconnus sont ignorés.
Un remapping d’un objet déjà mappé est refusé. L’API VirtioPCIUnmap libère les
quatre régions en sens inverse et efface les pointeurs ; son appelant doit
avoir arrêté le device et ses utilisateurs.

La notification de queue vérifie maintenant le produit offset × multiplier
en 64 bits contre la taille de fenêtre. Les features bloc optionnelles dont
les champs dépasseraient la configuration device sont exclues de la négociation.
Le contrôleur active MEM decode avant de lire les registres (`c63447e439`).
Le premier build propre a révélé des headers Hidd_Bus/Storage absents : les
dépendances explicites sont ajoutées dans `30ecf31b19`.

ASan/UBSan : CPU différent de l’adresse PCI, borne BAR, cycle, capacité tronquée,
chaque échec de MapPCI avec unwind, double mapping, notification hors fenêtre
et multiplication débordant 32 bits. Le test fait partie de make test.
Le probe invité utilise maintenant exactement le même moteur partagé pour
mapper common/notify/ISR (device config optionnelle), lire num_queues et tout
libérer : `hvf-virtio-map-shared` et `vz-virtio-map-shared` PASS.
Bundle `af653d51a114fb623a8faa8ef08e0a4fdbf805712b179e18af8031147c9fdcc8` ;
disque VZ `da5982055035eedff364205cfeeefa976935ff96629371f27413bca9783197ef`.

Limite explicite : ce n’est pas encore l’exécution complète de virtio.device.
Son ancien contrôleur retourne encore un objet sur plusieurs échecs et libère
une queue au Dispose sans démontrer l’arrêt préalable du DMA ni retirer tous
les utilisateurs. Je n’y ai pas ajouté un Unmap aveugle : il faut corriger
cette transaction de démarrage/arrêt avant les vrais transferts, puis appeler
virtio_hw_unmap après quiescence. Le rollback interne des mappings, lui, est
qualifié. Pont permanent, DMA/IRQ et bureau VZ restent à faire ; SMP intact.


## Reset virtio acquitté et borne de timeout — 30 septembre

Core `e09a898bdb` : l'ancien `while (status != 0 && spin--)` pouvait
faire passer le compteur non signé de zéro à ULONG_MAX et accepter un reset
jamais acquitté. Le helper partagé `status.h` impose un nombre exact de
lectures, accepte la dernière lecture autorisée et refuse une réponse trop
tardive ou absente. `virtio_hw_stop` laisse status à zéro ; `virtio_hw_reset`
n'écrit ACK puis DRIVER qu'après cet acquittement. Les accès status sont
ordonnés (DSB SY sur AArch64). Le transport PCI lit aussi réellement un mot
lorsque width=2, conformément à la note de revue 214.

Contrat : [Virtio 1.2, sections 2.4 et 4.1.4.3.2](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html).
Un acquittement du reset arrête les interactions du périphérique avec les
queues ; il ne supprime ni les tâches du pilote ni ses callbacks logiciels.
L'API stop n'est donc pas encore branchée dans un Dispose qui libérerait
aveuglément la queue. Le rollback complet reste nécessaire.

`make test` PASS, dont le nouveau `make test-virtio-status` sous ASan/UBSan :
réponse immédiate/retardée/dernière lecture, refus permanent, réponse trop
tardive, refus de DRIVER, budget zéro. Le probe invité utilise le même helper
sur les périphériques non-console, sans soumettre de queue et sans DRIVER_OK,
puis les laisse arrêtés. La console reste réservée.
`artifacts/vz-virtio-reset` et `artifacts/hvf-virtio-reset` PASS ;
bundle `ba6082428fa6deab11f3a8ab59ecaf6b0bd7c283b7f68b5d5ce36df8de83b9fa`,
disque VZ `c574ebc224014de0cff8636e7fc8b28cbe84243ec4bccc1bc9925134ace705c2`.
Le nouveau marqueur est obligatoire avec --require-pci-hidd. Contrôle négatif
`artifacts/vz-virtio-reset-negative` : l'ancienne image qui passe boot et
MapPCI est refusée, faute de preuve de reset.

Suite concrète : RootNew doit échouer transactionnellement au lieu de
renvoyer un contrôleur incomplet ; l'échec d'AddInterrupt laisse aujourd'hui
une tâche IO vivante ; Dispose ne retire pas le callback de reset ; RemoveBus
refuse tout retrait. Il faut suivre la publication des unités et bootnodes,
retirer les utilisateurs puis arrêter le device avant de libérer DMA/MMIO.
La sélection moderne dans virtio_init.c doit aussi utiliser l'identifiant
PCI 0x1042 pour le bloc, plutôt que supposer un subsystem ID de type legacy.
Aucune qualification DMA, IRQ périphérique, stockage persistant ou bureau VZ
n'est ajoutée par ces tests de reset. Pas de modification SMP.

Build AROS complet `make -j8 kernel-virtio` PASS dans clean-core ; binaire
virtio.device SHA256 `526d85f61b7d0ef67ba4304b2b4aeff7b3896bbf34e20528c719cce25eaefcc4`.
Le binaire complet est compilé, mais le test invité exécute le helper partagé
via PCIPROBE et non le cycle complet de virtio.device.


## Démarrage virtio transactionnel — 30 septembre

Core `064fbbe712` corrige le raccordement au framework stockage : AddBus de
Hidd_StorageController remplace l'appel HW_AddDriver sur le contrôleur, qui
n'implémente pas cette interface. RootNew retourne NULL sur chaque échec et
ne publie le contrôleur ni son callback reset qu'après création du bus.
L'ancien nom du callback lisait le pointeur OOP comme un struct Node : remplacé
par un nom statique. Le nom du bus est statique aussi (plus d'allocation perdue).

Le démarrage du bus prépare l'unité et son ID avant l'IRQ, DRIVER_OK et la tâche
IO. Un échec avant création de la tâche ne laisse donc aucune tâche orpheline ;
un échec DRIVER_OK ou création de tâche retire l'IRQ. Dispose du bus non publié
libère unités, tableau, ID et chaînes. Les requêtes ne sont pas encore visibles
à ce stade. La tâche ne commence plus par effacer un signal éventuellement reçu.
L'IRQ tolère le court intervalle sans q_IOTask ; aucune requête n'est publiée.

Dispose du contrôleur non publié attend le reset avant FreePCIMem et UnmapPCI,
puis restaure le registre command PCI. Si le reset ne répond pas, les mappings,
les buffers et la propriété PCI restent réservés jusqu'à extinction : pas de
réattribution d'une mémoire potentiellement encore ciblée par le matériel.
Ce chemin est signalé par dev_Quarantined ; le probe PCI libère normalement son
descripteur et la propriété lorsqu'une création échoue proprement.

Limite assumée : le framework stockage ne sait pas retirer un bus publié et
virtio.device est noexpunge. Dispose conserve donc un bus publié avec ses
utilisateurs au lieu de libérer sa queue sous eux. Cela sécurise les échecs de
démarrage ; ce n'est pas une implémentation du retrait à chaud ni un arrêt
complet d'un service actif. La récupération d'une quarantaine reste à faire.
MakeDosNode/AddBootNode restent une publication de boot optionnelle : leur échec
ne retire pas une unité runtime créée avec succès ; le boot disque sera qualifié
séparément.

Autres corrections sur ce chemin : sélection moderne du bloc par PCI 0x1042,
au lieu d'exiger le subsystem ID legacy ; tableau DOS initialisé à zéro ; copie
des noms d'unité bornée par la fin de chaîne (l'ancien code lisait 40 octets même
pour un nom court). Le bus impose une seule unité, conformément au pilote.

`make test-virtio-lifecycle` compile les vrais fichiers controllerclass.c et
busclass.c avec des services hôte simulés, sous ASan/UBSan. PASS : mapping,
reset, features, allocation/installation queue, création bus, expansion,
unité, ID, IRQ, DRIVER_OK et worker en échec ; aucun callback/contrôleur publié
sur échec, ressources libérées seulement après reset, quarantaine conservée
sur refus, succès conservé sur Dispose. Ce test ne prouve ni l'ordonnancement
réel des tâches/IRQ ni les transferts DMA. Aucun nouveau résultat invité ou
matériel physique n'est revendiqué pour ce commit. SMP inchangé.

Build complet `make -j8 kernel-virtio` PASS dans clean-core après le build ciblé ;
SHA256 du pilote `e647b20dfbe79c333dff9da7ed1ff72f82ab12dba82115d46875e6b6469f4a57`.
Revue 216, tests hôte dans le commit VM `f61ba3d`.


## Pont PCI résident de la VM — 30 septembre

Core `2f8860c6c3` ajoute vmpci.hidd dans arch/aarch64-all/hidds/vmpci.
Il réutilise le HIDD ECAM générique et possède sa racine PCI, son instance,
les mappings et le callback DSB SY pour toute la durée de vie de la VM
(noexpunge). Il accepte seulement l'arbre firmware `aros,virtualization`,
limite l'accès ECAM au bus racine zéro et réserve le slot de la console virtio.
Les fenêtres viennent de l'API OpenFirmware ; la classe ECAM vérifie leur
étendue, alignement et absence de recouvrement avant admission EL0. Le stage2
avait déjà vérifié les apertures contre la RAM. Cette admission dépend de ce
profil VM et n'est pas une politique de mapping générique pour Apple ou Pi.
Les adresses matérielles ne sont pas codées en dur. DMA/IRQ restent refusés.

Le module ferme les dépendances et rétablit les permissions supervisor si
l'installation échoue après admission. Le callback permanent réside dans
vmpci.hidd, jamais dans PCIPROBE. Le chemin optionnel --pci-hidd effectue le
test temporaire existant, installe le module, ferme sa bibliothèque puis quitte.
Le noyau décharge ce premier PCIPROBE, en recharge une nouvelle instance avec
l'argument resident, et exige l'énumération exacte et une écriture/relecture
du mot command PCI (valeur inchangée) pour chaque périphérique non-console.
Ce deuxième programme ne recharge pas vmpci : une installation absente ne
peut donc pas être masquée par le test.

VZ `artifacts/vz-pci-resident` et HVF `artifacts/hvf-pci-resident` PASS ;
bundle `ec818b53665132fd2e8693826ba38b689ac12092fd5526cca4f0f9517a82aa5e`,
disque `d1badd1c57d8517b5bd2a7d08129afa80f1ea0b76039e3eb3850dca4987dbed7`.
vmpci.hidd SHA256 `808d59703dce49f88aeec5ce6cc5d136027d811a7ac9e919c630e2532758eafd`.
`make test` PASS ; build noyau/stage2 et module ciblé PASS.
Contrôle négatif `artifacts/vz-pci-resident-negative` : l'ancienne image avec
preuve de reset mais sans pont résident est refusée par les nouveaux marqueurs.

Cela qualifie le pont permanent dans le profil PCI optionnel. Le package par
défaut et le bureau VZ restent à raccorder. Prochaine limite : allocations DMA
avec contrat de traduction/cohérence, puis routage MSI et transferts virtio.
Aucune preuve de stockage persistant, GPU, entrée ni matériel physique ici.
La revue 216 du démarrage virtio est approuvée. SMP toujours réservé à bsek.

Vérification du build standard : après retrait du seul binaire vmpci.hidd,
`make -j8 kernel-hidd-vmpci` le reconstruit avec le même SHA256 808d5970… ;
aucune invocation manuelle spéciale nécessaire. Les bibliothèques et le SDK
étaient déjà construits : ce n'est pas encore une seconde reconstruction
indépendante complète. Revue 217.


## Premier transfert DMA virtio — 30 septembre

Core `887f3882da` : le HIDD ECAM accepte quatre callbacks DMA de plateforme,
tous présents ou tous absents. Sans politique explicite, il continue à refuser
allocation/traduction. La plateforme VM suit chaque allocation de pages et
n'accepte CPUtoPCI/PCItoCPU que dans l'étendue demandée d'un buffer vivant.
FreePCIMem exige son adresse de base exacte. Les pages sont vérifiées une par
une avec KrnVirtualToPhysical avant exposition, et l'étendue/alignment ne doit
pas déborder. Les adresses inconnues, padding d'allocation et adresses libérées
sont refusés. L'allocation échouée restitue mémoire et métadonnées.

Politique explicite de ce profil : périphériques logiciels VZ/QEMU cohérents,
adressage DMA par adresse physique invitée, pas d'IOMMU. KrnVirtualToPhysical
du noyau VM est actuellement l'identité ; le test hôte, lui, impose un décalage
CPU/bus pour vérifier que le code ne les confond pas. Ce n'est pas une preuve
DART/native/Pi ni un mapper pour des buffers utilisateurs arbitraires. La mémoire
provient de l'arène KrnAllocPages existante, donc reste soumise à sa limite de
taille. Les clients doivent ordonner leurs descripteurs/notifications ; le
probe utilise DSB SY. Ne pas activer virtio-blk avant adaptation de ses buffers
header/data/status, qui ne sont pas tous alloués par AllocPCIMem actuellement.

`make test-vmpci-dma` ASan/UBSan PASS sur le vrai vmpci_dma.c avec primitives
hôte simulées : adresse physique décalée, première/dernière adresse, dépassement,
free intérieur refusé, double free ignoré, traduction après free refusée et
pages non contiguës rejetées. `make test-pci-ecam-class` PASS, y compris politique
partielle refusée et forwarding des quatre callbacks.

Dans le second PCIPROBE, après déchargement du programme installateur :
allocation de 4096 octets via le HIDD résident, contrôle des traductions,
négociation VERSION_1 sur le périphérique entropie, queue de huit descripteurs,
une requête de 64 octets, publication ordonnée, notification et attente bornée
à une seconde de compteur architectural. La complétion vérifie used.idx=1,
id=0 et longueur 1..64 ; elle ne qualifie pas la qualité de l'aléa. Le reset est
acquitté avant FreePCIMem ; sinon le buffer reste réservé. Le probe vérifie
ensuite que l'adresse libérée ne se traduit plus. Pas d'IRQ demandée.

`artifacts/vz-pci-dma` et `artifacts/hvf-pci-dma` PASS.
Bundle `dcf1044b9e6279c2d2a6efb38e4af659ddf0655d0b8f70c1766d53b2f843d0cc` ;
disque VZ `899e8e2e34da825761329b1cfbf504231983d7573b592abc7a5d0535b14b9399`.
QEMU reçoit maintenant virtio-rng-pci dans le profil PCI pour exercer le même
transfert que VZ. Les marqueurs DMA sont obligatoires pour --require-pci-hidd.
Cette preuve est un vrai transfert de queue avec le device virtuel, pas encore
le fonctionnement de virtio.device, une interruption MSI ou une écriture disque.
SMP intact, revue 217 du pont résident approuvée.

`make test` et build standard `make -j8 kernel-hidd-vmpci` PASS.
Contrôle négatif `artifacts/vz-pci-dma-negative` : image avec pont résident mais
sans DMA refusée malgré son boot réussi. Binaires : vmpci.hidd
`bf165860e239b3cb9dfe25dfe76cad7d5d54d9f73f112f65f6fcf2aaa4edc7a7`,
pciecam.hidd `4452d3d344f2fefa0e856a90791a0c0ff97a9c6973449c393fbdf4bc4ba8f831`.
Revue 218. Suite bloc : q_Reqs/header/status et données doivent appartenir aux
allocations DMA, vérifier les erreurs de traduction, remplacer les seules
barrières compilateur et traiter les longueurs de complétion. MSI reste une
qualification indépendante avant les requêtes asynchrones.


## Queue bloc réelle et lecture de secteur — 30 septembre

Core `eaa7e42a5e` : virtio_queue alloue désormais les métadonnées de requête
(header/status) via AllocPCIMem et une copie DMA des données par requête. Les
écritures copient l'entrée avant publication ; les lectures ne copient vers
l'appelant qu'après une complétion réussie de longueur exacte. Les adresses
CPU sont traduites directement par le HIDD, sans lui repasser le résultat
physique de CachePreDMA. Les traductions base/fin sont vérifiées, y compris
le sentinel -1 et les débordements, avant de publier un descripteur.

Les seules barrières compilateur sont remplacées par des barrières matérielles
(DSB SY sur AArch64). Le pilote exige une mémoire DMA cohérente ; un port natif
non cohérent devra fournir une politique explicite avant activation. Les queues
sont bornées à 32768 entrées. Les complétions conservent l'identifiant 32 bits
(l'ancien UWORD pouvait convertir 65536 en zéro). Une tête inconnue/inactive ou
un saut impossible d'indice marque q_Broken et bloque les soumissions ; les
buffers sont conservés jusqu'au reset. La récupération et la réponse aux IO
encore pendantes dans ce cas restent à intégrer au contrôleur.

Le contrat de longueur utilisé est celui des queues/blocs modernes de
[Virtio 1.2](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html) :
lecture réussie = données + statut ; écriture/flush = statut. Erreurs ou
longueurs nulles/tronquées/excessives ne donnent jamais io_Actual positif et
ne recopient pas les données. FreeQueue exige un device arrêté et des
utilisateurs logiciels retirés ; il libère aussi les copies DMA restantes.

`make test-virtio-queue` ASan/UBSan PASS, compilant le vrai virtio_queue.c :
DMA non identitaire, allocations refusées, six positions de traduction refusées
sans publication/fuite, lecture réussie, lecture tronquée/null/excessive sans
altération de l'appelant, copie d'écriture, réponses hors ordre, wrap 65535→0,
identifiant 32 bits invalide et refus de réutiliser la queue cassée.
`make test-virtio-lifecycle` reste PASS.

Le nouveau tests/virtio-block-queue.c compile virtio_hw.c et virtio_queue.c
sans les réimplémenter. Le programme prépare un device temporaire sur le HIDD
résident, négocie VERSION_1, lit LBA0 par une queue de huit entrées, attend au
plus une seconde via le compteur architectural, exige la réponse Exec correcte,
512 octets et la signature 55aa du disque de boot isolé. Reset acquitté avant
libération. Pas d'IRQ demandée, pas d'écriture disque dans ce test.

`artifacts/vz-pci-block-queue` et `artifacts/hvf-pci-block-queue` PASS.
Bundle `162675ea4125a415a7d811f00ec7f92b3b6c6b4dc83bd44b0f43cda024d57f40` ;
disque VZ `bf829c01659b25fdc32b7d3c236db32691ca780f82ff6947ae45613cce442f3c`.
Le marqueur de lecture bloc est obligatoire avec --require-pci-hidd. Ce sont
les vrais corps de transport/queue mais pas encore le chargement de tout
virtio.device et de ses unités DOS. Suite : MSI/IRQ, écriture/flush sur disque
jetable, redémarrage et persistance. Revue 218 du DMA approuvée. SMP intact.

Build standard `make -j8 kernel-virtio` PASS ; virtio.device SHA256
`d5949772c02cd4cd7c4ed76f4f96e95b97e99596186e26c0617b02e6edcf0c4c`.
`make test` PASS. Ancienne image entropie-only refusée dans
`artifacts/vz-pci-block-queue-negative` malgré son boot et son DMA réussis.
Revue 219 ; commits VM `c59b1fe`, core `eaa7e42a5e`.

## MSI discovery and strict malformed-FDT checks

The ACPI-to-FDT bridge retains the single MADT type-13 MSI frame for the
single-root VM profile, as an `arm,gic-v2m-frame` child and PCI `msi-parent`.
It rejects duplicates, reserved fields, invalid alignment/overflow, overlaps
with GIC/ECAM/PCI apertures, and invalid overridden SPI ranges. Per ACPI 6.6
§5.2.12.16, count/base are ignored when flags bit 0 is clear: the eventual
controller driver must read MSI_TYPER. VZ's retained ACPI dump has exactly
that case. No hardcoded VZ address or inferred ITS/device-ID route is used.
This is discovery only: MSI frame mapping, allocation and PCI MSI-X delivery
remain to implement; the shared block queue test still polls completions.

Host parser tests cover both flags cases, FDT phandle association, absent
frame, duplicate/truncated/misaligned/overlapping frames and invalid ranges.
EFI build PASS, SHA256
`0e406d54c4ba860909164b3c2a066a75c7cd3f5697258130dc6b70624fd097a9`.
Boot regression artifacts `artifacts/vz-pci-msi-fdt` and
`artifacts/hvf-pci-msi-fdt` PASS including resident PCI, entropy and block read.
VZ input disk `d649709ca4218b251a0e9aad78455a3fe8d52c5a2bf617635ac0df96261ad5a6`;
core bundle remains `162675ea4125a415a7d811f00ec7f92b3b6c6b4dc83bd44b0f43cda024d57f40`.
These runs precede the following independent early-reader hardening.

Running the whole suite with UBSan halt-on-error exposed an existing malformed
FDT mutation reaching an unaligned token load in libfdt. AROSEarlyFDTOpen now
rejects unaligned structure/reservation offsets before invoking libfdt.
The explicit regression covers every invalid alignment bit and empty output
state; the mutation survey remains. early-fdt and vz-platform tests compile
with `-fno-sanitize-recover=all` so a sanitizer finding cannot be hidden behind
a successful exit. A separate unaligned 64-bit property read in the new host
test was replaced by libfdt's byte-safe accessor before qualification.
Review 219 (owned block DMA and actual sector read) approved. SMP untouched.

`UBSAN_OPTIONS=halt_on_error=1 make test` PASS after both fixes; detailed log
`/tmp/vm-msi-host-tests-2.log`. Complete core rebuild and post-change boots PASS in
`artifacts/vz-pci-msi-alignment` and `artifacts/hvf-pci-msi-alignment`.
Core `b7321a360a`, VM `5ad8096`, review 220. Bundle SHA256
`b1519b287792ca2aa9ad09362ef6721fe3950ea42b8047b0a35fb9562af78022`;
VZ disk `61f093c13023d88dddeac51ac27297d34b4e33dec7abd2fec406d4541b592350`.
Next: admit the discovered MSI frame into the stage2 MMIO map, validate its
MSI_TYPER range, and connect PCI MSI-X through the resident platform driver.
The existing vm_gic_map maps only the two main GIC windows; no MSI MMIO is
accessible yet. Preserve that explicit admission policy. QEMU's MSI topology
needs discovery independently; a successful boot is not MSI validation.

## MSI frame admission and measured VZ SPI range

Core `15b812fedc`: portable AROSFDTDiscoverMSIFrame resolves the PCI msi-parent
phandle to one enabled GICv2m supplier, validates its GIC parent, one 4 KiB
translated reg and the optional paired SPI override. Unknown controller kinds
are not treated as v2m. Stage2 admits only this associated frame, with its own
MSI_FRAME role and Device mapping, rejecting aliases with other devices/PCI
and with the memory plan. Device-map version is 6. Unassociated frames remain
unmapped. The fatal-ASan/UBSan map suite has 51 cases, including 21 MSI cases.
Complete core rebuild and `UBSAN_OPTIONS=halt_on_error=1 make test` PASS.

The guest PCI probe reads MSI_TYPER at offset 8 through Supervisor, preserving
EL0 restrictions. On VZ it measured frame `0x1fff0000`, raw type `0x80800080`,
SPI base 128 and count 128. Decode the documented ten-bit fields: the high
reserved bit in this VZ value is not part of the base. Source for the field
layout: Arm BSA DEN0094C §I.9.1. No MSI doorbell write or device IRQ yet.

Artifacts `vz-pci-msi-read` and `hvf-pci-msi-read` PASS; HVF reports an absent
MSI frame in the current firmware-generated tree. The stronger optional runner
flag `--require-msi-frame` requires `--require-pci-hidd` and exactly one mapped
read proof. `vz-pci-msi-read-strict` PASS; `hvf-pci-msi-read-negative` fails with
`proof-missing` as intended despite passing boot/PCI/DMA/block-read markers.
VZ input disk `09913c72822ab4dd188805b6f256f0e69120dcb7cea91a76c9427164f1ba3753`;
bundle `ebb5ff3589d1c8ab74ca82658f9ef35300e7e936ecaa0f13b77b61a359fcdeea`;
PCIPROBE `609f5f552f5260993b61ec2e53fc02f54daa90ea4adf0e6841ad4e5e97f89ae4`.
Review 220 approved and explicitly includes the new core mapping/resolver.
Next: resident platform vector ownership and PCI MSI-X table programming, then
virtio completion IRQs; keep QEMU ITS routing distinct. SMP untouched.

## Resident MSI-X routing and an actual block completion IRQ

Core `af211a2944` adds an explicit paired IRQ policy to generic ECAM and wires
it to the resident vmpci module. Its callback/context outlive registrations;
no implicit INTx routing is inherited. The VM adapter resolves the admitted
frame and its override/type range, owns each source, walks a bounded PCI
capability chain, refuses another MSI/MSI-X owner, validates the table BAR
against platform windows and leaves unrelated masked table entries alone.
It installs the kernel handler, programs table entry zero under masking, then
enables MSI-X and checks control readback. Failures unwind handler/table/control.
Removal disables MSI-X and restores the entry plus original INTx-mask bit before
removing the kernel handler; caller must quiesce its queue/device first.

SPIs are deliberately retired until reboot after each attempted registration,
including failed kernel setup. This bounds registration attempts to the frame
count (128 on VZ). It prevents a late posted message from reaching a new owner
without pretending to have a proven drain protocol. Device hot-unplug/source
recycling remain unsupported. Normal resident device operation does not consume
additional sources per transfer.

Core `bb0a89c34f` selects queue vector zero when PCI MSI-X is enabled, or leaves
NO_VECTOR for a legacy route. The actual virtio bus start calls this after
AddInterrupt and unwinds the handler if selection fails. The guest block test
compiles that same helper and the real queue/transport; its completion loop
only processes used entries after the MSI-X handler has run. It then checks the
Exec reply, exact sector bytes/boot signature, acknowledges reset, removes the
IRQ route, and frees DMA. This proves one real device IRQ through the resident
platform and kernel handler chain, not merely a pending-bit injection.

`make test-vmpci-irq` (new fatal-ASan/UBSan target) compiles the real routing code:
wrong owner, existing MSI-X, loop, missing memory decode, unrelated unmasked
entry, short/outside table, allocation/kernel setup failures, enable refusal
with restoration, duplicate registration, mismatched removal, correct removal,
no source reuse and exhaustion PASS. ECAM forwarding/default/partial-policy
checks, DMA/queue/lifecycle tests and full `make test` PASS with UBSan halt.
Bus lifecycle also tests queue-vector selection failure leaving no IRQ/worker.
All three affected guest modules compiled successfully.

First real IRQ run `artifacts/vz-pci-msix-route` PASS; final shared-helper run
`artifacts/vz-pci-msix-shared` PASS with --require-msi-frame --require-msi-irq.
HVF regression `artifacts/hvf-pci-msix-shared` PASS (no MSI frame, polled block).
Final disk `36b1462012c336648b946e123308a4dfacd70ee539f1d67e6b9ec6b2d8809f18`;
bundle `1ee20c338898f6d406f99cb6735f75db1d97d94e2f71f28af3c20c0f950fd22c`;
PCIPROBE `d1e7dd23c198e77d7bb3a8c68177d4f75f67e75f18e8c1bb143bf14fa6d0e68d`.
vmpci.hidd `fadbfd385fdde1ece1da3d6997820f7c6ae89927e6b08a083657aa8c67d4c398`;
virtio.device `7f11791e05625039703a5b4d9ae6d54fff0aec284f80fa96ff4a1e28dd696e16`.
Old read-only MSI image is rejected by --require-msi-irq in
`artifacts/vz-pci-msix-negative`, despite its MSI register/sector/boot markers.

Review 221 approved. Next is loading all of virtio.device with its storage
classes and IO task, then isolated write/flush/restart and crash qualification.
Current guest test still instantiates the shared queue directly: it is not
proof of complete DOS unit startup or durable writes. Native launcher fix
`7827774c39` reported on board196 may matter when returning to Wanderer testing;
inspect its final gate before reusing it. SMP remains bsek's work.

## Full installed virtio.device: first IRQ-backed read

Core `df384149d0` adds explicit private-header dependencies for every virtio
module object, including generated start/end objects. The initial complete
unit opened and returned geometry, but its first CMD_READ failed with
IOERR_ABORTED before publishing an available descriptor. Diagnostics found
virtio_io.o older than the change adding vr_DMABuffer to the shared request
layout; its generated dependency file was empty. The queue object used the
new layout. Rebuilding coherently changed the same test to PASS; the dependency
fix prevents this particular incremental-build mismatch. A `make -n -W` of
virtio_intern.h confirms virtio_io and the other objects are rebuilt. This does
not replace the independent clean-build comparison required by stage five.
The commit also clarifies the IRQ-context callback restriction from review 222.

`--virtio-device` extends the optional PCI package with bus.hidd, storage.hidd
and virtio.device. The guest test uses only public Exec/device interfaces:
OpenDevice unit zero, TD_GETGEOMETRY, asynchronous CMD_READ, bounded completion,
exact length and boot signature. It leaves published libraries resident and
retains request memory on timeout until the outer runner stops the VM. No
private queue structures remain in the final test. The VZ gate
`--require-virtio-device` requires the PCI/MSI-frame/MSI-IRQ gates and both unit
open and read proofs. It cannot pass merely on the earlier direct queue probe.

Final `artifacts/vz-full-virtio-read` PASS (1.08 s); earlier rebuilt diagnostic
candidate `artifacts/vz-full-virtio-rebuilt` PASS. Final disk
`1f183680e04428ae2a9319a21f5ab089d2d0e3a5a08fb3b2e7a62b55b188f566`;
bundle `139df24cb08f04834a1cfb96dc0ab230d066edbc0a436008aba48cebc9e6d75b`;
PCIPROBE `e4865ed3dc71bcdd7a3b2adf5df51c48b0b1e9898f3a01c28b2c97a718f97f38`.
Full `make test`, actual queue/lifecycle/routing sanitizer tests PASS with
UBSAN_OPTIONS=halt_on_error=1. Old complete-MSI-queue-only image is the negative
control in `artifacts/vz-full-virtio-negative`.

Before write qualification, fix virtio_io.c request ownership: rw/flush detach
before submit, then BeginIO detaches again on submission failure. Also validate
unaligned offsets and the whole transfer extent rather than just its start;
check error io_Actual handling and quick-completion ordering. Missing negotiated
FLUSH currently returns success: durability acceptance must require a real
flush or an established write-through contract. AbortIO is unsupported, so a
timeout must retain ownership rather than free an in-flight buffer. No writes,
filesystem persistence, crash recovery or physical hardware qualification is
claimed by this read milestone. Next is isolated write/flush/restart testing
after these checks. SMP remains reserved to bsek.

## Block request ownership and bounds before write qualification

Core `7769f704f7` fixes the BeginIO path identified in the previous milestone.
Submission removes the request once and clears IOF_QUICK before it can become
visible to the device/worker. A rejected submission restores the caller's flags
and completes locally without a second list removal. Successful submission
returns without accessing the request again: the recipient may already have
freed it. Read/write validation captures the high 64-bit offset first, clears
io_Actual, checks null data, logical-block alignment, nonzero aligned length,
and the entire sector extent using subtraction to avoid overflow. The wire
sector remains in 512-byte units even for a 4096-byte logical block.

`make test-virtio-io` includes the real virtio_io.c under fatal ASan/UBSan.
It covers unaligned/zero/null/end-crossing/overflow cases, 512/4096-byte blocks,
quick and nonquick submission rejection for write and flush (one removal),
64-bit offsets, and a mock successful submit that frees the request immediately.
Queue and bus/controller lifecycle regressions also PASS. The guest module was
rebuilt and packaged: `artifacts/vz-virtio-io-safe` strict full-device read PASS.
Disk `7b78e389ad40c2ce2e609405e823c015cf4da87527e5a145d106dd8a35a33fe2`;
bundle `744d2dd12c3226a3243457d2a5c65e9f3b1643235f20a0941b7fb9509432b0c9`.

Next: isolated guest write/readback/flush and second-VM verification, with an
explicit test-owned range outside the boot filesystem or a separate test disk.
Require actual flush negotiation/completion before claiming flush durability;
the existing no-FLUSH success fallback is not such evidence. Filesystem mount,
controlled crash recovery, VZ desktop/input and stage-six integration remain
open. No physical disk was touched. SMP remains reserved to bsek.

## Isolated block write, flush and fresh-VM persistence

`tools/run-storage-vz.py` accepts only a regular 64 MiB test image whose FAT
BPB describes that exact extent. It preserves the source and extends a copy
to 68 MiB. A signed, zero-padded 4 KiB header identifies the appended test area;
the next 4 KiB holds the deterministic payload and every other appended byte
is a guard. The guest requires exact geometry/header before writing. It checks
the installed driver's negotiated FLUSH bit (diagnostic private-structure
inspection), sends CMD_WRITE and CMD_UPDATE through public IO, and verifies
readback. A timeout retains all in-flight resources until bounded VM shutdown.

The runner stops the first VM, verifies the complete tail from macOS, then
launches a new VM process with a fresh state directory and a copy of the
resulting disk. Matching content takes a read-only restart path in the guest;
it must emit the restart marker, not merely repeat the write proof. The host
checks the entire tail again. --storage-phase gates require the full installed
device/PCI/MSI proofs, FLUSH negotiation, and the expected phase's markers.
The ordinary 64 MiB image remains a read-only guest qualification.

`artifacts/vz-storage-restart` PASS for both VMs and host byte checks.
Input `5c7f9275ce379e35c5d0a28402740dd4ce6586141df6d49eb21acd2bc121949b`;
bundle `ac70a375a0611f4d28cca1c683cd4353b5e7cc8d535740b81bbf631823844ffe`;
PCIPROBE `5029e2ebbb4a718b4548022e4f3743574678a855a7a3841a5d68fcfb0673b6c9`.
Both tails SHA256 `25532b511bd49b7dea78e73175a1ecc1977b6f2894cf1fed0d7f9276d2baef6c`.
A one-byte corrupted payload is rejected as guest-failure in
`artifacts/vz-storage-corrupt`; it is not silently overwritten. The unextended
image passes `artifacts/vz-storage-read-regression`. Actual IO/queue/lifecycle
fatal-sanitizer regressions and Python syntax checks PASS. Core remains
`7769f704f7`; no core change was needed for this write test.

This proves a 4 KiB block transfer and persistence across VM process restart.
It does not prove host-power-loss durability, a mounted filesystem, sustained
IO or crash recovery. Next mount a filesystem on an explicitly isolated region
or second disk, exercise real files across restart, then controlled interruption.
VZ desktop/input, clean-build comparison and networking/clipboard/file exchange
remain outstanding; stage six is not complete. SMP remains reserved to bsek.

## Mounted FAT files survive a fresh VM

`tools/run-filesystem-vz.py` extends a copy of the validated 64 MiB boot image
to 80 MiB, with a separately mformatted 16 MiB FAT volume containing SEED.TXT.
At that exact geometry the guest registers VMTEST0: using MakeDosNode, a
DosEnvec confined to the appended sectors, and the resident FAT SegList from
FileSystem.resource. It verifies the seed before creating DATA/PENDING.BIN,
writes 64 KiB in sixteen blocks, closes, renames to KEPT.BIN and compares all
bytes and EOF. Inhibit(VMTEST0:, DOSTRUE) drains/detaches FAT, then an explicit
CMD_UPDATE completes with negotiated FLUSH. The next fresh VM reads the existing
file. Host mtools independently verifies payload/seed and absence of the old
name after both phases. Input disk is preserved; no physical device is used.

First run `vz-filesystem-restart` stopped in early IRQ diagnostics before any
PCI/filesystem code. This is consistent with the unresolved intermittent
virtio-console symptom, not a FAT finding. Same bundle under HVF/QMP
`hvf-filesystem-startup` passed early IRQ and polled block checks, then failed
opening the complete driver as expected without the VZ MSI route; this is
not a passing filesystem run and does not explain the VZ symptom.
`vz-filesystem-restart-2` and `vz-filesystem-diag` reached mount/mkdir/write/
rename/content, then Inhibit returned 218: the test had named both device and
volume VMTEST, so DOS resolved a volume rather than the required device. It
also passed TRUE instead of DOS's DOSTRUE. Separate VMTEST0 device and VMTEST
volume plus DOSTRUE fixed the test; no core patch was needed.

Final `artifacts/vz-filesystem-fixed` PASS both VMs and independent host checks.
Input `e35ffbcf02ef64b5d040fbee9d00ba3cac1b79e172bedd937574e153d6cb21b3`;
bundle `0f4dec04fe63b31c9211034767a68d04fc8569a17d428f4234bef80c5d16b12a`;
PCIPROBE `60ce5681963901aa88d84ee400e968aac184b8e74aa0d289e69185a4a876635c`.
File SHA256 in both phases:
`1942de172742ea4cc88bacca1802a39924147e552ff0a36397597ad066c3e94f`.
A flipped byte at file offset 60001 is rejected by the restart guest in
`artifacts/vz-filesystem-corrupt`. Prior two-VM block gate also PASS in
`artifacts/vz-filesystem-block-regression`; actual IO/queue/lifecycle sanitizer
checks and Python syntax checks PASS. Core stays at `7769f704f7`.

Reviews 223/224/225 approved. Next qualify interruption during writes and
sustained filesystem IO; this clean-close/restart test is not crash recovery.
VZ desktop/input, reproducible independent rebuild and stage-six integration
remain open. SMP remains reserved to bsek.

## Abrupt termination during an open-file workload

`tools/run-crash-vz.py` first requires the clean filesystem/restart baseline.
It copies the resulting 80 MiB image and adds an unused 4 MiB trailer: exact
84 MiB geometry selects crash mode while the mounted FAT extent stays 64..80.
After checking the seed and durable KEPT.BIN, the guest opens UNFINISHED.BIN,
writes 64 KiB, emits the active-workload marker and repeatedly seeks/rewrites
without closing, flushing or inhibiting. The host observes required mount/
content/workload markers, sends SIGKILL and requires process return code -9.
No guest shutdown is requested. The exact crash image is preserved. A recovery
copy loses only the unused trailer, without repairing any filesystem byte;
read-only fsck_msdos examines the extracted volume, a fresh VZ guest remounts,
and mtools checks KEPT.BIN both before and after guest recovery.

First `artifacts/vz-filesystem-crash` FAIL at baseline: early IRQ-console silence,
before any write-cut scenario. It remains a failure and the underlying early
console symptom is unresolved. The same scenario's explicit second attempt
`artifacts/vz-filesystem-crash-2` PASS: workload observed, kill return -9,
read-only fsck return 0, no orphan-cluster report, fresh guest remount and durable
file content PASS. Post-kill FAT volume SHA256
`dda29fac6680548e27f889fcbd1d1cea43d02a4325849ff9deb2d12bb63c27b6`;
durable file `1942de172742ea4cc88bacca1802a39924147e552ff0a36397597ad066c3e94f`.
Bundle `e7350d794eb912d669b1c1ba62169bcda5234dd74db46446165c5516e1ec59df`;
PCIPROBE `c79243b73562e1dd192488b52cf8bd410405d6fe1e8c94fd8bdc1db38262dd45`.
Core remains `7769f704f7`. Actual IO/queue/lifecycle sanitizer tests and Python
syntax checks pass. Old guest without the crash workload is a negative control
in `artifacts/vz-crash-negative-old-guest`.

Scope: one observed abrupt open-file write workload, not a precise DMA-boundary
cut, exhaustive FAT recovery or physical host power loss. The interrupted file
may be absent/partial; the already flushed reference must remain exact. Broader
interruption and sustained IO remain, as do VZ GPU/input, independent clean-build
comparison and networking/clipboard/file exchange. The early console issue
needs independent debugging before sustained-use acceptance. SMP untouched.

## Early-console loss: independently captured late TX completion

Read-only inspection of the previously failed
`vz-filesystem-crash/baseline/write/system.img` found the complete correct
64 KiB KEPT.BIN despite its console stopping during early IRQ diagnostics.
This proves that run continued through filesystem IO; it was not a whole-CPU
freeze. A diagnostic filesystem test now snapshots the retained VMConsole
record through Supervisor and saves CONSOLE.BIN before FAT flush. This path
uses disk IO independently of the failing console. The record is raw diagnostic
state tied to the exact header/package, not an external stable ABI.

Snapshot candidate bundle `6263400457d09dde168f8f3c7c62aa137cb6a2c53e991d2f7281ebc07892fe1e`
passed six two-VM runs (vz-console-snapshot and suffixes2..6), then
`artifacts/vz-console-snapshot-7` reproduced the silence. Extracted
`write-console-state.bin`: initialized1, failed1, stage7, published/available/
used index517, last used slot4 id0. No further descriptor was published once
failed latched. The old TX loop marks failed permanently when its ten-million
spin budget expires; a valid later completion cannot clear it. Initialized1
and stage7 exclude initialization failure; the intact id0 excludes the other
TX failure branch. This is evidence of a late valid completion after the TX
wait gave up, not inference from silence alone.

Core `c200c678ee` and matching VM header replace that TX spin count with a
one-second architectural-counter deadline. Timeout retains the published
buffer/descriptor, without latching permanent failure; a subsequent write must
drain it before reuse. Invalid completion id remains fatal. Completion indexing
also casts before subtract/wrap selection, keeping index0's previous slot at7.
The actual shared-header sanitizer test injects a clock and proves timeout,
no overwrite while pending, delayed completion/recovery, 16-bit wrap and bad-id
refusal. This is a single-CPU early polling console; no SMP work was done.

Core `d49ffc2e5c` adds the missing stage2_fdt_map.o dependency on vmconsole.h.
The first build had updated kernel but not bootstrap; that unrun candidate was
superseded. Firmware, bootstrap and kernel were then rebuilt coherently. Full
make test and test-console-timeout PASS with fatal UBSan; core and EFI builds
PASS. Final disk `bbc692cd62917c22257a201acbc5a41dffd8dcb770d0027070e6ed2201544eeb`;
bundle `3598968a63ce48d4907ddfba6d5d99a0b82661983bbd96f4b8f6df94e1801e0e`;
PCIPROBE `a92928f54f6e64ca9ec138020e25267ce0d3afb22507de3c87f3b4fd9756e094`.
`artifacts/vz-console-recovery-1..4` PASS (eight VMs); all independent disk
snapshots show initialized1/failed0 and used index equals published index.
`artifacts/hvf-console-recovery` PASS with virtio console, timer/IRQ/platform/PCI
checks; its package omits the complete device because QEMU ITS routing remains
unimplemented. HVF bundle `4cbed314feb57d3ac6a8e12f07a9d8e85e433c1edbf21744eab0afbe0e6639e3`.

The captured permanent-disable path is fixed; eight successful boots do not
prove all console failures impossible. Initialization/control-queue spin bounds
were not changed, and a timeout can still omit later text while a descriptor
is pending. Keep independent snapshots/supervision during prolonged tests.
Next return to sustained IO and VZ graphics/input rather than treat this finite
sample as completion of stage six. Review227 approved; SMP remains bsek's work.

## VZ GPU admission and real display-info command

Re-read aros-next/docs/AARCH64.md: the VM uses virtio display/input and optional
vsock host services; shared drivers should remain separate from Apple-specific
hardware. No existing virtio GPU driver was found in the core checkout. The
existing virtio PCI mapping/reset/negotiation and owned DMA queue allocator are
reused by a GPU controlq test; no second transport was written. Wire layout is
from Virtio1.2 section5.7, https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html.

Launcher --gpu permits an explicit 1024x768 graphics device with --headless,
without window or USB input creation. Ordinary windowed startup retains its
existing graphics/input configuration. run-vz --require-gpu exposes the device
and requires a separate display-query proof. The PCI probe recognizes1050,
resets, negotiates VERSION_1, allocates controlq and a512-byte DMA message area,
submits GET_DISPLAY_INFO, checks used id/index/exact response size/type and
scanout0 enabled1024x768. It acknowledges reset before releasing buffers/maps
and restoring PCI command; failed reset retains DMA. This control command is
polled; it does not yet qualify GPU IRQs, rendering or a graphics HIDD.

First `vz-gpu-query` exited at launcher argument validation (the old parser
rejected a second option); corrected option validation accepts unique known
options. `artifacts/vz-gpu-query-2` PASS with GPU query and installed block driver
MSI/read proofs together. Product1050 enumerated, response408bytes accepted.
Old no-query guest `artifacts/vz-gpu-query-negative` correctly FAIL timeout even
though ordinary boot/device proofs pass. `vz-gpu-launcher-filesystem-regression`
PASS both VMs without GPU. Swift build, guest Werror build, full make test,
console-timeout and real queue sanitizer tests, Python syntax and diff checks
PASS. Core unchanged at d49ffc2e5c.

Input disk `2f0c7a4fb715dfad09b96443a7715ea17f79d67387f8883902bc8b32a3ad3f36`;
bundle `6efdb7805dcd7f40d672bab2b7ae4a6edeec55d186ad9c52efca7e8d053c1ead`;
PCIPROBE `42f0835712bcbd8cf2912cc5024b7e3cdb36c25cb8b14168a40dda53ec4b4c63`;
launcher `d8b6c74c3a05312b4616fe55043496c415dd02391ea177339aa7dffb951f7d2f`.
Next implement resource/backing/transfer/scanout commands and rendered-pixel
verification, then connect an AROS graphics HIDD (existing gfx/efifbgfx provide
reusable bitmap/rendering behavior). A display query is not a desktop.
Review228 approved the console diagnosis/fix. VZ input and stage-six services,
independent complete rebuild and sustained IO remain. SMP untouched.

## GPU surfaces, fenced commands and independent rendered pixels

The GPU test reuses its two owned descriptors sequentially, waiting for each
completion before reuse; it never calls the block queue's descriptor free-list
allocator for these commands. Every command uses FENCE and a matching nonzero
fence id; responses must have exact used length, expected type and returned
fence. Added RESOURCE_CREATE_2D (B8G8R8X8), ATTACH_BACKING (owned contiguous3MiB),
TRANSFER_TO_HOST_2D, SET_SCANOUT and RESOURCE_FLUSH. A1024x768 red/green/blue/white
quadrant image is held for5seconds. Cleanup disables scanout, detaches backing,
unrefs and acknowledges reset before freeing DMA; any failed reset retains it.
This remains a finite transport test, not the resident graphics HIDD.

`artifacts/hvf-gpu-scanout` PASS: QEMU virtio-gpu-pci, QMP screendump selecting
GPU/head0, actual decoded quadrant RGB values exactly match255/0 primaries and
white, guest cleanup + boot/timer/IRQ/platform/PCI proofs pass. Viewed
`scanout.png` directly: the intended four quadrants cover the image. The runner
reuses tests/check-aros-render-frame.py's PNG decoder, no Pillow dependency.
Old no-render guest `hvf-gpu-scanout-negative` FAIL proof-missing as intended.
HVF bundle `c818ec43cf452205972cf5ac1a7f1a41633076c19d7a95e01838515b41dee513`.

VZ command protocol `artifacts/vz-gpu-scanout-protocol` PASS, including the same
fenced resource/transfer/scanout/cleanup sequence and installed block IRQ/read.
VZ visual gates `vz-gpu-scanout` and `vz-gpu-view-capture` FAIL: screencapture
could not create an image from the target window. An app-owned NSView cache
experiment produced all-black images, correctly rejected by the pixel gate;
it does not establish whether the Metal-backed view actually displayed black.
The cache experiment was removed. --gpu-window retains an ordinary VZ test
window without adding USB input to the headless profile. The strict VZ visual
gate remains available and cannot pass on command responses alone. No private
VZ API or screen-permission workaround was introduced.

VZ input `ee8b3066ccc0542763dc882b8b5fcebd7e2d4dd1dff70151c46c9ae21a8c2e92`;
bundle `5c1d8d3ad758cced0f6d1a56bf59fca823930c3295b821f94429f16474899359`;
PCIPROBE `1ec0ec31e7bcece34b9a535ec0944aaba12e665f6eba37a80a437e21e87cdff7`;
final launcher `046bef28890248f478e3a66b11163320132f85f11c5513ce3b364ecb67f14ac3`.
Guest/Swift builds, console/queue/lifecycle sanitizer tests, Python syntax/diff
checks pass. Core stays d49ffc2e5c. Next move this transport into a reusable
resident graphics HIDD and connect AROS rendering, while keeping VZ visual
acceptance open. Review229 approved the prior display-query step. Board196
reports launcher fix ab97ed7453 now qualified on EL1/EL2 with missing-program
and missing-script negatives: inspect that change before the VM Wanderer return.
SMP untouched; stage six remains active and incomplete.


## Persistent GPU transport and partial updates

Core be6caf1b2e extracts the GPU protocol into rom/hidds/virtiogfx/transport.c
and transport.h. Core 7a1cc5ee61 separates the 64-bit fence identity from the
16-bit virtqueue index (review230 note). The heap-owned context retains PCI/DMA,
queue, command memory and a 1024x768 B8G8R8X8 surface until Close. Open/GetSurface/
Present/Close define the lifetime explicitly; Present validates rectangle bounds
and serializes command submission. Failed commands stop buffer reuse; failed
reset quarantines all DMA. Close distinguishes clean teardown, reset recovery
and retained quarantine. Existing virtio hardware/queue code is reused directly.
This is the backend for a graphics HIDD, not yet a registered HIDD module.
Completion remains polled; GPU-specific failure injection and IRQ completion are
follow-ups. Caller must retain OOP/PCI providers and serialize surface writes
and destruction according to transport.h.

PCIPROBE now calls this backend, rejects empty/out-of-bounds rectangles, then
presents four separate quadrants instead of one full frame. The nonzero x/y
transfers exercise the backing offset and full-surface stride.
`artifacts/hvf-gpu-transport` PASS before the independent-fence change, exact
captured pixels plus timer/IRQ/platform/PCI gates. Final VZ protocol gate
`artifacts/vz-gpu-transport` PASS, including installed block IRQ/read;
VZ pixels remain unverified. Final PCIPROBE SHA256
`db2608743c58e1268e79d7ed00796888c73822c1051fad584ba3a053430e7cdf`;
VZ bundle `1a55a5ba03edac7d287d3ee11e49c3b9f05495f1e52d84263bf239b0076e8f7d`;
VZ input `3556f308c34cbb444f509419c75b595683501e7e71d92ba35e258351f313cd8d`.
Shared queue/controller/bus sanitizer regressions pass. Review230 approved the
previous finite scanout milestone. Inspected native launcher fix ab97ed7453:
it tracks Boot Mount liveness instead of requiring observation of CLI1; it is
not cherry-picked yet. Next: reuse the existing chunky bitmap/display frontend
for a real graphics HIDD, then validate Wanderer and input. SMP untouched.

Final independent-fence build: `artifacts/hvf-gpu-transport-fence` PASS,
including exact captured quadrant pixels and all required startup gates;
bundle `3a7ec023a13cd9c33240be40b7431c378cae0a31842bb0e342cd459941101b8a`.
All test processes terminated. Goal remains active; this completes a transport
extraction, not stage3 or stage6 acceptance.


## Resident virtio graphics HIDD and real Wanderer rendering

Core through 7ea36d0966 adds virtiogfx.hidd, directly reusing maintained EFI
chunky bitmap/display source through thin wrappers with distinct OOP class IDs.
Shared HWData gains an optional task-context present callback, invoked after
clipped pixel copies and display blanking. Reset callback only clears memory;
it does not submit GPU commands from interrupt context. The HIDD owns its GPU
surface and retains providers for its lifetime; failed reset retains the module
and DMA. Fixed1024x768 B8G8R8X8, polling, no hot removal. Dynamic modes/IRQ
completion are not claimed. New source uses John's explicit APL1.1 header;
2282232c05 fixes the transport headers requested in review231.

The VM desktop package replaces efifbgfx with virtiogfx and adds OpenFirmware,
pci, pciecam and vmpci residents. Kernel validation admits this exact ordered
variant only in the VM build, preserving the native profile. Required fixes:
static C helpers for pre-DOS graphics initialization; vmpci opens resident
providers by basename before trying DEVS: paths; fixed boot display creation is
deferred through DDRV_BootMode until UI dependencies are ready. Immediate display
creation let Wanderer exit without a screen; failure stage/foreground diagnostics
preserve that distinction. HIDD color shifts align32-bit components (8/16/24),
not byte offsets. Wrong shifts visibly produced a green desktop, rejected by
the final image verifier.

`artifacts/hvf-virtio-desktop-color` PASS: guest startup/foreground contract,
resident driver marker and independently captured GPU/head0 image, 1024x768,
769884/786432 pixels at expected gray153. Image viewed directly: Wanderer bar,
boot-volume icon, gray desktop, no requester/CLI. Bundle SHA256
`4b7d5ea12c2e59877d72a827aaf3776a2f0ef837bce554e7efb89dc84d80d848`.
`artifacts/hvf-efi-desktop-regression` PASS: rebuilt shared EFI frontend,
800x600 desktop; bundle
`39d18bd60bc8791b17428aab5a645747aa0ef27f46df9be8bbf400eca614f3d1`.
Verifier keeps explicit800x600 and1024x768 profiles, centers requester detection,
and tests clean/CLI/requester synthetic cases at both sizes. Earlier green
`hvf-virtio-desktop-bootmode/desktop.png` remains rejected for wrong colors.
Failure runs retain registers and, where available, a GPU screenshot; the
initial profile/dependency/failure captures remain under hvf-virtio-desktop*.

`artifacts/vz-virtio-desktop-startup` PASS with the same final bundle: PCI and
GPU registration, timer/IRQ/ABI/DOS and complete guest startup/foreground proof.
This is --desktop-startup, NOT captured VZ pixels or input acceptance.
Input disk `c184d0b7a8bc32d91a7a376bc3a0d4e89bc1f53b7cf7598391b6290bbf469527`;
launcher `046bef28890248f478e3a66b11163320132f85f11c5513ce3b364ecb67f14ac3`.
Package: build/virtio-desktop-color; disk: build/virtio-desktop-color.img.
All guest processes ended. Stage3 still needs VZ visual/input qualification;
stage6 still needs sustained IO, independent rebuild and integration services.
SMP untouched. Native launcher ab97ed7453 is not imported (the existing positive
launcher path passed here); its missing-barrier failure fix remains a follow-up.


## VZ input transport identified: reuse USB xHCI/HID

Review233 approved the resident graphics HIDD. The roadmap proposes virtio-input
for the generic VM, but this VZ launcher configures VZUSBKeyboardConfiguration
and VZUSBScreenCoordinatePointingDeviceConfiguration. Actual guest PCI inventory
with those devices confirms an Apple xHCI controller, vendor106b/product1a06,
class0c0330; no virtio-input device. Use the existing AROS PCI xHCI + Poseidon/HID
stack for this backend, which also supports the Raspberry Pi reuse objective.
Do not claim that the virtio-input plan has been implemented.

Added --usb-input to the launcher and bounded VZ runner so the windowed device
configuration can be exercised without screen capture. PCI inventory records
all vendor/product/classes. xHCI inspection maps BAR0 through the same owned
PCI driver's MapPCI path, temporarily enables memory decode and restores it;
no controller reset, start or USB DMA. It validates a bounded capability chain,
BAR extent, capability length/version/ports. Observed BAR0=0x50001000,4096bytes;
caplength0x20, version0x110, HCSPARAMS1=0x10002010 (16ports/32interrupters/16slots),
HCCPARAMS1=0x02600801; MSI-X control0x001f and MSI control0x008a. MSI-X availability
is not a proof of USB interrupt delivery.

`artifacts/vz-xhci-capabilities-strict` PASS: xHCI capability mapping plus all
existing GPU protocol/block-device MSI-X/read/startup gates. Bundle
`d2d77c75447f36e3747c56ead51d3f00426ae14a5b535f07dd0f4677a6240a6c`;
PCIPROBE `4b915785a2478a6171f33c38015c3d995ac8d5704c7787fbba4e5e4a5c4775e6`;
input disk `4beef31c4788847cf20864ea9d1f48c87db1f1677cccfa3114b0344664c8ba7b`;
launcher `4bcffc65164b25d0d6d1e470b9a08cc4493c6b0468000bb69aafc55362df2e17`.
`artifacts/vz-xhci-missing-proof` FAIL timeout as intended with the previous
inventory-only guest: the runner requires the mapping proof, not just the
presence of the controller. `artifacts/vz-desktop-usb-exposed` PASS startup with
the previously qualified desktop disk and the USB devices exposed. None of
these tests delivers a key/pointer event; verdicts explicitly say so.

Built kernel-usb-pcixhci in the isolated VM tree. No upstream pcixhci changes
are pending on origin/apple-silicon relative to this worktree. Existing build
warns that diagnostic code in xhci_hcd.c reads ctx[2]/ctx[3] beyond the declared
ctx[2] array (the following hardware fields are deq); resolve before strict
sanitizer qualification. More substantive: xhciPrepareDMABuffer in
rom/usb/pcixhci/xhci_schedule.c passes CPU pointers into payload TRBs, only
bouncing for32-bit addressability. Ring/context allocations already use PCI DMA
allocation and translation. Next adapt payload buffers to owned PCI allocations
and explicit bus addresses, preserve cache/copy/free ownership on completion and
failure, then qualify controller startup and USB HID enumeration/delivery.
No USB transfer was attempted against this known incomplete DMA contract.
Core source revision remains7ea36d0966; SMP untouched.

Poseidon and its SDK link library also built successfully. Binary hashes are
in artifacts/usb-build-products.json; no runtime USB qualification is claimed.
All build and guest processes ended.


## xHCI owned payload primitive and lifetime audit (stage 3, incomplete)

Core `d16c1de1a2` adds xhciPreparePCIPayload/xhciReleasePCIPayload in
rom/usb/pcixhci/xhci_dma.c. Preparation allocates through the owning PCI HIDD,
checks bus translation at 4 KiB intervals and the final byte, rejects overflow,
noncontiguous mappings and unsupported 64-bit extents, copies OUT and prepares
cache ownership. Release balances the prepared cache extent even for zero
actual bytes and bounds IN copies; invalid lengths/direction report HOSTERROR.
The completion cookie is CPU metadata, not the address submitted to hardware.
Platform controllers are explicitly rejected by this PCI-specific primitive.

`make test-xhci-dma` PASS under AddressSanitizer and UndefinedBehaviorSanitizer,
using the actual core source and mocked Exec/PCI services. Covers nonidentity
bus translation, partial IN with untouched tail, zero-actual release, excessive
completion length, changed request extent/direction, metadata/PCI allocation
failure, first/interior/final translation failures, short cache preparation,
32-bit limits and integer overflow, null/invalid input and platform rejection.
This does not simulate a running controller or prove abort retirement.
`kernel-usb-pcixhci` rebuild PASS; diagnostic endpoint dequeue reads now use
`deq.addr_lo/addr_hi`, eliminating the ctx[2]/ctx[3] out-of-bounds warnings.
Exact source/test/device hashes: artifacts/xhci-owned-payload.json.

The new primitive is deliberately NOT called by the scheduler yet. Wiring it
into the existing release sites exposed unsafe DMA lifetime assumptions:

- xhciFinishRequest clears ringio back-pointers and frees the buffer while its
  own comment explicitly allows TRBs to remain until a later endpoint stop.
- cmdAbortIO calls this path with interrupts disabled; the NAK timeout interrupt
  also replies/frees through xhciAbortRequest. A synchronous command wait there
  would be invalid. Cancellation must be handed to task context.
- xhciQueueData_IO publishes TRBs one at a time, may fail halfway, and its caller
  releases the buffer on failure. Submission needs reservation/atomic publish,
  or retirement of the published prefix before release.
- Endpoint teardown ignores stop/reset completion codes before freeing rings.
  An unacknowledged stop must retain DMA memory, never pretend retirement.
- Completion error mapping can overwrite a payload validation error; integrating
  this API must preserve the validation result through the final reply.

Next: repair these retirement/publication paths with failure-injection tests,
then connect owned payloads, bound runtime/doorbell offsets inside BAR0 (review
234), check interrupt installation failure, and qualify controller startup,
USB enumeration and keyboard/pointer delivery. These are code-audit findings,
not observed VM USB crashes. No USB transfer or input acceptance is claimed.
The old scheduler remains unchanged; do not ship it as VZ USB support. SMP
untouched; goal/task71 stay active.


## Whole-payload ring admission (core 305335fee4)

Moved existing queue/descriptor code from xhci_hcd.c into xhci_ring.c so tests
compile the real publication functions. xhciQueueData and xhciQueueData_IO now
check capacity for every segment under the existing outer ring lock before
publishing any descriptor. Admission simulates the primitive's guard slot and
Link insertion, including a producer already positioned on the Link slot.
The lock stays held through all scalar insertions. Rejected transfers leave
ring descriptors, indices and owner pointers untouched. Invalid ring indices,
missing bookkeeping and payload extent overflow are rejected. This uses the
existing single-CPU Disable/Enable contract; no SMP implementation added.

`make test-xhci-ring` PASS with ASan/UBSan: 1152 combinations on an eight-slot
model ring, invoking actual queue code, all producer/consumer positions,
segment counts 1..9, and IO/non-IO entry points. Checks guard-slot rejection,
Link wrap/address/cycle, DATA-to-NORMAL segmentation, chain/IOC/direction,
lengths, payload addresses and request correlation. Zero-length and malformed
inputs are covered separately. A regression first invokes the old unchecked
sequence and confirms two TRBs escape before the third fails; the new public
path rejects the same request with zero descriptor writes/translations.
Cross-build kernel-usb-pcixhci PASS. Exact identities:
artifacts/xhci-ring-admission.json. No USB runtime test in this milestone.

Correction to the earlier DMA audit: xhciInsertTRB already translates non-IDT
CPU payloads through CPUTOPCI (except Set TR Dequeue, whose parameter is already
a bus address). Therefore the new PCI-owned primitive cannot simply replace
xhciPrepareDMABuffer: its returned bus address would be translated twice.
The actual missing contract is owned-buffer allocation/admission plus explicit
address-domain handling and lifetime. The host ring test confirms existing
payload and Link translations with a deliberately nonidentity PCI mapping.

Still required before connecting the owned payload primitive: allocate before
publishing a control SETUP; reserve setup/data/status together; handle the
request-level failure without stale descriptors; defer AbortIO/NAK cancellation
to task context and establish acknowledged retirement before freeing memory;
retain allocations when stop/reset fails; preserve completion validation errors.
Also audit segment boundaries against the controller's address rules during
explicit address-domain integration. This change prevents insufficient-capacity
partial DATA publication, not every control/abort/teardown failure, and is not
an atomic hardware publication protocol for complete control transactions.
SMP untouched. Goal/task71 active; review235 of the earlier primitive pending.


## Complete control preparation and admission (core b81bce6042)

Control scheduling now prepares its payload before SETUP publication, outside
the interrupt-masked ring section. A missing buffer, failed allocation or
shortened cache preparation rejects the request before touching the ring.
The shared ring primitive admits SETUP + all DATA segments + STATUS together
under the existing single-CPU lock. After admission there are no allocations
or command waits and all scalar failure predicates have already been checked.
SETUP TRT, DATA direction/chaining and opposite STATUS direction are retained;
no-data STATUS is IN. Control correlation indices and buffer ownership are
installed before the wrapper re-enables completion interrupts.

Setup is packed from the eight USB bytes into a numeric little-endian value;
Poseidon's psdSetupPipe already stores wValue/wIndex in little-endian form
(rom/usb/poseidon/poseidon.library.c). Test checks the exact emitted eight bytes.
This is source/host evidence, not a big-endian target qualification.

New xhciDiscardPreparedDMABuffer balances the prepared cache extent and frees
an old CPU-addressed bounce allocation without any IN copy. It is only for
buffers that were NEVER published, not a replacement for abort retirement.
Control admission failure and data-batch admission failure use it. PCI-owned
payload adoption remains deferred; its header now explicitly warns that the
existing InsertTRB translates CPU addresses, so returned bus addresses need an
explicit already-translated path. Review236 approved the preceding payload
admission change; its two notes are addressed by this header and control work.

`make test-xhci-ring test-xhci-dma` PASS, ASan/UBSan. The actual ring source
passes 1024 complete-control cases (producer/consumer position, 0..7 DATA
segments, IN/OUT), alongside the 1152 payload cases. Tests check no publication
or result mutation on capacity failure, SETUP flags/bytes/TRT, DATA tail without
CH/IOC, STATUS direction/IOC, and no-data indices. The actual control wrapper is
included too, with mocked preparation/discard: allocation failure, short prep,
insufficient ring capacity and null data produce no writes; successful prep
occurs outside the ring lock. Actual discard function is tested separately for
cache extent/flags and both bounce and direct buffers. Driver cross-build PASS,
including final incremental rebuild after the metadata-lock/header changes.
Exact source/test/device hashes: artifacts/xhci-control-admission.json.

Remaining lifecycle issues found during this work, to repair before live USB:
failed scheduling can leave hu_DevBusyReq set, blocking the requeued request;
xhciActivateEndpointTransfer calls Remove/ReplyMsg on failure even though its
callers handle failure themselves; active TD-list publication can lag descriptor
publication and interrupt delivery. Fix shared request transitions, single reply
ownership, and queue/metadata publication ordering, then deferred AbortIO/NAK
retirement and failed-stop retention. Control capacity is now covered, but this
is not a proof of those asynchronous lifetime properties or USB delivery.
No VM USB transfer attempted. SMP untouched; goal/task71 remain active.


## Unpublished request rollback (core b1b8a9533a)

xhciActivateEndpointTransfer is now a software-reservation operation only:
validate the private device/endpoint/ring and key before acquiring ownership,
return HOSTERROR on failure, never Remove or ReplyMsg. Its callers have already
removed the pending request and own its eventual reply or retry. This removes
the double-remove/double-reply path on a missing ring in async scheduling.

xhciRollbackUnsubmitted clears only the failing request's busy/NAK reservation
and invalidates its TRB indices. It leaves a different owner's state untouched.
Async, interrupt and ISO rejection paths use it, so ring-capacity rejection can
be retried instead of leaving hu_DevBusyReq pointing at the requeued IO forever.
The helper is explicitly NOT an abort of published DMA. Async terminal errors
also detach/free private request metadata before the single ReplyMsg. Interrupt
and ISO keep their current retry policies; broader error-policy work remains.

`make test-xhci-request` PASS under ASan/UBSan. It includes the actual request,
async scheduler and interrupt scheduler source, with OS list/message services
and submission outcomes mocked. For control and bulk: capacity rejection then
successful retry of the same request; preparation failure; missing ring; missing
prepared device; correct remove/requeue/submit/doorbell/reply/free counts. For
interrupt: capacity or missing-ring rejection followed by successful retry,
with no premature reply. Invalid endpoint and another owner's busy/NAK state
are checked. Mocks poison removed node links and reject a second Remove;
terminal async cases require exactly one reply and private free. This is not
concurrent IRQ injection or an ISO/RT-ISO runtime test.
Guest kernel-usb-pcixhci rebuild PASS. Exact identities:
artifacts/xhci-request-rollback.json. No USB device traffic attempted.

Next: move active-list insertion, metadata and doorbell into the protected
publication section after allocation. Existing schedulers still insert on the
active list after the queue helper returns, allowing an early completion to
outrun list publication; they also dereference private state to ring the doorbell
after interrupts can resume. Then address task-context cancellation, acknowledged
stop/retirement and failed-stop retention before switching to owned PCI payloads.
The whole stage-six objective remains open. SMP untouched.


## Protected publication before completion IRQs (core 2a0f7a82df)

Control and payload submission now share protected publication in
xhci_control.c. Allocation/cache preparation happens outside the ring lock;
after successful whole-transfer admission, the outer lock covers correlation
indices, buffer ownership, completion-code reset, active-list insertion, ISO
PTD active/buffer-valid flags, and the hardware doorbell. Only then are guest
interrupts re-enabled. The doorbell operation is MMIO with a barrier/readback,
not a command wait. Async, interrupt and ISO schedulers no longer add the IO to
the active list or dereference its private state to ring the doorbell after the
submission helper returns. The header explicitly transfers ownership on success:
the IO/private state may already have been freed by completion when it returns.

This also clears stale io_Error/iouh_Actual when a retry is actually published.
Payload submission rejects short cache preparation before writing descriptors,
as control already did. Failed preparation/admission performs no list insertion
or doorbell and balances only the unpublished buffer's cache ownership.

`make test-xhci-ring` PASS under ASan/UBSan: existing 1024 control + 1152 payload
admission cases, failure injection for both preparation wrappers, and immediate
completion of control/bulk/interrupt/ISO publication. The actual helper source
runs; mocked AddTail/doorbell assert the correct active list, indices/length,
completion state and ISO flags are ready while the outer lock is held. At the
first outer unlock the model removes/frees IO and private state; ASan verifies
the helpers do not access them on return. This is a publication-level ISO model,
not an RT-ISO scheduler or hardware qualification.

`make test-xhci-request` PASS with actual async and interrupt scheduler source:
previous rollback/retry tests plus a submission mock that completes and frees
both records before returning success. Control, bulk and interrupt success paths
return without use-after-free, double insertion or a second doorbell. No real
controller concurrency is claimed by these synchronous injection tests.
Guest driver incremental cross-build PASS. Exact hashes:
artifacts/xhci-publication.json. Review238 approved the preceding rejection fix.

Next: move active AbortIO/NAK/flush cancellation into task context, stop the
endpoint and retire references with acknowledged commands before releasing DMA;
retain memory on failed stop/reset. Audit iteration over pending queues against
concurrent removal as part of request-state work. Then integrate explicitly
owned PCI payloads without double translation, validate segment address rules
and controller register bounds/interrupt admission, and test real VZ USB input.
No USB transfers attempted yet. SMP unchanged; stage-six goal remains active.


## Stop observations preserve request ownership (core cbb3fcac84)

The cancellation audit found a concrete additional release hazard: the event
handler excluded Stopped codes only from fallback-to-busy correlation. A request
resolved through ringio still reached xhciIntWorkProcess, which cleared its
correlation, released the payload and marked it complete. The transfer-event
branch now consumes all three Stopped codes before request processing; the
completion function itself also rejects them defensively. The accumulator uses
OR for transfer completion work, retaining work already seen in the same event
batch. Remaining fallback comments no longer assume stopped requests were freed.

Reference checked directly: [Intel xHCI 1.2b, sections 4.6.9–4.6.10](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf).
Stop preserves resumable state; Set TR Dequeue discards cached transfer state.
Ring edits require command acknowledgement. This rules out resetting dequeue
indiscriminately while an unrelated ISO transfer is partially executed.
Board240 records the cancellation design discussion and corrections to code/order
assumptions; no native host-xHCI cancellation implementation exists to reuse.

The existing completion body is extracted into xhci_completion.inc, included
unchanged apart from the guard by the driver and host test. Its AROS provenance
is preserved. `make test-xhci-completion` PASS under ASan/UBSan: 36 combinations
of control/bulk/INT/ISO, three stopped codes (26/27/28), and residual values
including UINT32_MAX. Every stopped observation leaves the full IO/private
record and ring ownership unchanged and calls no DMA release. A later success
still releases once with correct actual length and clears only matching owners,
including wrapped data indices. A late stopped observation cannot overwrite the
completed result; missing/private-less requests are covered too. This tests the
actual completion function, not a full interrupt-controller or cancellation run.

Also removed debug logging from out-of-line xhciRingDoorbell per review239;
the inline form was already MMIO-only. Both remain suitable for the publication
critical section. Guest driver rebuild PASS after these changes. Exact hashes:
artifacts/xhci-stopped-completion.json. Review239 approved publication ordering.

Next implement command-owner cancellation: deferred AbortIO/NAK/flush handling,
endpoint submission exclusion during stop, stopped/dequeue acknowledgement,
retirement of only the cancelled request while preserving unrelated TDs, and
retention after command timeout. Existing immediate abort/free paths remain
unsafe and are NOT declared fixed by filtering stopped events. Owned PCI payloads
remain unwired and no VZ USB traffic was attempted. Goal active; SMP untouched.


## Stopped-ring retirement primitives (core bc42b1c88d)

Added prepare/commit cancellation helpers beside the actual ring code. Their
caller MUST already own an acknowledged stopped endpoint and hold the ring lock,
excluding all producers/recovery until commit/resume. They do not issue commands,
free buffers or reply, and are not yet connected to AbortIO.

Prepare validates the entire first..last request range and matching ring owners
before changing anything. Link TRBs stay untouched; only target TRBs become
zero-length/address No-Ops with their original cycle bits. Other request data,
flags and ownership are preserved. If the stopped position belongs to this
request, the plan advances past its last descriptor, computes the next DCS across
wrap/Link, and requires a Set TR Dequeue acknowledgement. Otherwise it leaves
the stopped position alone so an unrelated partially executed TD can resume.
Commit revalidates all owners/types before clearing any correlation and refuses
a required but unacknowledged dequeue change. It updates the software consumer
position only when dequeue was actually moved. These checks do not themselves
prove a hardware stop: that belongs to the forthcoming command owner.

The final replacement No-Op has IOC. Without an event, cancelled entries at the
end of a ring could strand software capacity indefinitely. The transfer-event
handler now recognizes a successful ownerless No-Op before busy-request fallback,
keeps the normal ring-end update and wakes the scheduler without completing an
unrelated IO. Host tests check the IOC descriptor; this IRQ dispatch branch still
needs live/controller-level qualification.

`make test-xhci-ring` PASS with ASan/UBSan: 672 cancellation combinations on the
8-slot ring model (every start/count, stop position including Link, and initial
cycle). Tests preserve all non-target descriptors and Link bytes, verify DCS by
walking across the physical Link, retain every owner after denied acknowledgement,
reject changed ownership without partial commit, and reject malformed range,
stop index or cycle before mutation. Earlier 1024 control and 1152 data admission
cases and immediate-completion publication tests also pass. Guest driver build
PASS. Exact source/test/device identities: artifacts/xhci-cancel-ring.json.

Next wire an endpoint command owner in the event task: mark active cancellations
without replying/freeing in interrupt context, exclude submissions/recovery during
stop, wait for Stop and conditional Set Dequeue completion, invoke these helpers,
then clear/release/reply only after retirement. Completion while cancellation is
pending must retain the request; device/endpoint teardown needs the same lifetime
coordination. Command timeout must retain state and prevent endpoint reuse.
RT-ISO stop/flush call sites need their own completion ownership treatment.
Existing immediate AbortIO paths remain unchanged and unsafe; no live USB traffic
has been attempted. Full stage-six goal remains open; SMP untouched.

## Annulation xHCI : transaction de commandes avant libération

- `xhciRetireCancelledRequest` ajoute la séquence exécutée en contexte tâche :
  Stop Endpoint (Running/Stopped), ou Reset Endpoint (Halted), puis lecture du
  contexte de sortie après acquittement. État Stopped obligatoire ; l'adresse
  de dequeue et DCS viennent du contrôleur, pas du dernier événement USB.
- Le dequeue doit désigner le segment connu, y compris son Link final. Les
  adresses hors segment, les bits de stream, les indices invalides et les
  débordements d'adresse sont refusés. La commande Set TR Dequeue utilise
  `dmaa_DMA`, déjà traduit, sans le retraduire comme une adresse CPU.
- Le retrait prépare les No-Ops, attend Set TR Dequeue si le transfert annulé
  est courant, puis valide le commit des corrélations. Un refus ou timeout
  retourne FALSE : conserver requête/buffers/anneau et mettre l'endpoint en
  quarantaine. Un endpoint Error reste également en quarantaine ; aucune
  récupération automatique de cet état n'est revendiquée.
- Le code ne libère ni buffer ni requête et ne sonne pas la porte de reprise.
  Son appelant doit conserver leurs références et exclure publication,
  récupération, déconnexion et libération par completion pendant les attentes.
  Les interruptions restent actives pendant les commandes.
- `make test-xhci-cancel` compile les vrais fichiers `xhci_cancel.c` et
  `xhci_ring.c` sous ASan/UBSan. 4 032 cas couvrent positions, cycles, wraps,
  contextes 32/64 octets, états Running/Stopped/Halted, plages contrôle/data.
  Injections supplémentaires : timeout/refus Stop et Set Dequeue, état final
  incorrect, dequeue hors segment, flags stream, propriétaire perdu avant
  commit, entrées invalides. Aucun de ces échecs n'autorise une libération.
- Référence : [xHCI 1.2b, sections 4.6.8–4.6.10](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf).
  Revue 242 des primitives précédentes approuvée ; ses remarques sur le
  contexte de sortie et la vérification de Stopped sont intégrées ici.
- **Pas encore raccordé à AbortIO** : les chemins existants de NAK/flush/ISO
  et de déconnexion doivent être convertis avec la même propriété exclusive.
  Prochaine étape : état d'annulation conservé dans la requête et verrou de
  durée de vie par endpoint ; les completions concurrentes enregistrent leur
  résultat sans libérer pendant la transaction. Aucune preuve de transfert
  USB VZ ni de clavier/souris n'est ajoutée par ce test hôte.

- Compilation AArch64 `kernel-usb-pcixhci` PASS. Core `c74aa4aa8d`,
  identités exactes et périmètre : `artifacts/xhci-cancel-command.json`.

## Fins de transfert xHCI : observation IRQ, libération par la tâche

- Core `2d4390911e` : `xhciIntWorkProcess` enregistre le premier code terminal
  et son résiduel dans le privé de la requête. Pour les transferts munis de ce
  privé, l'IRQ ne copie/libère plus le payload et n'efface plus `ringio`.
  Les observations Stopped/Invalid et les événements terminaux suivants
  n'écrasent pas le résultat enregistré.
- `xhciHandleFinishedTDs` appelle le consommateur `xhciRetireCompletion` dans
  ses chemins périodique et asynchrone avant de rendre le résultat au client.
  Le consommateur retire les références et le payload une seule fois. La
  publication remet explicitement à zéro le résiduel et le marqueur de retrait.
- Le chemin historique de commande asynchrone sans privé conserve son résultat
  immédiat ; les commandes synchrones passent par `xhc_CmdResults`.
- `make test-xhci-completion` PASS sous ASan/UBSan : 36 observations d'arrêt
  conservent requête/privé/payload/corrélations ; 96 cas terminaux couvrent les
  quatre commandes, IN/OUT, succès/short/stall/underrun et zéro/partiel/complet.
  Avant consommation par la tâche, aucune libération ni modification des
  corrélations ; les doublons ne changent pas le premier résultat et une
  consommation répétée ne libère pas deux fois.
- `make test-xhci-ring` PASS, y compris remise à zéro des marqueurs avant
  la porte de publication et completion immédiate des quatre commandes.
  Compilation AArch64 `kernel-usb-pcixhci` PASS.
- Cette modification est branchée dans le traitement normal des transferts,
  mais **ne corrige pas encore AbortIO/NAK/flush/RTISO ni la déconnexion**.
  Le maintien exclusif de la requête pendant la transaction d'annulation reste
  à raccorder. La tâche et les callbacks de teardown peuvent encore se
  concurrencer ; ce résultat ne qualifie ni ce cas ni l'entrée USB sous VZ.
- Revue 243 de la transaction de commandes approuvée. Evidence précise de
  cette étape : `artifacts/xhci-task-completion.json`.

- Le consommateur conserve un `Disable/Enable` équilibré pendant le retrait
  du payload : les anciens chemins AbortIO/NAK ne partagent pas encore le
  propriétaire tâche. Cela préserve leur exclusion préexistante avec la
  libération IRQ et évite une nouvelle course introduite par le déplacement.
  Aucun gain de latence IRQ n’est revendiqué tant que cette exclusion reste.

## Première ouverture réelle du pilote xHCI sous VZ

- Essai isolé `artifacts/vz-xhci-open-first` : FAIL à OpenDevice, avec core
  `2d4390911e`. Le PCI publie bien Apple 106b:1a06, classe 0c0330, avec MSI et
  MSI-X ; le pilote rejetait systématiquement INTLine=255 à l'énumération.
- Core `f1d798a010` supprime cette condition héritée d'INTx. L'installation de
  l'interruption par le propriétaire PCI doit réussir avant de lancer le
  contrôleur. En cas de refus, l'initialisation échoue et retire le callback
  de reset installé avant cette tentative.
- `artifacts/vz-xhci-open-msi` : PASS VZ réel, 6,43 s. `pcixhci.device` ouvre
  son unité et UHCMD_QUERYDEVICE retourne deux attributs, USB30 dans les
  capacités (0x80000006) et version de pilote 0x300. Les gates antérieurs
  PCI, MSI, virtio block, GPU et démarrage restent PASS. Bundle
  `996c6d03cb72f4ae2646ca39f7c598f6a085f87fed3902fd23a5d4aeeff60bed`,
  disque `edfa574d7786d3b8e8f6d039356e2d4b71c6e9bb56ae96735e7d58274f3c9314`.
- Ce n'est pas une preuve de transfert USB ni de saisie. Le test garde le
  pilote ouvert jusqu'à la fin de la VM ; son teardown n'est pas qualifié.
- Le packager propose `--xhci-driver` et `--xhci-enumerate` ; le runner a les
  exigences séparées `--require-xhci-driver` et `--require-xhci-enumeration`.
  `artifacts/vz-xhci-enumeration-missing-proof` échoue volontairement après
  12 s avec l'image d'ouverture seule : les marqueurs d'énumération manquants
  ne sont pas remplacés par un simple démarrage réussi.
- Choix de progression : garder ouvertes les corrections de durée de vie
  AbortIO/NAK/flush/RTISO/disconnect, mais faire les premiers essais bornés sur
  une VM et une image isolées pour identifier les blocages effectifs du chemin
  clavier/souris. Ne pas présenter ces essais comme une qualification de
  robustesse ; toute qualification prolongée exige encore ces corrections.

## Énumération USB VZ : verrou DMA appelé depuis une interruption

- `vz-xhci-enumeration-first` atteint l'attachement Poseidon et le hub racine,
  puis échoue avec une alerte Exec de sémaphore en mode superviseur.
  `vz-xhci-enumeration-context2` confirme EL0 avant/après le scan ; le premier
  essai de cette instrumentation (`context`) avait lui-même omis d'ouvrir
  kernel.resource, corrigé avant de tirer une conclusion.
- `vz-xhci-enumeration-stack3` conserve la pile avant Alert. Le registre des
  allocations DMA de VMPci utilisait un sémaphore partagé dans CPUtoPCI /
  PCItoCPU, alors que xHCI appelle ce chemin en interruption. L'hypothèse
  initiale OOP_FindClass était incorrecte : la lecture du code et des offsets
  du binaire identifie le registre DMA.
- Core `0c0f71b699` protège publication, retrait et recherche avec des sections
  Disable/Enable brèves. Allocation, protection des pages et libération restent
  hors section. Contrat explicitement monocœur ; le propriétaire doit arrêter
  ses utilisateurs DMA/IRQ avant de libérer le buffer. Le SMP reste à bsek.
  Le dump temporaire de pile est retiré ; Exec conserve les adresses caller et
  stack dans ses diagnostics de sémaphore (`8a5e701121`).
- `make test-vmpci-dma` PASS sous ASan/UBSan : code réel, non-identité CPU/bus,
  bornes, libération, refus non-contigu, traduction avec masquage déjà imbriqué.
  Build croisé Exec + VMPci PASS. Revue 246 demandée.
- `vz-xhci-enumeration-irq-dma` reste FAIL : plus d'alerte de sémaphore, mais
  arrêt VZ inattendu immédiatement après le scan, sans exception invitée
  enregistrée. Aucun descripteur enfant ni événement HID validé. Examiner
  ensuite les adresses DMA et les transferts ; le helper de payload PCI-owned
  existe mais n'est pas encore raccordé. Cette piste n'est pas un diagnostic
  établi du nouvel arrêt.
- Régression `vz-xhci-open-irq-dma` PASS, 7,28 s : ouverture/query xHCI et
  gates PCI/MSI/block/GPU/démarrage. Bundle
  `68d0fc614781c546c43c65895c595c90d872111c4977b24dd4caf534aeaa2df1`,
  disque `06277b86e450ef613e052848ed487e1212d93d1a1b566af3e78bf104320f5ec9`.
  Les étapes 3 et 6 ne sont pas validées. Aucun essai matériel M1 ou Pi.

## Deux périphériques USB énumérés sous VZ

- Core `2e5bf1debe` raccorde les payloads PCI-owned à la préparation, à la
  libération après complétion et au rollback non publié. La primitive garde
  son résultat bus pour validation ; le wrapper retourne le buffer CPU du
  cookie. InsertTRB effectue donc exactement une traduction CPU → bus.
  Les wrappers sont regroupés dans xhci_dma.c pour tester le vrai code.
  Le chemin HCF_PLATFORM reste distinct. Aucun nouveau support SMP.
- Ce raccordement ne résolvait pas l'arrêt VZ : les traces
  `vz-xhci-enumeration-event-trace`, `address-trace`, `port-trace`, puis
  `port-write` montrent les commandes Address Device achevées avant l'arrêt,
  sans publication d'un TRB de données. L'écriture précise fautive est
  ClearFeature(C_PORT_ENABLE=17), PORTSC 0x500014a0 : valeur lue 0x603,
  valeur écrite 0x40200 ; l'écriture ne revient pas.
- Core `de67f956d2` limite les bits RW1C demandés aux bits observés dans
  PORTSC. L'IRQ peut avoir déjà acquitté le matériel tout en conservant
  l'événement dans la carte logicielle pour Poseidon ; cette carte est bien
  acquittée indépendamment. VZ n'accepte pas l'acquittement PEC redondant
  observé ici. Ce résultat ne décrit pas le comportement d'un contrôleur
  physique. Référence de registre consultée : [Intel xHCI, PORTSC](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf).
- `vz-xhci-enumeration-observed-ack` PASS 8,14 s avec traces. Core final
  `fc39873111` retire toutes les traces temporaires ;
  `vz-xhci-enumeration-clean` PASS 7,42 s, descripteurs enfants
  05ac:8105 et 05ac:8106. Gates PCI/MSI/bloc/GPU/startup également PASS.
  Bundle `124c51839ee24f68da9037b1e517ce2fd0ff21c17050fe32051dcbe029fa2ffd`,
  disque `a28ed77be5569aa3b264cf25c2e85c3b0f4f4b959b7a1d3cb6e75e6e67087a93`.
- ASan/UBSan : tests xhci-dma, ring, request et completion PASS. Le test DMA
  exerce les vrais wrappers, le retour CPU distinct du bus, copie IN partielle,
  rollback sans copie IN et requête sans données. Publication/indices de ring
  et complétion restent couverts par leurs tests existants.
- Revues 246 et 247 approuvées ; revues 248 (payload) et 249 (PORTSC) demandées.
  Prochain chemin d'usage : charger la classe HID, recevoir les événements
  clavier/pointeur et les livrer au bureau VZ. Les deux descripteurs ne prouvent
  pas cette livraison. AbortIO/NAK/flush/RTISO/déconnexion restent ouverts avant
  qualification prolongée. Réseau, clipboard et échanges restent à réaliser.

## Classe HID chargée dans le bureau VZ

- Build existant `kernel-usb-classes-hid` terminé. Le profil opt-in
  `--profile wanderer --virtio-gpu --usb-desktop` place Poseidon, xHCI,
  hub/hubss/hid et la commande VMUSB dans SYS (1,44 Mio, 12 fichiers).
  La Startup-Sequence exécute VMUSB, G4Ready puis EndCLI. La génération FAT
  accepte un script explicite avec Wanderer ; le script par défaut reste testé.
- L'essai d'ajout des cinq modules USB aux résidents était refusé par la preuve
  exacte de profil (vz-usb-desktop-first, 0x3ff). Ce montage a été retiré :
  les classes sont chargées normalement depuis DOS, sans changer le profil
  résident ou sa validation.
- `vz-usb-desktop-dos` expose une course (0x90b) : le lanceur de bureau à
  priorité -1 démarre dès que la commande avant G4Ready attend une IO.
  Core `c7008b3a4c` attend un signal explicite SIGBREAKF_CTRL_F ; G4Ready
  publie le drapeau puis signale. Cela traite aussi le signal reçu avant Wait.
  La priorité seule n'est plus considérée comme une barrière de démarrage.
- `vz-usb-desktop-ready-signal` PASS 2,36 s : deux périphériques enfants
  connectés/configurés avec interface liée à la classe HID effectivement
  chargée, puis Wanderer/foreground et contrat de démarrage. Bundle
  `824698847e93fbba4642494063679f7225e9b02f94f52ec7daaf6b3f40489999`,
  disque `64cbe4425cc9f7f4e39769992dcab59c30b73eb01124a52409552ac38acbb7a4`.
- Régression bureau standard sans VMUSB `vz-desktop-ready-regression` PASS
  1,72 s. Build du noyau PASS avec avertissement préexistant de type de
  pointeur ligne 1047 ; aucune modification liée à cet avertissement.
- Le runner `--require-usb-desktop` exige les bindings et le desktop ; sa
  preuve est distincte des événements, `input_verified=false` conservé.
  VMUSB garde Poseidon ouvert : les classes et tâches actives lui appartiennent.
- Prochaine étape indispensable : une session de bureau continuellement
  ordonnancée et l'injection/réception réelle d'événements dans une fenêtre.
  Le profil de preuve actuel retourne au chargeur après son verdict ; un
  bureau dessiné et un binding HID ne constituent pas une session interactive.
  Le mécanisme existant `open_shell_and_hold` utilise des propriétés chosen
  desktop-hold, absentes du FDT VM ; examiner boot/arosboot/vz_platform.c et
  les modes du lanceur pour séparer exécution normale et qualification bornée.
  Pas de SMP, ni d'essai physique M1/Pi. Revue noyau 250 demandée.

### Revue de la barrière de démarrage et tests négatifs

- La revue 250 identifie une limite de c7008b3a4c : sans G4Ready, une attente
  sans timer peut rester bloquée. Le code natif ab97ed7453 traitait déjà ce cas.
  Core final 95f8c95032 reprend son helper wait_for_initial_cli et l'ordre
  drapeau/priorité/signal de G4Ready, en préservant les adaptations VM.
  Le helper attend le signal ou le timer et échoue si Boot Mount disparaît
  sans avoir annoncé la disponibilité. Le commit initial n'est pas le résultat
  final qualifié.
- `vz-usb-desktop-native-wait` PASS 2,29 s avec ce noyau : démarrage du bureau
  et deux bindings HID. Bundle
  `89def052eb4f9879f7aafa285bb58bff285bbd0f507cdf717e31ad5d61743af5`,
  disque `16b1ab4fae5009bfc2da1f6579c295e4231fde842b3a0402f978ff3461eeafb8`.
- `vz-desktop-no-g4ready` : échec attendu en 1,74 s, C_DETAIL=0x90b,
  script EndCLI sans G4Ready, plutôt qu'un timeout. `vz-desktop-no-wanderer` :
  échec attendu en 2,43 s, C_DETAIL=0x529 et vecteur d'exception nul.
  Ce second fixture réutilise le mutateur FAT12 de l'intégration pour retirer
  l'entrée Wanderer ; bundle et BOOT0.ID recalculés, provenance négative
  distincte sous build/desktop-no-wanderer/negative-provenance.json.
- `vz-usb-desktop-missing-proof` : le bureau standard dépourvu de VMUSB
  ne satisfait pas la gate HID (timeout attendu à 6,11 s).
- Ces tests portent sur VZ mono-cœur ; ils ne valident ni des événements
  clavier/souris, ni une session prolongée, ni le matériel physique.

## Session VZ maintenue après le lancement du bureau

- Le chargeur lit une politique optionnelle AROS/SESSION.CFG sur son ESP :
  secondes 1..3600 ou forever, suivies d'un seul saut de ligne. Traduction
  vers les propriétés chosen déjà utilisées par open_shell_and_hold ; pas de
  nouveau mécanisme noyau, pas de changement SMP. Absence = preuve de boot.
  Les propriétés sont des chaînes décimales, conformément au lecteur noyau.
- `--desktop-session` au packaging et `--require-desktop-session` dans le
  runner rendent ce chemin reproductible. Le runner exige shell, début/fin
  de période, nombre de ticks, durée hôte et contrat normal de démarrage.
  Les événements et pixels restent explicitement non vérifiés.
- `vz-usb-desktop-session30` PASS 32,37 s : shell et fenêtre alive ouverts,
  input.device ouvert, 15 ticks, hold complete, puis contrat de démarrage.
  Core95f8c95032 inchangé. Bundle89def052eb4f9879f7aafa285bb58bff285bbd0f507cdf717e31ad5d61743af5.
  Disque c3f1205e67ccf8b2bad6ed595bc39041a0943d502764520492c8b4743a76643d.
  EFI e6b21692a8a4b838e880c2cee89ff116a121b7e63e48fd42cd791ba77b53cc55.
  Ce premier package précède l'ajout du hash SESSION.CFG à inputs ; sa valeur
  desktop_session=30 et l'identité du disque sont enregistrées.
- Sans politique, `vz-usb-desktop-session-default` PASS 2,50 s. La gate
  exigeant 30 s sur cette même image échoue explicitement en 2,35 s
  (`vz-desktop-session-missing`, desktop-session-not-run).
- Revues250/251 approuvées. La remarque251 est traitée : une image sans
  VMUSB échoue désormais dès desktop stable, raison desktop-without-hid-bindings,
  en 1,64 s (`vz-usb-desktop-missing-explicit`), sans attendre un timeout.
- Tests ASan/UBSan du parseur/FDT : valeurs limites, entrées malformées,
  absence, propriétés bounded/forever exclusives ; PASS après correction
  d'une double déclaration locale dans le test. Build EFI -Werror PASS.
- Limites : 30 s de session ordonnancée n'est pas une qualification prolongée.
  Pas de frappe ou mouvement hôte observé dans une fenêtre invitée. Le mode
  forever est exposé pour l'usage interactif mais n'est pas qualifié comme
  stable à long terme. Prochain travail : observation et livraison des entrées
  réelles, puis durées longues/annulations USB. M1 et Pi non utilisés.
- Suite `make test` complète PASS (artifacts/desktop-session-host-tests.log),
  y compris ABI x18, FDT/early-FDT, GIC, PCI et virtio ; pas de test physique.

## Entrées VZ jusque dans une fenêtre Intuition

- Fixture VMInput (commande DOS MIT) lancée via Run dans Startup-Sequence,
  après VMUSB ; fenêtre active après démarrage du bureau. Observation IDCMP
  uniquement, aucune injection dans input.device. Deux mouvements vérifiés
  à ±2 pixels, bouton gauche down/up et touche A raw down/up sont obligatoires.
- Le runner attend `input observer ready`, puis crée une requête dans le
  répertoire d'état isolé. Le lanceur envoie des NSEvent à sa propre vue VZ,
  sans publication globale. L'option --require-input impose cette preuve
  et le contrat de session complet ; --no-input-events sert de contre-épreuve.
- Premier essai `vz-desktop-input-v2` FAIL31,07 s : touches0x20/0xa0 reçues,
  aucun mouvement/bouton. Traces HID temporaires localisent la perte après
  réception : rapports pointeur de20octets présents, mais carte du rapport1
  limitée à5champs (molette/pan/vendor), champs X/Y/boutons absents.
- Descripteur réel conservé dans `vz-desktop-input-trace4/console.log`.
  Changement report1 → report2(feature) → report1 dans une collection imbriquée.
  nParseReport ignorait les ancêtres quand la feuille était vide et parcourait
  le parent du nouveau clone au lieu de l'ancienne collection. Correctif générique
  5ed1c1d692 : sauvegarde et clone toute la hiérarchie ouverte à chaque changement.
  Le test avec traces `vz-desktop-input-parent` PASS32,52 s.
- Le recadrage tablette5..95% décalait aussi les coordonnées des souris absolues.
  6f639b6c24 le désactive pour les collections Mouse, conserve les réglages
  Tablet et le chemin relatif. Ce n'est pas une exception VID/PID pour VZ.
- Core final6f639b6c24 ne contient plus les traces temporaires (retirées dans
  e39b57b88f). Build HID final PASS sans avertissement ; la première trace
  temporaire avait deux avertissements de format, corrigés avant retrait.
- `vz-desktop-input-clean` PASS32,52 s : messages aux positions relatives
  (199,199) puis(299,249), codes boutons0x68/0xe8, touches0x20/0xa0 ; fenêtre
  fermée, puis fin du hold30s et contrat startup. Bundle
  7cd0da732de86612859b0164a98c78a4f37c0cf38dc18f46ef626959fa6c7f89,
  disque f0550eb02943b54312035235ceb3c0c491ebffeccbe34dedf31ceb9c8dacb72f,
  lanceur724f99b31ffaf7c3d6c18aa0648894040ab4b5afe2ee60ae3c4297bbab235b7c.
  input_verified=true signifie ici chemin événement synthétique hôte → VZ USB
  → xHCI/Poseidon/HID → fenêtre Intuition, pas clavier/souris physiques.
- Bibliothèque VZ utilisée par API publique :
  https://developer.apple.com/documentation/virtualization/vzvirtualmachineview
- Limites : pas de capture pixels VZ validée, pas de durée longue ni de teardown
  USB qualifié. La table de tags tablette existante utilise ULONG au lieu
  d'IPTR : audit64bits à poursuivre pour les attributs tablette étendus.
  Pas de changement SMP, pas d'essai M1/Pi. Réseau/clipboard/fichiers restent
  dans l'objectif complet. La prochaine étape ne doit pas recompter ce seul
  scénario d'entrée comme qualification prolongée.
- Contre-épreuve `vz-desktop-input-withheld` : même disque/lanceur, requête
  d'événements hôte retenue, échec attendu30,65 s avec `input observer missing
  events`, aucune preuve d'entrée. Pas de faux PASS par simple fenêtre ouverte.

## Attributs HID tablette : validation réelle en 64 bits

- Core1e814b029e remplace le tableau ULONG[20] et son curseur par de vrais
  TagItem[10]. Construction typée des tags/données, terminateur complet.
  Maximum8attributs + terminateur ; pas de changement de taille en32bits.
  Le cast d'un tableau32bits en TagItem64bits combinait tag et valeur et
  perdait notamment ButtonBits quand le bouton était pressé.
- Le test VMInput demande WA_TabletMessages et lit TABLETA_ButtonBits avec
  GetTagData avant de répondre au message. Il impose maintenant1 puis0,
  en plus des mouvements et codes clavier/boutons. Les bibliothèques ouvertes
  sont fermées sur les chemins d'erreur comme sur le chemin normal.
- `vz-desktop-tablet-after` PASS32,66 s : attributs0,0,1,0, entrées et session.
  Bundle15e8128adbbbbc7994dde498ddb3851b959afdf0756099fdc7c010700cb51d27,
  disque78e357cc451421324f08b4c7d3e96c4aa49075f0697555163fae7a36168c9bf5.
- Contre-épreuve avant/après : même observateur et modules ; remplacement
  du seul SYS:Classes/USB/hid.class par le binaire6f639b6c24
  (sha68a5670005e696fe36977e9c15eabfca43c3cf3ce3ee201d25bc348e8a6d96da),
  bundle/BOOT0.ID recalculés avec provenance de mutation distincte.
  `vz-desktop-tablet-old-hid` échoue explicitement30,62 s : ButtonBits reste0
  même pendant le clic. Aucun changement du driver injecteur hôte.
- Build HID final PASS, sans avertissement ; commande observatrice -Werror
  PASS après correction de sa déclaration UtilityBase. Pas de test tablette
  physique ni de validation des axes/pression étendus, mais layout partagé
  corrigé et attribut réellement cloné/livré par Intuition en AArch64.
- Reproductibilité : clone figé aros-core-vm-repro-20261001 et sorties neuves
  repro-a-20261001 / repro-b-20261001 sous ~/Build/macaros-vm. Ne pas confondre
  leur lancement avec une comparaison achevée ; résultats à consigner après
  les builds et la comparaison binaire effective.

## Configuration réseau VZ et échec de reconstruction indépendante

- Ajout opt-in --network au lanceur : virtio-net avec attachement NAT,
  adresse MAC locale unicast aléatoire conservée dans le répertoire d'état.
  Pas de pont réseau ni de changement d'entitlements. API Apple documentée :
  https://developer.apple.com/documentation/virtualization/vznatnetworkdeviceattachment
- Probe PCI existant étendu : découvre1041, vérifie le bit MAC offert et
  la génération de configuration, lit les6octets via MapPCI, restaure sélecteur
  et commande PCI. Le runner compare exactement avec l'adresse annoncée par
  le lanceur. Ce n'est pas un driver SANA-II ni une preuve de paquets.
- `vz-network-config` PASS1,63 s. Bundle
  39fb2565cb45317899941ba6f6765f263302f7221c14a41742f1c96c0ce9cc7e,
  disque fde92b2bdb8ce8928b3bd6d80b7970ce6fb52e5bad4f8be36ec1cfd1dd8bb33f,
  lanceur87e436bd4b27df54c2a720597c7f78d5ea23b739ea37c75c05e2c73c3f1c72b6.
  Sans réseau, `vz-network-disabled-regression` PASS1,60 s.
  `vz-network-identity/verdict.json` : deux boots1,37/1,38 s reprennent
  02:aa:bb:cc:dd:ee côté invité ; valeurs malformée/multicast refusées avant boot.
- ReproA figé1e814b029e échoue229,75 s : SIGBUS du genmodule hôte pendant
  graphics.library. Rapport macOS genmodule-2026-10-01-000749.ips, pile
  writeincdefines.c147 → strcmp, lecture3octets avant une allocation5octets
  contenant Text. La recherche TagList faisait strlen(name)-7 sans borne.
- Test réel ASan/UBSan `genmodule-short-before` reproduit l'overflow sur
  graphics.conf et un fixture de noms1..6caractères. Les deux émetteurs
  defines/inline avaient le même défaut. Après borne de longueur, UBSan révèle
  aussi le VLA argtypes[0] pour les APIs sans argument ; taille minimale1.
  Core03275b8c5e corrige les deux émetteurs. `genmodule-short-fixed` PASS
  sur graphics et fixture, sans erreur de sanitizer ; les avertissements
  préexistants de compilation du générateur sont conservés dans compile.log.
- Le buildB initial utilise encore le clone1e814b029e, laissé intact pendant
  son exécution. Aucune conclusion de reproductibilité à partir de cette paire.
  Une nouvelle source figée aros-core-vm-repro-fixed-20261001 à03275b8c5e
  et deux sorties neuves repro-fixed-a/b-20261001 préparent la reprise.
  Ne pas réutiliser les objets de la tentative échouée pour la preuve finale.
- Suite réseau : réutiliser transport/DMA/IRQ virtio, ajouter le chemin
  RX/TX et l'interface SANA-II pour la pile existante. La négociation actuelle
  virtio_hw_negotiate_features contient des masques spécifiques au bloc :
  les séparer avant de la réutiliser pour le réseau. Pas de SMP/M1.


## Virtio-net : premier aller-retour réel avec le NAT VZ

- Core `dfdfdd688a` (série depuis `e0523981fb`) : transport RX/TX réutilisant
  mapping PCI, reset borné/confirmé et allocation des queues virtio existants.
  Deux queues de8, buffers DMA propres2048octets, une émission en vol copiée,
  recyclage RX après copie ; vérification des indices, IDs, doublons et longueurs.
  Close ne libère rien si le reset n'est pas confirmé. API à propriétaire unique,
  contexte tâche, polling explicite. Pas encore de device SANA-II résident.
- Les masques de features et les bornes de configuration bloc8/20/24octets,
  alignement8, sont dans le contrôleur bloc. Le transport commun ne doit pas
  interpréter des numéros de features selon le type disque pour une carte réseau.
  Le test réel du constructeur couvre tailles0..25, adresses alignée/non alignée,
  échecs de démarrage, reset refusé et rétention du bus publié, sous ASan/UBSan.
- Première ouverture refusée par VZ : features acceptées candidates100000020,
  status03 après tentative FEATURES_OK. Configuration réseau17octets, offre
  basse300119ab/haute5 ; la borne bloc8octets n'expliquait donc pas ce refus.
  `vz-network-packets-negotiation` isole chaque ajout sans queue ni DRIVER_OK :
  bit3(MTU) accepté, bits16/28/29/34 isolés refusés. Pas besoin de packed rings.
  Le transport négocie MTU et lit MAC/MTU dans une génération stable ; MTU1500
  observé. Les limites de trame suivent le MTU ; valeurs >1500 non supportées.
- Particularités VZ mesurées (`vz-network-packets-completion`, `...-rx-header`) :
  num_buffers=0 sans MRG_RXBUF, flags6 sur des trames reçues sans offload négocié.
  Virtio1.2 demande num_buffers=1. Tolérance explicite0/1 seulement dans ce mode
  où un descripteur contient toute la trame ; aucune chaîne multibuffer.
  Les indications checksum/RSC sont ignorées et les checksums sont vérifiés par
  le consommateur. NEEDS_CSUM reste refusé, ainsi que GSO et num_buffers>1.
  Référence : https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html
- Probe DHCP (RFC2131) : broadcast DISCOVER, identifiant de transaction dérivé
  du compteur invité, réponse OFFER portant la même MAC/transaction, adresse
  proposée non nulle, bornes IPv4/UDP/options, checksum IPv4 et UDP si présent.
  Il n'acquiert pas un bail et ne remplace pas AROSTCP. Le runner exige aussi
  toute la séquence de démarrage, la MAC persistée et exactement un marqueur.
- `vz-network-packets-qualified` PASS1,29s. Bundle
  c02a5d12e5f750c98924aa207e10956af9e338930c10d25d694771152fb6c0eb,
  disque66d6aa092c6c17952ab6be152d72c2d15300021c5eaae7a3ff3eb012f1ef32af,
  lanceur87e436bd4b27df54c2a720597c7f78d5ea23b739ea37c75c05e2c73c3f1c72b6.
  Commande : `python3 tools/run-vz.py --disk build/network-packets-qualified.img
  --output artifacts/NEW-RUN --require-pci-hidd --require-network-packets --timeout 30`.
- Contre-épreuve `vz-network-packets-negative` : ancienne image de configuration,
  FAIL attendu1,30s, `network-packets-not-run`. Nouvelle image sans carte réseau,
  `vz-network-packets-disabled`, PASS1,30s. Modèles de queues réelles
  `make test-virtio-net` :18scénarios ASan/UBSan, copie TX, batch RX, wrap16bits,
  MTU et complétions invalides ; ce modèle n'exerce pas l'ouverture PCI.
  Reconstruction incrémentale kernel-virtio et virtiogfx PASS, probe -Werror PASS.
- Les deux builds complets figés03275b8c5e ont terminé apple-wanderer-payloads
  puis échoué au même endroit : virtiogfx, interface/Hidd_Bus.h manquant.
  Résultats terminaux `repro-fixed-build-results.json` (1176,45/1176,76s).
  Le mmake du GPU ne déclarait que les includes graphiques, malgré le transport
  virtio qui importe les types storage/bus/PCI/timer. Core5be666ebbe ajoute
  ces dépendances et les entrées source partagées aux dépendances des objets.
  Vérification vierge en cours, pas encore de comparaison reproductible.
- Troisième paire : clone figé aros-core-vm-repro-v3-20261001 à5be666ebbe,
  sorties neuves repro-v3-a/b-20261001. Superviseur et manifestes conservés
  sous artifacts/repro-v3-*. GPU demandé en premier pour exercer directement
  ses dépendances, puis Wanderer et pilotes. Les anciennes sources et preuves
  restent intactes ; aucun objet des essais précédents n'est réutilisé.
- Suite : SANA-II + AROSTCP en réutilisant ce transport, IRQ réseau, endurance
  et chemins d'annulation/fermeture, puis clipboard et fichiers. Pas de SMP/M1.
  Aucun résultat présent ne qualifie un IOMMU ou un DMA matériel non cohérent.

- Régression combinée `vz-network-block-regression` PASS1,43s : même invité
  avec DHCP et vrai virtio.device, lecture CMD_READ terminée par MSI-X.
  Disque d5e2c8c8a1f98b51163fc37ca6d3d896e63fefb03276ea6d67d3d1d942194a18,
  bundle 9b56ee958effcc1774764ea6421f3a3cfe2ae2dad5653208ed2f5740159df2b5.
  Cela exerce aussi le constructeur bloc après déplacement des bornes de
  configuration hors du transport commun.


## Interruptions réseau et dépendances de reconstruction

- Core dad242ffe0 : routage partagé virtio_hw_route_queue_irq(queue), vérifie
  num_queues et lit le vecteur après écriture ; l'API bloc existante reste un
  wrapper queue0. Le réseau enregistre un gestionnaire PCI, route RX0/TX1 sur
  le vecteur partagé et active les notifications. Le callback acquitte ISR et
  peut signaler le propriétaire ; le traitement des trames reste en tâche.
- Contrat de vie explicite : callback/code/data restent vivants jusqu'à Close
  réussi. Reset non confirmé conserve tout, y compris le gestionnaire ; reset
  confirmé puis RemoveInterrupt, puis buffers/queues/mappings/libérations.
  Huit scénarios du code réel Enable/Close sous ASan/UBSan couvrent refus
  d'enregistrement, refus de routage0/1, double enregistrement et reset refusé
  puis réussi. Aucun test SMP ; le fournisseur PCI actuel est UP.
- `vz-network-irq` PASS2,22s, DHCP et callback réel requis avant drainage.
  Ancienne image polled `vz-network-irq-negative` FAIL attendu1,94s,
  network-irq-not-run, alors que son DHCP passait. Le runner distingue
  network_packets_verified et network_irq_verified.
- Régression combinée réseau + vrai virtio.device + CMD_READ/MSI-X :
  `vz-network-irq-block` a eu un timeout DHCP isolé9,54s, sans faute explicite.
  Pas classé comme corrigé. Ajout d'un résumé IRQ/indices de queues :
  `vz-network-irq-summary` PASS2,12s, IRQ4, TX1/1, RX8/8, failed0.
  Même binaire `vz-network-irq-sample-0..4` : cinq PASS1,83..2,33s,
  IRQ2..3, TX1/1, RX5..7 entièrement drainés. Ceci est un échantillon de boots,
  pas une endurance ni une preuve expliquant le timeout précédent. La sonde
  DHCP n'envoie qu'un DISCOVER ; la pile/client réel devront gérer les reprises.
  Image instrumentée 54c60baadb9ba369bf03b5568149132c0268b9146b5e43c8f0d17a7be54cdeb9 (bundle),
  disque5fc45c0680ca218dd3a8eb881d69c1ba1b713cc626c090c90e94ceab46d0c2fd.
  Rejouer : `python3 tools/run-vz.py --disk build/network-irq-summary.img
  --output artifacts/NEW-RUN --require-pci-hidd --require-network-irq
  --require-virtio-device --timeout 30`.
- Build kernel-virtio et probe -Werror PASS. Modèles queues18 + IRQ8 +
  constructeur/bus PASS. La prochaine intégration est le device SANA-II et
  AROSTCP. Les frontends APL tap/dwmac offrent des références réutilisables ;
  pcnet32 est GPL, son implémentation n'est pas à copier dans ce code APL.
- Revue259 : la dépendance storage/bus corrigée est confirmée par les deux
  builds v3 GPU-first à5be666ebbe, tous deux PASS avant de poursuivre Wanderer.
  Le reviewer a signalé que les headers virtio écrits à la main dépendaient
  indirectement de includes-copy. Contrairement à la prédiction de panne,
  v3 les possédait déjà : hidd stubs tire includes/includes-copy.
- Core5c177455fa rend cette dépendance directe : les deux copy_includes nommés
  kernel-virtio-includes-copy sont requis par kernel-virtio-includes ET par
  includes-copy global, pour préserver le SDK. Fresh configure séparé,
  seule cible kernel-virtio-includes-copy : PASS ; les4destinations SDK/gen
  hardware/virtio.h et hidd/virtio.h sont identiques aux sources. Preuve
  artifacts/virtio-headers-direct-verdict.json. Les v3 continuent sur leur
  source figée5be666ebbe ; aucune comparaison binaire complète encore faite.
- Comparaison partielle v3 après fin des deux cibles GPU : virtiogfx.hidd
  identique octet pour octet,51736octets, SHA256
  3942e7a54c6aeb85453a91ef4b5500bba2444d5f035e1814043bf606151cf86e.
  Preuve artifacts/repro-v3-gpu-comparison.json ; cela ne compare pas encore
  les builds complets, toujours actifs. Revue259 approuvée ; revue260 IRQ ouverte.

## Suite SANA-II et rythme des essais

John demande un ou deux essais ciblés par changement, puis de poursuivre
l'intégration ; pas de campagnes répétées pendant des heures. Les limites
restent explicites et les défauts observés seront traités à l'usage.
Réseau indiqué par John : `Apartmenthaus Berlin Mitte`, sans mot de passe.
La VM utilise VZ NAT et la connexion du Mac ; aucune association Wi-Fi
physique n'a été effectuée par ce travail.

Le pilote résident `Devs/Networks/virtionet.device` est compilé dans le core
66361f52ae : tâche IO unique, requêtes SANA-II, tampon DMA possédé par le
transport, annulation et fermeture après reset acquitté. Ce premier frontend
ne qualifie pas encore AROSTCP, les paquets via SANA, le multicast ou les
statistiques par type. La preuve transport DHCP précédente reste distincte.

La compilation a exposé un oubli de 5c177455fa : la cible globale
`includes-copy` appelle aussi une cible make locale, supprimée par le renommage.
Correction e5190e75f8 ; les quatre en-têtes copiés correspondent aux sources
(`artifacts/virtio-headers-global-verdict.json`). La compilation réelle du
frontend franchit maintenant cette étape. Le contrôle négatif VZ sur l'ancienne
image échoue correctement avec `sana-device-not-run` en 3,88 s
(`artifacts/vz-sana-negative`). Le passage positif reste à consigner.

Passage positif `artifacts/vz-sana-lifecycle` : PASS 3,53 s, deux cycles
OpenDevice/query/configuration/online/lecture pendante/AbortIO/offline/CloseDevice
au sein d'un seul démarrage. Réouverture PCI/reset réussie. Le même démarrage
valide le DHCP brut et son interruption, séparément du frontend SANA.
Disque SHA256 `c67a79b6e37e5c8fd2d9bab11d7bfa24ab5904a614249b60aff9ca61d59ad130`,
bundle `fffc374710f10044d3146274bbe324546eb33c956c091d3be7e28897038de27a`.
Commande : `python3 tools/run-vz.py --disk build/sana-lifecycle.img
--output artifacts/NEW --require-pci-hidd --require-sana-device --timeout 25`.
Un positif et un négatif suffisent pour cette étape ; compilation de la pile
existante AROSTCP lancée, journal `artifacts/arostcp-build.log`. Aucun trafic IP
AROSTCP ni bail DHCP complet n'est encore qualifié.

## Deux builds indépendants : comparaison des produits terminée

Les deux builds v3 figés sur core `5be666ebbe244fd6c7077d38870082e6d922acb8`
ont terminé leurs huit cibles avec retour 0 (1920,16 s et 2014,15 s).
`artifacts/repro-v3-build-results.json` conserve leurs étapes. Comparaison
SHA256 de tout le répertoire produit `AROS/` (SDK inclus) et du binaire stage2 :
18 069 fichiers dans chaque build, zéro absent, zéro différence.
`artifacts/repro-v3-products-comparison.json` conserve les deux inventaires.
Ce résultat exclut le lanceur macOS, l'EFI et les ajouts réseau postérieurs à
la révision figée ; il ne clôt donc pas à lui seul toute l'étape 5.

## Intégration de la pile IP existante

AROSTCP (environ 710 Kio) et dhclient (environ 1 Mio) compilent pour le SDK VM,
sans modification de leur implémentation (`arostcp-build.log`,
`dhclient-build.log`). Le profil expérimental `--network-stack` les installe
sous `SYS:System/Network/AROSTCP`, avec vn0 sur virtionet.device et DHCP.
La sonde attend bsdsocket.library puis interroge vn0 via SIOCGIFADDR ; une
adresse nulle, loopback ou IPv4LL ne valide pas ce contrôle. Aucun succès de
cette sonde n'est encore revendiqué.

L'ensemble dépasse l'ancienne image SYS de 1,44 Mo. L'expansion FAT16 de
16 Mio conserve les fichiers et vérifie leur contenu via mtype. Premier
passage VZ : échec 0x410 avant DOS, dû à une géométrie BPB dont le nombre de
secteurs n'est pas un multiple du cylindre accepté par appleboot.device.
Géométrie corrigée à 1 tête / 32 secteurs : le deuxième passage franchit ce
contrôle puis échoue 0x507 lors de l'initialisation DOS, toujours avant IP.
Artefacts `vz-network-stack` et `vz-network-stack-geometry`. La cause de 0x507
reste à établir ; aucune réussite réseau ne découle de ces essais.

Une anomalie distincte était visible via fsck : mtools produit une entrée
VFAT devant le label de volume. L'expansion la retire ; l'image
`build/network-stack-label/SYS.img` passe `fsck_msdos -n` (code 0), et tous
ses fichiers correspondent aux sources. Cette image corrigée n'a pas encore
été essayée dans VZ. Pas de troisième redémarrage aveugle : ajout de diagnostics
DOS sur les trois échecs handler / verrou du volume / signature de boot.
Compilation en cours dans `artifacts/dos-boot-diagnostics-build.log`.
Reprendre avec cette version diagnostique pour identifier l'échec restant.

## Image SYS agrandie : signature de boot retrouvée

Le build DOS diagnostique est terminé (core 1d4506efb2). Le run
`vz-network-stack-dosdiag` donne explicitement : signature rejetée sur SYS0,
erreur 205. Reproduction hôte isolée : mcopy extrait `AROS.boot` en
`AROS.boot_` sous `LC_ALL=C`, mais conserve le nom sous `en_US.UTF-8`.
La comparaison précédente des fichiers après staging ne pouvait pas détecter
ce renommage : elle comparait déjà le nom altéré. Correction : locale UTF-8,
puis contrôle des noms ET des contenus de chaque fichier extrait depuis
l'inventaire du seed avant d'ajouter les fichiers réseau.

`vz-network-stack-utf8` franchit le boot DOS, les tests ABI, PCI, DHCP brut et
SANA-II : le problème de démarrage SYS est corrigé. Il échoue ensuite au
lancement d'AROSTCP via SystemTags, qui dépend d'un Shell absent du profil
minimal. La sonde lance désormais le binaire directement avec LoadSeg et
CreateNewProcTags, un CLI et son répertoire programme explicite.
`vz-network-stack-process` démarre le processus mais n'obtient pas
bsdsocket.library (échec borné 14,87 s). L'inspection ELF révèle une dépendance
Intuition obligatoire : `__aros_libreq_IntuitionBase`, apportée par le
requester d'erreur. Celui-ci est changé pour ouvrir Intuition localement
lorsqu'une erreur doit être affichée et utiliser la console série en son
absence. Compilation dans `artifacts/arostcp-headless-build.log` ; vérifier
la disparition du symbole obligatoire puis reprendre le contrôle réseau.
Aucune adresse IP de la pile n'est encore qualifiée.

La reconstruction hôte de l'image d'avant correction de locale était bien
identique (SHA256 6b17ff7b45f25413c95e9e3b285a56f59d1d4cdc33cc85340f26d09bcce259fc,
`artifacts/network-sys-repro.json`). Cette preuve de déterminisme ne prouvait
pas la validité du nom de signature. L'image UTF-8 corrigée a pour SHA256 SYS
7d42a74bcd188af8f10bf7e0c3187b29c9192de91d61c809921ace45024be02b ; son boot réel
est la preuve pertinente pour la correction.

## AROSTCP : bail DHCP obtenu dans VZ

Core c993611142 rend Intuition optionnelle pour les requesters, y compris
le chemin panic utilisant EasyRequestArgs avec la base locale. f408e056f2
ajoute les étapes série du démarrage : elles ont localisé le second arrêt
dans log_init. Le paquet réseau inclut désormais les bibliothèques existantes
rexxsyslib, commodities et keymap nécessaires au service de journalisation.

`artifacts/vz-network-stack-libraries/verdict.json` : PASS en 2,63 s,
network_stack_address_verified=true. La pile publie bsdsocket.library,
dhclient accepte 192.168.64.2, configure le masque et la route via
192.168.64.1 ; la sonde lit cette adresse via SIOCGIFADDR. SHA256 du disque :
4afa09fe362307f012bccda4912473bf42ecc43b1877c270a49ad42836c33311.
Cela prouve le DHCP réel à travers SANA-II, pas encore les échanges généraux
par sockets, les requêtes DNS, ni le réseau dans le bureau. Le journal T: et
les inscriptions multicast IPv6 échouent dans ce profil minimal ; ces limites
ne sont pas classées comme résolues. Pas de répétition du run réussi.

John précise le réseau local : Apartmenthaus Berlin Mitte, sans mot de passe.
Aucune association Wi-Fi modifiée ; VZ utilise le NAT de la connexion hôte.
Prochaine frontière : échanges par sockets puis services de fichiers et
presse-papiers, avec un ou deux essais ciblés par modification.

## Contrôle des sockets : échec localisé après émission UDP

Ajout de tools/network-echo-server.py : écoute IPv4 sur une adresse hôte
explicite, port éphémère, défi aléatoire 32 octets et durée bornée (180 s).
Il écrit NETECHO.REQ (IPv4/port réseau/défi, 38 octets) ; le paquet accepte
--network-echo-request et run-vz --require-network-echo exige la réponse
exacte reçue par une socket connectée AROS. La sonde attend au maximum
trois fois deux secondes. Aucun service Internet ni changement de pare-feu.

Deux essais : vz-network-echo FAIL 9,00 s ; vz-network-echo-trace FAIL 9,19 s.
Le second utilise la trace temporaire core eda3f68988 : ARP de la passerelle
résolu, send=32 errno=0, trames UDP vers le Mac émises. Vérification hôte
des octets capturés : checksums IPv4 et UDP corrects. Aucune réponse UDP
n'est observée et le serveur ne reçoit pas le défi. La cause du défaut de
livraison hôte/retour n'est pas prouvée. Pare-feu macOS actif ; aucune règle
modifiée. Capture BPF inaccessible sans privilèges. On ne classe donc pas
le réseau applicatif comme opérationnel.

La trace temporaire est retirée dans core a98a972a77 et le pilote reconstruit.
La preuve diagnostique reste dans les artefacts ignorés avec les identités
des binaires. Pas de troisième répétition aveugle. Continuer le diagnostic
hôte ciblé et le canal virtio-vsock prévu par aros-next/docs/AARCH64.md pour
les services Mac/VM : fichiers et presse-papiers ne doivent pas dépendre
du Wi-Fi local. Le transport réseau pour le bare metal reste un objectif
distinct, pas une propriété démontrée par vsock.

## Premier échange virtio-vsock réel

Core ec66d3cbeb : transport de paquets vsock réutilisant PCI/queues/DMA/reset
existants, trois files RX/TX/event, CID lu en configuration stable, copies
possédées, validation des complétions et invalidation sur événement de reset.
API de paquets seulement : la couche de flux résidente et ses crédits complets
restent à implémenter. La sonde est un unique flux borné de 32 octets.

`artifacts/vz-vsock-stream/verdict.json` PASS en 1,53 s : handshake, échange
exact AROS→Mac→AROS, marqueurs des deux côtés, reset et contrat startup complet.
Disque SHA256 7bc4042bcc4a302b73f7ac3eeacfa02d15a91d9cff95f55489dba1b5fa4c9dfb ;
lanceur e8aaf6900ce44b30c3a91f7eb2c232b04b5a34987d50f5c6d66f1dc76fdede73.
`artifacts/vsock-queue-test.log` : 18 cas ASan/UBSan PASS, dont réception
pendant TX occupé, wrap 16 bits, IDs dupliqués, longueurs/CID invalides et reset.
Compilation Swift et invité avec warnings-as-errors PASS.

Second essai `vz-vsock-negative` avec l'ancienne image réseau : FAIL en 1,52 s,
mais sur la sonde SANA sans device réseau exposé. Cela ne valide PAS le chemin
vsock-not-run du nouveau scorer ; pas de répétition pour obtenir un joli négatif.
Limites : aucun fichier ni presse-papiers exposé, transport polled dans cette
preuve, aucune qualification physique/Pi, pas de flux résident/reconnexion.
Procédure et contrat dans docs/VSOCK.md. Prochaine étape : propriétaire résident
du flux, crédits/fragmentation puis protocole de services fichiers/presse-papiers.

Revue 264 approuve le contrôle UDP et propose de viser l'adresse vmnet du Mac
(passerelle DHCP) plutôt que son adresse Wi-Fi : diagnostic ciblé à reprendre,
sans conclure que le défaut soit résolu. Revue 265 AFS+ : claude-next lit déjà
le dma-bounce ; signalé le Mask 32 bits du boot node comme cause possible,
sans duplication ni prise du M1.

## Flux vsock : crédits, fragmentation et deux connexions de 64 Kio

Core 8d9695cdb5 ajoute vsock_stream.c/.h, moteur portable sans OS, deux rings
fixes de 16 Kio, crédits validés, compteurs modulo 32 bits, acceptation TX
transactionnelle, fermeture et RST. Les données portent les crédits actualisés
pour éviter de priver TX quand RX progresse. f95496c9a0 conserve une erreur
latente après émission RST ; c77a928393 distingue aussi un RST spontané
d’une fermeture attendue. Ces chemins sont contrôlés sur hôte ; la conservation
de l’erreur est également compilée côté invité et
vérifiée sur hôte, sans troisième run VZ. Le moteur n'est PAS encore un
service résident ni une API device AROS.

La sonde transfère deux fois 64 Kio, ports invités distincts, chunks applicatifs
de 997 octets, comparaison intégrale. Premier run vz-vsock-bulk FAIL 4,51 s :
la sonde sortait dès CLOSED alors que 14689 octets restaient dans RX. Le moteur
conservait les données ; correction du consommateur pour vider RX après fermeture.
Deuxième run vz-vsock-bulk-drain : deux marqueurs hôte, marqueur invité des deux
flux et startup complet, mais verdict original FAIL timeout à 30,07 s : le
scorer attendait à tort UNE occurrence du marqueur hôte. Verdict conservé.

Correction du scorer : contrat partagé tools/vz_contract.py, exactement DEUX
marqueurs hôte et un invité. tests/vsock-contract.py sur le journal conservé :
42 contrôles PASS (chaque marqueur absent/dupliqué est refusé). Preuve limitée
au contrat des marqueurs, pas une réécriture rétroactive du verdict VM.
Artefact artifacts/vsock-bulk-contract-replay.json. Disque du deuxième run :
4b3d9af32fa12d632808979bcedd175e41a34745d1f0295777b2091d8b179bd9 ; lanceur :
0570a8820b39cfde1300acc344978e599359743a19dc893ea13c06d48f8151a1.

15 cas ASan/UBSan PASS dans artifacts/vsock-stream-test.log : backpressure,
crédits, fragmentation, wrap, malformed, shutdown, lecture après fermeture.
Swift et invité warnings-as-errors PASS. Deux boots seulement ; aucune répétition
pour transformer la preuve existante en nouveau vert. Revue 266 approuvée.
Prochaine étape : propriétaire résident (IRQ/scheduling, device/IO, annulation
et durée de vie), puis services fichiers et clipboard texte/images. Le défaut
UDP via adresse Wi-Fi et les autres critères STAGE6 restent ouverts.

## vmbridge.device résident : Exec IO et interruptions

Core 7ddc8acf68 ajoute workbench/devs/vmbridge : une ouverture exclusive vers
CID hôte 2, unité=port, tâche propriétaire de PCI/DMA/flux, IRQ→Signal, watchdog
1 s, Open borné à 5 s. READ/WRITE partiels avec io_Actual, buffers copiés,
annulation des requêtes en attente/actives, un RST de flux étranger conservé
si TX occupé. Expunge refuse le worker vivant ; reset non acquitté garde toutes
les ressources et le code épinglés. Même contrat UP que virtionet, aucun SMP.
41cb314891 ajoute CMD_UPDATE et fermeture avec drain/shutdown borné (3 ticks),
puis reset avant retrait IRQ et libération DMA/PCI.

Build complet du module puis build quick PASS sans avertissement.
Premier VZ vz-vmbridge-device PASS 1,20 s : deux 64 Kio, AbortIO d’une READ
vide, Close/Open et 74/82 IRQ. Après ajout du drain : vz-vmbridge-flush PASS
1,22 s, READ/WRITE/UPDATE/Abort/reopen et 71/44 IRQ, contrat startup complet.
Disque final SHA256 ee68474262fe8a482bdec25eb2537041b3a33ec21e668c885f2d434c704467af ;
lanceur 0570a8820b39cfde1300acc344978e599359743a19dc893ea13c06d48f8151a1.
Artefacts : vmbridge-build.log, vmbridge-flush-build.log, vmbridge-probe-build.log,
vz-vmbridge-device/verdict.json, vz-vmbridge-flush/verdict.json.

Le paquet accepte --vsock-device, le runner --require-vsock-device exige les
marqueurs applicatifs et exactement deux compteurs IRQ positifs. Les sondes
raw et device ne se disputent pas le matériel. Deux boots, pas de répétition.
Pas encore de fichiers/presse-papiers, ni validation de reset défaillant, pression
de RST étrangers, expiration de Close ou usage prolongé. Revue 267 approuvée.
Prochaine étape : protocole de services au-dessus de l’IO résident, accès fichiers
et clipboard texte/images ; diagnostic UDP via passerelle vmnet toujours ouvert.


## File services: partial native evidence and command startup investigation

Core 899da8c0f1 fixes AbortIO after data has already moved: report the partial
transfer rather than cancellation. Review 268 approved. VZ
artifacts/vz-vmbridge-abort-fixed passes both 64 KiB cycles, flush, abort and
reopen (1.74 s). Core 3b7065280f adds VMExchange and its protocol client;
318cadde19 adds temporary command-entry diagnostics.

HostServices serves a pinned Shared directory on vsock port4051. Socket-pair
qualification passes (artifacts/host-services-test.log), including no-replace,
traversal/symlink refusal, incomplete upload cleanup and malformed frames.
See docs/HOST-SERVICES.md for the interface and limits.

VZ artifacts/vz-host-services and vz-host-services-trace remain FAIL at their
35 s and 20 s deadlines. The protocol workload completes upload, no-replace
refusal and readback before closing the first device connection (183/181 IRQs).
The second run's host file has 65536 correct bytes and SHA256
51fe7c95debcfefba857851144285f2450a5f2355c37dd6d99a5733b73e0ae6a.
LoadSeg returns, but RunCommand never reaches the command main diagnostic.
This does not qualify the CLI or the complete file-service gate.

Static investigation: the unrelocated startup instruction is b .; the full
relocation table DOES contain its JUMP26 to .text+8 at the END of the table.
An initial claim based on the table head was wrong and corrected on board.
Minimal linker and both strip-tool checks preserve that relocation; no linker
change is justified. Artifacts/startup-relocation retains this experiment.
NewStackSwap's inline assembly omits some caller-clobbered registers, but this
build keeps its operands in callee-saved registers: no causal claim or fix made.
Next: inspect the loaded entry and independently locate guest execution before
another acceptance run. No more blind VZ repetitions. No M1 or SMP work.

Host Wi-Fi supplied by John: Apartmenthaus Berlin Mitte, no password. No network
association changed; VZ NAT uses the host connection. Clipboard remains open.


## Command startup corrected; VZ file-service gate passes

Core 49ec565a3d defines WBenchMsg in the CLI alongside __nowbsupport, following
the existing portable probe and native CLI tools. Without it commandline.o
pulled fromwb.o, whose startup waits for a WBStartup message when pr_CLI is
null. The non-CLI RunCommand caller never sends that message. QEMU independently
captured AROSAppleKernelIdleLoop while NewStackSwap itself had passed. Adding
the symbol removes __startup_fromwb from the binary and reaches main argc=2.
This was not a linker relocation loss or an Exec stack-switch diagnosis.

An additional packaging mistake selected build/uefi/BOOTAA64.EFI (SHA256
dbbf5ad58003810b746d4b55f3941236a749d27c0dff8ef86163ef1bae817b7a), an older
loader without PCI FDT nodes. The OF tree dump proves their absence; there was
no demonstrated memory corruption. The previous packages actually selected
build/uefi-desktop-session/BOOTAA64.EFI (SHA256
e6b21692a8a4b838e880c2cee89ff116a121b7e63e48fd42cd791ba77b53cc55).
package-core.py now requires --efi explicitly rather than silently choosing
an old build. Failed diagnostic verdicts remain unchanged.

artifacts/hvf-command-current-efi: PASS 6.50 s, NewStackSwap, nested
LoadSeg/RunCommand, real VMExchange main and expected unavailable-service result,
plus complete startup contract. Bundle SHA256
0afbc6849dd740e3abdf1ba420f0cb3820ec42977c0c8e61be2e077a7811d41f.
artifacts/vz-host-services-current-efi: PASS 2.26 s, protocol 64 KiB
upload/readback, no replacement, path refusal, standalone HELLO and two
positive IRQ counts. Input disk SHA256
f78744a37f6997ddc4a1957f17d27bb27b8c147f295c9d74598796ad8a5d6530;
launcher b70bf5b682df747f80c07e12a74c150e97183757a472a5d9714cd6e3ad9527ab.
CLI PUT/GET against guest files, automatic desktop startup and text/image
clipboard remain unfinished. No M1, no SMP, no prolonged reruns.


## Actual file commands qualified across a VZ restart

Core 2df4413a2d checks Close success before publishing a downloaded file by
Rename. The --file-commands package and --file-exchange filesystem runner reuse
the isolated VMTEST0 FAT fixture. Two fresh VZ boots in
artifacts/vz-file-commands pass: VMExchange PUT uploads DATA/KEPT.BIN, GET
creates EXCHANGE.BIN (EXCHANG2.BIN after restart), a repeated GET returns20 and
preserves the destination. All 64 KiB are compared in the guest, in the host
Shared directory and independently with mtools on the resulting image.
The first download survives the second boot. Five positive vsock IRQ counters
per boot are required, plus the complete boot/filesystem/protocol contracts.

Input disk SHA256
955869ecdc0dc2c69bf4d38fa23d98e2b5c9d72bc8097cf16e6d37e1aefed70d;
file SHA256 1942de172742ea4cc88bacca1802a39924147e552ff0a36397597ad066c3e94f.
Write disk 96a14208d8c7ca4882bd7ed2c91a251c4477064c6bb211a35fac95720da6e03f;
restart disk 1d5ac06c2f9f829144aa2a46e2df19e268c772dc7d7161faffcb8d6e4e4fc4a6.
Only these two focused boots; no extended qualification or M1/SMP work.
Next: automatic text/image clipboard and desktop integration.


## Host clipboard endpoint and format conversion

Added original MIT ClipboardCodec/ClipboardStore, wired into HostServices and
the VZ launcher's --host-services option. MVHS ops8..12 capture an immutable
IFF clipboard snapshot or conditionally publish a fully received one. Main-queue
NSPasteboard access, 16MiB payload/4MP image bounds, revision conflict status6,
disconnect discards pending upload. At most4 host connections allow old socket
cleanup to overlap a new guest session. No simultaneous guest device opens are
claimed: its single-stream constraint still needs session serialization in the
future synchronization daemon.

Text preserves Unicode in UTF8 plus a Latin-1 CHRS fallback; older CHRS-only
apps see '?' for unmappable characters. ILBM indexed/24-bit RGB, ByteRun1,
binary mask and PNG conversion are supported. Partial alpha, HAM, dynamic
palettes, rotated and multi-frame images are rejected; no silent flattening.
Format specifications and full wire contract: docs/CLIPBOARD.md.

artifacts/clipboard-codec-test.log PASS: bounds/truncation, Unicode, Latin-1,
independent indexed/masked ByteRun1 fixture, 17-pixel row padding, true-color PNG
round trip, stale/malformed write refusal. artifacts/clipboard-services-test.log
PASS: real socket framing, >64KiB Unicode snapshot, PNG publication, stale
revision conflict, size limit and abandoned upload. Tests use private named
pasteboards only; the user's general clipboard was not modified.

Host launcher compiles and signs; file-service host regression passes. The first
combined build had a missing codec/store dependency in the file-service test
Makefile rule; fixed without changing behavior. No clipboard VM run yet. Guest
clipboard.device integration, auto-sync/anti-echo/reconnect and desktop startup
are next; stage6 remains active and incomplete.

## Guest clipboard engine and desktop package

Core f83a2ba5b1 adds VMSync (one-second polling, Ctrl-C, ONCE) and a named
reference-counted session gate shared with VMExchange. Heap-owned semaphore/name
survive command unload; UP registry operations only, no SMP implementation.
Host-first initial import; later simultaneous copies resolve in favor of the
changed host revision. A guest copy during download cancels that cycle.
Our own write IDs/revisions are remembered across ordinary session reconnects.
Network input is complete before touching clipboard.device. The stock device
cannot roll back local disk errors or abort a deferred producer wait; guest
ID checking is not an atomic CAS. Forced removal of a gate owner can strand it.

Builds PASS for VMSync, VMExchange and clipboard.device. An initial parallel
invocation of three make targets raced genmf's temporary file; rerunning targets
successively fixed the build invocation. No core build-system workaround.

artifacts/vz-clipboard-sync PASS on exactly two fresh VZ boots (write and restart),
with private named Mac pasteboards, real clipboard.device and 19 positive
vmbridge IRQ closure counts each. Standalone ONCE imported the exact Mac seed;
the shared engine then checked text and two-color ILBM in both directions,
host-wins conflict resolution, two unchanged cycles without echo, recursive
session refusal/reopen, and existing CLI file/persistence checks. Image host
publication passes through ClipboardStore's PNG encoding; readback uses its
retained original IFF. No interactive application paste or physical M1 proof.
Input disk SHA256 143040cea606ca5a6fbd0a9995c9a6305630b404e6599aeb32a398a06134c75d.
Launcher SHA256 b2e99ffd87dc3b645ef7ad9a72c82d5ae12a5b1dd7cbefd8283acd81d9fc23b3.
Write disk 0d64c351b46c6f1c35984b7c224e46fb28c88f96567f43aae940e70c981189af;
restart disk 3a67c8fb2dda200c42ecf81951e79e10ea30d99b02a42a2e37a6f5ce4c7c479d.
artifacts/clipboard-marker-controls.json PASS 58 positive/negative marker
admission checks, without changing either VM verdict.

Desktop --host-services packaging adds VMSync/VMExchange/devices and starts
VMSync before G4Ready. The original 1.44MiB SYS overflowed with USB plus these
components; reused the existing 16MiB FAT expansion, deferring service files
until expansion. artifacts/desktop-clipboard-expanded-bundle packaging PASS,
bundle da2387a394263f3ffc554d0583264535dd6fc3abd649f2e9117ddb7c19b8effd;
SYS 34b8306c3197c2b35ab45de785c44f3722ab276b82984bf6ce84f3ed0dc55210.
That desktop package has NOT been boot-qualified. Next: verify daemon auto-start
with Wanderer and actual text/image paste, plus cross-task file contention and
failure recovery. No extra VM runs after the two focused guest tests, no M1/SMP.
Stage6 remains active; the other checklist gaps remain intact.

## Actual desktop daemon and host UDP gateway

artifacts/vz-desktop-clipboard PASS, 22.86s, with a 20-second guest desktop hold,
Wanderer startup, USB/HID bindings and ticks. The probe is a separate normal
AROS process, not a caller of syncOnce: it reads the exact Mac seed, holds a
foreground VMExchange session across two polling intervals while a guest text
copy is pending, verifies publication after release, runs the separately loaded
VMExchange HELLO, imports/exports a two-color ILBM, and checks stable host/guest
revisions across two further intervals. The real auto-started VMSync performs
all synchronization. Named host pasteboard only. GUI menu/keyboard editing and
forced transport failures are not covered.
Disk eaa91ebcdd1fcbbf55df55f18d23a401c0d4008148bec5494ab717f739c730d4;
launcher b2e99ffd87dc3b645ef7ad9a72c82d5ae12a5b1dd7cbefd8283acd81d9fc23b3;
VMCLIP e65f7248b7cb1d767cd76aeb857709f5f8ea4b2e6150f244ad9987c2a5c6d536.
Core unchanged at f83a2ba5b1. Added explicit desktop probe packaging/runner
contracts; general host-file gates are not silently substituted.

artifacts/vz-network-gateway PASS, 2.57s. The existing AROSTCP socket test sent
32 random challenge bytes to 192.168.64.1:59239 and received the exact reply.
Independent artifacts/network-gateway-peer/verdict.json PASS records receipt
from DHCP guest 192.168.64.5:1024. No guest driver or stack change was needed.
Because bridge100 did not exist before VM startup, the bounded peer bound an
ephemeral wildcard socket and advertised the explicit NAT bridge address;
only the per-run challenge is answered. The tool requires a unicast advertised
address and matching address for specific binds. Socket closed on success.
Disk 0500bfd01868216a7a4e000dd5ba437389d26ba7d7a2f5cc4385ff85b46964b3;
launcher same as above. This establishes working UDP through the NAT gateway,
not the cause of earlier Wi-Fi-address peer failures. DNS/TCP and automatic
network startup in the desktop remain. Host Wi-Fi route was en0 via 10.0.170.2;
no association, firewall or M1 state changed.

Exactly two VM runs in this step, both passed; no prolonged test loop. Next:
DNS through the DHCP-provided resolver and desktop network startup, then the
remaining stage-six checklist (including visual acceptance, GPU IRQs and current
build reproducibility). Goal remains active, no SMP work.
Desktop marker-admission controls also pass 40 positive/negative cases in
artifacts/desktop-clipboard-marker-controls.json; original VM verdict unchanged.

## Desktop network/DNS integration — qualification not achieved

Added desktop --network-stack startup through Run, plus an independent VMNET
observer (--network-probe). VMNET does not start AROSTCP: it waits for the normal
startup process/library, checks vn0 DHCP address and resolves example.com through
bsdsocket gethostbyname. dhclient already calls AddDynNameServ/EndDynNameServ;
no DNS IP or host entry is hardcoded. Added separate strict runner flags for
headless DNS and desktop autostart/DNS. Reused expanded SYS packaging for the
network driver, dependencies, commands and probe alongside clipboard/USB.
The first probe compile exposed an incorrect CONST_STRPTR cast against the
socket API's const char* signature; removed it, no warning suppression.

Three short desktop attempts, all FAIL, retained unchanged:
- artifacts/vz-desktop-network-dns, 2.87s, disk
  8bc4362fe35fde1b045f85ad256ea71877580a1fb39d0d4a24b11911458f17a4.
  AROSTCP published its API; Wanderer failed foreground detail0x905 (then0x529).
- artifacts/vz-desktop-network-dns-logs, 12.47s, disk
  4291a4d8fee5e9a209eef58e03c36c0fdc69ae115926fd6669f90616de8f43b3.
  Desktop/clipboard progressed, but AROSTCP stopped after readconfig and no
  socket library appeared. Our added CONSOLENAME setting was invalid: the
  CONSOLE_SYSLOG branch is not compiled, and logname_changed rejects it.
- artifacts/vz-desktop-network-dns-filelog, 3.10s, disk
  15e94a6b9a42b0ee4a046609cbcc94c30a19552bb470053530ba895220d79e2d.
  Removed CONSOLENAME; retained LOGFILENAME=RAM:AROSTCP.log and captured command
  output in RAM:AROSTCP-startup.log. API startup resumed, but the original
  foreground failure remains. Thus console-log attribution was not established.

All three use core f83a2ba5b1 and launcher
b2e99ffd87dc3b645ef7ad9a72c82d5ae12a5b1dd7cbefd8283acd81d9fc23b3.
No DNS verdict or TCP/Internet claim follows from these runs. The desktop gate
still requires Wanderer at the front and no requester; it was not weakened.

Added failure-only coldstart diagnostics: snapshot up to16 window addresses,
flags and bounded titles under the Intuition lock, then emit after unlocking.
Kernel module build PASS; existing printbacktrace format warnings unchanged.
These diagnostics have not yet run. Next action is one diagnostic reproduction
to identify the foreground window before any further configuration guess.
No more VM runs in this step, no M1/SMP. Goal/task71 remain active.
Diagnostic core commit: 545f66dabf. Prepared but unrun candidate:
artifacts/desktop-network-dns-diagnostic.img, with packaging/provenance under
artifacts/desktop-network-dns-diagnostic-bundle. Reproduce using the desktop
network and clipboard runner flags from docs/NETWORK.md and docs/CLIPBOARD.md.

## Desktop network failure identified and corrected: missing T:

artifacts/vz-desktop-network-dns-diagnostic FAIL3.03s identifies an untitled
Wanderer window at the front plus a System requester behind it. The blocking
condition was the requester exclusion, not another foreground window alone.
Added DOS ErrorReport task/error/volume/device serial evidence immediately before
display, preserving suppression and Retry/Cancel semantics (core d6ee88aca9).
artifacts/vz-desktop-network-requester FAIL2.96s records the exact cause:
[DOS] requester task=AROSTCP DHCP client code=218 volume=T device=-.
This supersedes the earlier unproven logging/foreground hypotheses.

Desktop startup now includes standard AROS MakeDir/Assign, creates RAM:T and
assigns T: before launching services. AROSTCP logs use T:AROSTCP.log and
T:AROSTCP-startup.log. All desktop packages use the existing 16MiB SYS expansion;
the small FAT12 image is only the seed. No network driver or resolver changes.

Building workbench-c exposed absent pinned LoadResource and R catalog submodules.
Initialized b84bda0fa63c0dfc7945749a0fdaf08cb3d0c46e and
ed5d3b16fd320735ad962b56da00ab6fe5f9b21d; fresh configure now initializes both.
Full workbench-c build then PASS; MakeDir/Assign are consumed from that build.
The earlier frozen 18,069-file reproducibility comparison did not include these
additional base-command products; do not extend its claim to them.

artifacts/vz-desktop-network-temp PASS23.02s with a 20-second guest hold:
normal AROSTCP Startup-Sequence launch, DHCP192.168.64.6, DNS server192.168.64.1,
gethostbyname(example.com) ->172.66.147.243, Wanderer foreground/USB-HID/ticks,
actual VMSync daemon text/images/no-echo and foreground-session contention.
No static DNS/hosts entry or foreground waiver. TCP/HTTP, GUI editing commands
and prolonged reliability remain separate. Named host clipboard only.
Disk be627affbea6df4420a49f1a65bb673ff1ef2f8810c8796ed8bd1e60ec844b88;
launcher b2e99ffd87dc3b645ef7ad9a72c82d5ae12a5b1dd7cbefd8283acd81d9fc23b3;
bundle ed74274dccb6b4f584e28b479ac69a689103faf0f04ed2546f7c4180271afda9;
SYS b6a8a8c5c267583b656dec10f1fc5fbc2b216c63c2338afcde33a015db74bcba.
Core d6ee88aca9 includes both diagnostics. Three short runs in this step:
two diagnostic failures, then the corrected combined pass; no further runs.

Next product boundary: persistent desktop work storage (the current persistent
FAT proof uses an isolated test volume, while embedded SYS is RAM-backed), TCP
usage and remaining visual/GPU IRQ and complete-current-build acceptance. Keep
the full stage-six goal active; no SMP/M1 work was done.
Captured-log marker admission also passes 48 positive/negative cases in
artifacts/desktop-network-marker-controls.json. Build logs are preserved as
artifacts/base-commands-missing-catalogs.log, base-commands-complete.log and
dos-requester-build.log. Base command Makefiles also contain wall-clock ADATE
macros (for example Mount/SetClock); a complete cross-date reproducibility claim
must account for these rather than reuse the frozen subset comparison.

Prepared a normal, unbounded desktop image without qualification probes at
artifacts/desktop-network-ready.img (package: desktop-network-ready-bundle,
bundle SHA f25c78f47475cdbf3f69f9dd0df994f7863a6bd6045c5f7e2973511067eba6fb).
Packaging passes; that probe-free variant has not had a separate VM run.
Launch from this repo with:
`build/macaros-vm artifacts/desktop-network-ready.img artifacts/desktop-user-state --gpu --usb-input --host-services --network`.
This normal launch uses the general Mac clipboard, as requested by host-services;
all automated qualifications above used isolated named pasteboards.

## Desktop WORK auto-mount — 2026-10-01

Commit fdb7ccb adds optional desktop --work-volume. Reuses stock
LoadResource/Mount/fat-handler and virtio.device; bus/storage HIDDs are
noexpunge. Mount retains its native-width mask default. WORK is a new FAT16
256 MiB volume appended after the 64 MiB boot filesystem, unit 0. Explicit
VM layout, not GPT/native/Pi partition discovery. No existing image is resized.
Packaging provenance makes make-disk select the layout automatically.

artifacts/work-layout-verdict.json PASS: geometry/label, mtools write/read,
boot region unchanged, existing output refused with identical hash.
artifacts/vz-desktop-work PASS: 12.98 s, ten-second desktop, USB HID and mount
startup enabled. Input SHA256
9524b92a4f2f5fbd22f0d68f474a9b28810f7b334c47d8f9473879895645582a.
artifacts/vz-desktop-work-write PASS: 11.78 s, ten-second desktop without USB;
custom startup runs MakeDir WORK:PROOF. Independent host mdir after exit finds
::/PROOF/. work-verdict.json records the resulting image hash and scoped claim.
Input SHA256 99a022a67a3d1af9f86075d007accddf4017268888698069fd88d2e5eaefa1bf.
Core d6ee88aca9; launcher
b2e99ffd87dc3b645ef7ad9a72c82d5ae12a5b1dd7cbefd8283acd81d9fc23b3.
The first host assertion incorrectly listed the empty PROOF directory's contents;
corrected to check the root entry, preserving both original VM verdicts.
Provenance auto-selection was added after these runs; they used explicit disk
--work-volume. Guest bytes are unchanged by that host-side admission change.

Next: guest re-read on a fresh VM from the resulting disk, explicit clean
shutdown/flush integration, combined network/clipboard/WORK packaging and
normal user image refresh. No power-loss guarantee claimed. Two short VM
runs only; the older two-boot filesystem harness is separate evidence.

## WORK fresh-VM read and combined desktop — 2026-10-01

Two startup issues found and corrected, without changing the filesystem driver:
- Device and volume both named WORK made DOS getdevpacketinfo reject Inhibit:
  it requires DLT_DEVICE, but GetDeviceProc returned the volume. Device is now
  VMWORK0; user-visible volume remains WORK.
- fat-handler replies to its startup packet before ProcessDiskChange publishes
  the volume (rom/filesys/fat/main.c). Opening WORK immediately after Mount can
  request a missing volume on restart. Stock Assign temporarily locks VMWORK0,
  then removes VMWorkReady. This waits for the handler without a timed sleep or
  a retained assign lock. The same startup is used by normal and test images.

Added independent desktop-work probe: uses the already-mounted volume, creates
or verifies a deterministic 65536-byte file, checks EOF and Close, drains FAT
with Inhibit(VMWORK0), checks negotiated VIRTIO_BLK_F_FLUSH, requests CMD_UPDATE,
then uninhibits. File creation and existing-file read have distinct strict
run-vz markers/phase flags. Errors report the failing stage. This is a test
command, not a host Quit implementation.

Evidence artifacts/desktop-work-persistence-verdict.json PASS, including host
mtype comparisons and exact disk hashes. Write run
artifacts/vz-desktop-work-integrated-fixed-write PASS 23.14 s; restart read
artifacts/vz-desktop-work-restart-fixed PASS 23.11 s. Both include bounded
20-second desktop, USB HID, actual clipboard daemon text/image/foreground
contention/no-echo and DHCP/DNS. File SHA256
1942de172742ea4cc88bacca1802a39924147e552ff0a36397597ad066c3e94f.
Core d6ee88aca9, launcher b2e99ffd87dc3b645ef7ad9a72c82d5ae12a5b1dd7cbefd8283acd81d9fc23b3.
The read boot used the corrected startup bundle and a byte-for-byte copy of
WORK from the successful write. Transfer hash/paths are independently recorded
in artifacts/work-restart-volume-transfer.json. This is persistent data across
fresh VMs with an updated boot bundle, not an identical-whole-disk two-boot gate.

Failed attempts retained: integrated-write (2.25 s, generic probe failure),
integrated-diagnostic (Inhibit error 218), integrated-restart (40.11 s timeout,
WORK requester before the readiness barrier). Initial probe build rejected
IOStdReq/IORequest pointer mismatches; corrected explicitly, no warning override.
An orchestration attempt checked the write verdict before it existed and started
copying for the first failed restart while the writer runner was finishing. The
first failed restart's input SHA matches the writer's final disk SHA exactly;
that failed run remains FAIL and is not the accepted fresh-read proof. The
accepted restart copied only after the source runner was confirmed complete.

Normal image prepared, no probes and no extra boot test:
artifacts/desktop-work-ready-v2.img (320 MiB), bundle
artifacts/desktop-work-ready-v2-bundle. Includes WORK, network and VMSync.
Run from the VM repository:

    build/macaros-vm artifacts/desktop-work-ready-v2.img artifacts/desktop-work-user-state --gpu --usb-input --host-services --network

Automated clipboard runs used private named pasteboards; normal launch uses the
Mac clipboard. Host SIGTERM/quit still stops the VM without a guest flush
handshake: the probe's explicit flush is not a product shutdown guarantee.
Next implementation: clean host-requested shutdown, then app packaging and
remaining stage-six acceptance (visual/input, GPU IRQ, full rebuild comparison,
TCP and bounded sustained use). No SMP, no physical M1/Pi validation claimed.
