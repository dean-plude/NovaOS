## Services run as administrators

A service whose account is `LocalSystem` (the default for
`CreateService`) now runs with the full, elevated token, as on Windows,
even when a program with the limited token starts it.  Until now
`StartService` started the service's process with the caller's own token,
so GOG GALAXY's service (`GalaxyClientService.exe`, registered as
`LocalSystem`) saw itself unelevated: it refused the client's request to
write its Vulkan registry keys ("Cannot create Vulkan keys without
elevation and a registry elevator"), the client asked ten times, gave up
and sent a "Registry-Operation-Failed" problem report.  Now the service
writes them and the client goes on to its sign-in window.  A service set
to run under another account keeps the caller's token.  `svctest` checks
that its service runs elevated, 64- and 32-bit.
