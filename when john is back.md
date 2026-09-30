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
