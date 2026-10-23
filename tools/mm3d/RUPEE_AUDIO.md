# Selective accurate HLE audio

Audio offers Balanced and Accurate HLE. Balanced is the former Rupee sounds
mode (stored value 1); Accurate HLE is the former All audio mode (stored value 2).
Balanced is the default. Every old preference, including Standard, Rupee sounds,
All audio and either legacy boolean value, migrates to Balanced once. Saving
writes `hle_audio_mode_version: 1`, so later explicit Balanced/Accurate HLE
choices survive reloads. Missing/invalid modes default to Balanced. Stored mode
IDs remain 1/2, and saving still writes a compatible boolean for older builds.
Do not seed the migration marker through release-default merging: that would
incorrectly preserve an older mode 2 selection before the runtime migration.
The packaged legacy boolean is ignored until settings are saved in the new form.

Balanced applies accurate ADPCM length/history and polyphase resampling to
three verified sample voices. The remaining voices retain the Standard path.
Selection is latched at each buffer dequeue and recomputed for loop tails, so
channel reuse and relocated sample addresses do not transfer the classification.
Identification uses ADPCM coefficient and bounded encoded-prefix fingerprints,
plus sample count; there are no bundled game samples or fixed runtime addresses.

JokerSound.bcsar evidence from the user's own ROMFS:

| Sound | Sequence file/start | Bank/program | Wave archive/index | Full/loop samples |
| --- | --- | --- | --- | --- |
| NA_SE_EV_RUPY_FALL | 2 / 0x278b | 3 / 9 | 6 / 11 | 1698 / 802 |
| NA_SE_SY_RUPY_COUNT | 4 / 0x72b | 3 / 11 | 6 / 13 | 946 / 50 |
| NA_SE_SY_GET_RUPY | 4 / 0x112 | 3 / 29 | 6 / 33 | 905 / 9 |

The pickup and counter sequences also use a bank-1 accompaniment, left Standard.
Bank3 program19 reuses wave33; other effects using these identical waveforms
receive the same correction. This is sample-specific, not a sound-ID dispatcher.
The UI explains this limitation. Modified audio samples fall back to Standard.

Capture audio-capture-20260929-211436-423.bin contains the pickup and counter
coefficient sets, complete initial buffers of905/946 samples and queued tails
of9/50 samples. In particular, rounding the9-sample pickup loop to14 changes its
pitch; the accurate decoder retains its exact length.

Validation:

- tools/mm3d/rupee_audio_check.cpp tests prefix bounds, damaged samples, unrelated
  coefficients and lengths. It accepts optional extracted CWAV paths to validate
  the production classifier against actual samples without distributing them.
- Local check passed for all three CWAVs, initial buffers and their loop tails.
  Scanning all801 waves across22 archives found only these three coefficient
  identities; prefix and length checks further narrow matching.
- mm3d_tests passed for new-user defaults, both legacy boolean values, all three
  retired menu selections migrating to Balanced, migration save/reload, later
  Accurate HLE selection persistence, both current modes, and invalid values.
  This menu change does not alter decoding or resampling.
