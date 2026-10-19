## Bluetooth: headset audio (A2DP and hands-free)

Second part of roadmap step 28.2.  A paired Bluetooth headset now plays
and records, the way a USB headset does:

- **A2DP stereo** (`kernel/nt/bthaudio.c`): NovaOS is the source.  It
  configures the headset's SBC stream endpoint over AVDTP (48 kHz joint
  stereo) and streams SBC frames in RTP packets, encoded by NovaOS's own
  fixed-point SBC encoder (`kernel/nt/sbc.c`, written from the A2DP
  specification and checked against ffmpeg's decoder), with fewer bits a
  frame while the link cannot keep up.  The headset shows up as
  "Headphones (NAME)", the default output.
- **Hands-free voice**: HFP 1.8 with NovaOS as the audio gateway, over
  RFCOMM with credit-based flow control.  Playing to "Headset (NAME
  Hands-Free)" or recording from its microphone opens an eSCO voice link,
  which `bthusb` carries on the radio's isochronous endpoints (interface 1
  at the setting the link needs); `usbd` gained isochronous OUT transfers
  for it, and a Windows driver's bulk transfer that waits now sleeps one
  tick between polls, not two.
- **Tested** in the devices suite's `bluetooth` boot: `tools/btpeer.py`'s
  headset now answers HFP and A2DP, decodes what it is sent into WAV files
  and sends a tone from its microphone; `soundtest` plays through both
  outputs and records from the headset.

Not yet: connections the headset starts, wide-band speech (mSBC), other
A2DP codecs, the headset's buttons, and a real headset on the T14.
