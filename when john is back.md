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
