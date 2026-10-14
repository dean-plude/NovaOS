## Thunderbird opens an e-mail message

Roadmap step 27.1 (grow the corpus), its second app: Thunderbird 158,
Mozilla's mail client, which the App Store lists and which runs on the
same engine as Firefox.  The nightly corpus now installs it with the
Store's own button from the Store's download (Mozilla's full installer,
which the Store unpacks into `C:\Programs\Mozilla Thunderbird`) and opens
`C:\Apps\data\hello.eml`, a plain-text message the corpus makes, in
Thunderbird's message window (`tests/appcorpus/954-thunderbird.py`).  The
sender, recipient, date, subject and text must match
`tests/reference/thunderbird.png`.  It needed no change to NovaOS:

- **A pinned download.**  The catalog listed Mozilla's "latest" link,
  whose file changes with every release; it now lists Thunderbird 158.0
  from Mozilla's release archive, the same file the corpus tests, so the
  Store installs what was checked.
- **Deployment settings.**  A `policies.json` in Thunderbird's
  `distribution` folder (Mozilla's documented way to configure a
  deployment) turns off update checks, telemetry, the default mail
  client question, the start page and the account setup Thunderbird
  offers when no account exists.
- **Two windows.**  Opening a message also opens Thunderbird's main
  window behind it, so the test closes both (`App(closes=2)`).

Under QEMU without KVM the Store installs it in about 25 seconds and the
message shows within six minutes of starting it.  Still open: no mail
account, sending or receiving is checked (there is no mail server in the
test), and the main window stays empty without an account.
