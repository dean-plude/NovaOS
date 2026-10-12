## Godot's sound through WASAPI

Squash the Creeps on Godot 4.7 plays its music (a leftover of roadmap
24.4).  Godot was silent in the app corpus because the corpus booted
NovaOS without a sound card for it: Godot's WASAPI driver found no render
endpoint (`GetDefaultAudioEndpoint` answers `E_NOTFOUND`, as Windows does
with no audio device) and fell back to its dummy driver.  With a card,
NovaOS's `mmdevapi.dll` already answers every call Godot makes.

- **Squash the Creeps** (app corpus) now boots with a sound card and must
  play at least 5 s of sound while it runs: the game's autoloaded music
  player, which Godot mixes and writes through WASAPI.
- **`soundtest godot`** (core suite, 64-bit and 32-bit): WASAPI output
  made the way Godot's driver (`drivers/wasapi/audio_driver_wasapi.cpp`)
  makes it: an endpoint notification callback, `IAudioClient3` with
  `SetClientProperties`, `GetSharedModeEnginePeriod`,
  `InitializeSharedAudioStream` without an event and
  `GetCurrentSharedModeEnginePeriod`, then a thread that polls the padding
  every millisecond and writes one engine period at a time; the recording
  must hold its tones.  Nothing tested the `IAudioClient3` calls before.
