# soundtest godot: WASAPI output the way Godot 4's driver opens and feeds
# it (an endpoint notification callback, IAudioClient3 with
# SetClientProperties, the engine period and InitializeSharedAudioStream
# without an event, then a thread polling the padding every millisecond),
# 64-bit and 32-bit; the recording must hold both tones
DOC = '`soundtest godot` (WASAPI through IAudioClient3 as Godot plays, 64-bit and 32-bit)'
TESTS = [
    Test('soundtest godot', 'soundtest godot 990 1000', [r'period 480 frames; played 48000 frames']),
    Test('soundtest godot x86', r'C:\Programs\x86\soundtest.exe godot 1320 1000',
         [r'period 480 frames; played 48000 frames'], check=tones(990, 1320)),
]
