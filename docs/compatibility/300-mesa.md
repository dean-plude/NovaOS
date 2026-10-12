program: **Mesa 3D** (OpenGL 24.2.4 built in; Vulkan 26.2.0 from the App Store, Runtimes)
kind: runtime
rating: Platinum
works: OpenGL 4.5 on the CPU (llvmpipe, on drive C: from the start) and Vulkan (lavapipe), 64- and 32-bit
problems: CPU only (llvmpipe, lavapipe) unless Venus is installed
checked: core self-test `gl2test`, CI graphics tests (`gltest`)
