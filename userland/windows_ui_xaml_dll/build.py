# Windows.UI.Xaml.dll's build hooks (tools/build_userland.py calls them with
# itself as b): the system XAML, NovaOS's XAML framework (Microsoft's WinUI 3
# source, userland/windows_ui_xaml/build.py) as the DLL Windows has, roadmap
# step 32.6 (0.2.2 "Pulsar", docs/releases/0.2.2.md).  It links what a
# "framework" program links of the framework (the XAML self-tests link it
# into themselves) and dll.cpp, its entry points: DllGetActivationFactory,
# which the registry's ActivatableClassId keys for every Windows.UI.Xaml
# class lead RoGetActivationFactory to (kernel/um/um_registry.c,
# tools/winrt/wuxclasses.py).  See docs/design/windows-ui-xaml.md.
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def objs(b, odir):
    xaml, msvcp = b.XAML, b.HOOKS['msvcp140']
    entry = b.compile_many([os.path.join(HERE, 'dll.cpp')], odir, 'wuxdll_',
                           xaml.program_flags(b, {'xaml': 'framework'}), [], compiler='clang++')
    return entry + xaml.dll_objs(b, odir) + msvcp.start_obj(b, odir, 'wuxdll_')


def link(b, odir, objs, deps, base):
    # (and tlregdtr.lib: the thread_local variables' start-up, as programs
    # link it; msvcrt.lib after ucrtbase.lib, for what the framework's
    # sources, compiled as programs are, take from it alone)
    b.HOOKS['msvcp140'].link_dll(b, odir, 'Windows.UI.Xaml', objs + [os.path.join(odir, 'tlregdtr.lib')], base,
                                 [d for d in deps if d not in ('vcruntime140', 'kernel32', 'ntdll')],
                                 ['/export:DllGetActivationFactory', '/export:DllCanUnloadNow'])
