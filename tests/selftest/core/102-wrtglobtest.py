# wrtglobtest: the Windows.Globalization classes Windows Calculator asks for
# (0.2.2 "Pulsar" item 3), through the registry's ActivatableClassId keys
# (Windows.Globalization.dll): languages, regions, the user's preferences,
# number, currency and date formatters and the Gregorian calendar
DOC = ('`wrtglobtest` (`ApplicationLanguages`, `Language`, `GeographicRegion`, `GlobalizationPreferences`, '
       '`DecimalFormatter`, `CurrencyFormatter`, `Calendar`, `DateTimeFormatter`; 64- and 32-bit)')
TESTS = [
    Test('wrtglobtest', 'wrtglobtest', [r'wrtglobtest: \d+ passed, 0 failed \(64-bit\)'], timeout=120),
    Test('wrtglobtest x86', r'C:\Programs\x86\wrtglobtest.exe', [r'wrtglobtest: \d+ passed, 0 failed \(32-bit\)'], timeout=120),
]
