# gl2test: OpenGL 2 and later with nothing installed: Mesa's llvmpipe is
# on drive C: from the start and NovaOS's opengl32.dll hands programs to
# it (GLSL shaders, read-back, SwapBuffers, a 3.3 core-profile context),
# 64- and 32-bit
DOC = '`gl2test` (OpenGL 3+ with shaders on the built-in Mesa llvmpipe, 64- and 32-bit)'
TESTS = [
    Test('gl2test x64', 'gl2test', [r'gl2test: \d+ passed, 0 failed'], timeout=300),
    Test('gl2test x86', r'C:\Programs\x86\gl2test.exe', [r'gl2test: \d+ passed, 0 failed'], timeout=300),
]
