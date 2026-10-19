# bluetooth audio: the headset's sound (step 28.2, slice 2).  The bluetooth
# boot's "NovaOS Test Headset" (tools/btpeer.py) answers the audio profiles
# a real one does: Hands-Free (HFP 1.8 over RFCOMM: the service level
# connection, then voice on an eSCO link, 16-bit 8 kHz to the host on the
# radio's isochronous SCO endpoints, interface 1 setting 2) and Audio Sink
# (A2DP over AVDTP: an SBC stream endpoint, configured to 48 kHz joint
# stereo).  bttest pairs it again (the bluetooth tests before removed it)
# and switches both services on: kernel/nt/bthaudio.c connects them and
# gives the mixer (kernel/audio.c) two outputs and an input, as a USB
# headset does.  The stereo output, the default, plays a tone that btpeer
# decodes from SBC into bt-a2dp.wav; the hands-free output plays one over
# the voice link into bt-hfp.wav; the headset's microphone (a 600 Hz tone
# from btpeer) records through waveIn.  Removing the headset takes its
# sound devices away.

DOC = ('a Bluetooth headset\'s sound (btpeer\'s headset answering Hands-Free and A2DP): bttest switches both services '
       'on and winmm lists its stereo output, its hands-free output and its microphone; a tone through the default '
       'output reaches the headset as SBC over A2DP, one through the hands-free output over an eSCO voice link on '
       'the radio\'s isochronous endpoints, and the headset\'s microphone records; removing it takes its devices '
       'away')

TESTS = [
    Test('bluetooth headset', 'bttest audio', [r'bttest audio: \d+ passed, 0 failed'], timeout=300, elevated=True,
         boot_expect=[r'\[BTHAUD\] NovaOS Test Headset: A2DP sink SEID \d+: SBC 48\.0 kHz joint stereo',
                      r'\[BTHAUD\] NovaOS Test Headset: Hands-Free on RFCOMM channel \d+',
                      r'\[BTHAUD\] NovaOS Test Headset: Hands-Free service level connection up']),
    Test('bluetooth stereo', 'soundtest tone 440 1000', [r'played \d+ samples'], timeout=120,
         check=tones(440, wav='bt-a2dp.wav', only=True),
         boot_expect=[r'\[BTHAUD\] NovaOS Test Headset: A2DP stream started']),
    Test('bluetooth hands-free', 'soundtest tone 330 1000 dev=Hands-Free', [r'played \d+ samples'], timeout=120,
         check=tones(330, wav='bt-hfp.wav', only=True),
         boot_expect=[r'\[BTH\] 4E:4F:56:41:00:02: voice link [0-9a-f]+ \(eSCO\)',
                      r'\[BTHUSB\] voice: interface 1 setting 2, 17-byte isochronous packets each way']),
    Test('bluetooth microphone', r'soundtest record C:\btmic.wav 2000 dev=Hands-Free', [r'recorded 88200 samples'],
         timeout=120, check=recording(r'C:\btmic.wav', 600, 1500)),
    Test('bluetooth headset remove', 'bttest audio-remove', [r'bttest audio-remove: \d+ passed, 0 failed'],
         timeout=120, elevated=True),
]
