## Packaged apps in the shell, first part: the Start menu and Settings > Apps

The first part of item 2 of 0.2.3 "Nebula" ([plan](releases/0.2.3.md)).
An installed app package's applications now appear in the Start menu as
Windows lists packaged apps: from the package repository, not from
shortcuts.  `appx` records each application's tile logo (the manifest's
`Square44x44Logo`, else `Square150x150Logo`, found on disk as a scale or
target-size variant the way Windows' resource lookup picks one, a target
size of 48 first, then the scales from 200 down) and its `AppListEntry`;
the Start menu reads the current account's packages, leaves out
frameworks and `AppListEntry="none"` applications, shows each one by its
display name with its logo, and starts it in its package's folder, so it
runs with its package identity.  `appx` no longer writes `.lnk` files
(removing a package installed before still deletes its old shortcut).

Settings has a new **Apps** page (`ms-settings:appsfeatures`,
`kernel/apps/settings_apps.c`): the installed packages with their logo,
publisher and version and an **Uninstall** button that asks first, then
runs `appx remove` as the system, without a UAC prompt, as Windows'
deployment service removes a Store app.  Up and Down pick an app, Delete
asks, Enter confirms.

- **Tests:** the core suite's `889-appxshell.py` installs the test
  package, starts it from the Start menu by its manifest name (its
  program writes the application user model id it runs as) and
  uninstalls it from Settings > Apps; `appxtest` checks the recorded logo
  (the scale-200 variant of the test package's new logo) and that it goes
  with the package.
- **Left:** suspend and resume (`CoreApplication.Suspending` and
  `Resuming` when a package's window is minimised and restored), the
  item's second part.
