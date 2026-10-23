# Local testing cheats

The existing Cheats panel now includes:

- **Moon jump (hold L + A)**: session-only toggle. Each guarded Restoration pad
  update sets the current player actor's vertical velocity to 12 while the chord
  is held. The general cheat timer runs too slowly for smooth movement. Player
  identity, active gameplay, pause, death, locked/frozen/riding flags and finite
  velocity are checked first. Native gravity, horizontal velocity, collision and
  position are untouched; release the chord to return to native falling.
- **All dungeon small keys + boss keys**: one-shot grant of 99 small keys and a
  boss key in all four temples. Can become part of the save through normal saving.
  Other dungeon-item bits (maps/compasses), other dungeon slots and stray fairies
  are preserved.

USA Restoration layout only. The native item-grant branches at `0x233f30` and
`0x233f4c` index dungeon items at `0x7753d4` and signed key counts at `0x7753de`;
verify both literal addresses before granting. Preflight all affected save words
before any queued action writes. Existing title/code/save gates also apply.

The player list pointer is at gameplay context + `0x20dc`. The actor layout in
Restoration's `game/actor.h` places `vel.y` at `0x68`; `vel_y` at `0x74` is a
separate motion field and is not changed. The chord is read from the existing
Restoration held-pad state at `0x72a008`, independently of the stick-click option.

`tools/mm3d/simple_cheats_check.cpp` covers guards, preflight failure, adjacent
save-byte preservation, chord release, disabled mode, actor identity, pause,
death/frozen state, finite velocity and session reset. Live behavior still needs
checking after loading the new build; no cheats are automatically enabled.
