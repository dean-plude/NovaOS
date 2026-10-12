program: **Steam** (App Store, the current client)
kind: game
rating: Bronze
works: Its installer runs and installs the bootstrapper; on first start Steam downloads its client over HTTPS, updates itself to the 64-bit client, verifies it and starts it with `SteamService` (which runs as a real service) and its browser, `steamwebhelper.exe` (Chromium); the browser starts its GPU, network, storage and page processes and Steam accepts its UI connection; the "Sign in to Steam" window opens and draws its form (account name, password, Remember me, Sign in) ([Steam](#steam)). Signing in and games are unchecked
problems: Signing in and games not checked
checked: nightly corpus (install and update, [`092-steam.py`](../tests/appcorpus/092-steam.py))
