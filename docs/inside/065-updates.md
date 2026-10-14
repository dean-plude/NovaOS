- **Updates**: an installed NovaOS updates itself from an update channel
  ([updates.md](updates.md)). The App Store's Updates page (or the
  Terminal's `update`) downloads the new kernel and bootloader, the next
  restart starts them, and NovaOS goes back to the old ones if the new
  kernel does not reach the desktop. A NovaOS built with the release key
  installs only channels signed with it (Ed25519). The default channel is
  the newest tagged release; each green CI run on `main` also publishes a
  channel of its own with the rolling `latest` build.
