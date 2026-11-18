# wrtapptest: more of the Windows Runtime classes Windows Calculator's
# start-up asks for (0.2.2 "Pulsar" item 3): ApplicationData.Current
# (Windows.Storage.ApplicationData.dll) refused to a program with no package
# identity, as on Windows (its LocalSettings: appxtest's packaged child),
# ResourceContext's qualifier values
# and languages (MrmCoreR.dll), User.GetFromId and NamedPolicy
# (twinapi.appcore.dll), all through the registry's ActivatableClassId keys
DOC = ('`wrtapptest` (`ApplicationData.Current` refused with no package identity, `ResourceContext.QualifierValues` and `Languages`, '
       '`User.GetFromId`, `NamedPolicy`; 64- and 32-bit)')
TESTS = [
    Test('wrtapptest', 'wrtapptest', [r'wrtapptest: \d+ passed, 0 failed \(64-bit\)'], timeout=120),
    Test('wrtapptest x86', r'C:\Programs\x86\wrtapptest.exe', [r'wrtapptest: \d+ passed, 0 failed \(32-bit\)'], timeout=120),
]
