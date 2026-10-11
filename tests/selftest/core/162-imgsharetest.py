# imgsharetest: a copy of a running program takes its pages from the
# kernel's image cache (as each Firefox process takes xul.dll's): its data,
# zeroed data and read-only pages as the file has them, not as the first
# process changed them, and its imports bound; 64- and 32-bit.  The 64-bit
# build commits 15/16 of the RAM (the per-process cap is the whole RAM, was
# 7/8) and is refused more.  Then `mem` counts the loads that reused a kept image.
DOC = '`imgsharetest` (a second process of a running program shares its image pages from the image cache; 64- and 32-bit; a commit of 15/16 of the RAM)'
TESTS = [
    Test('imgsharetest x64', 'imgsharetest', [r'imgsharetest child: \d+ passed, 0 failed', r'imgsharetest: \d+ passed, 0 failed'], timeout=120),
    Test('imgsharetest x86', r'C:\Programs\x86\imgsharetest.exe', [r'imgsharetest child: \d+ passed, 0 failed', r'imgsharetest: \d+ passed, 0 failed'], timeout=120),
    Test('image cache counted', 'mem', [r'loads that reused one: [1-9]\d*'], builtin=True),
]
