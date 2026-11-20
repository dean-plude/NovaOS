/* xamlpagetest.exe — a XAML page in a Window (roadmap step 32.4;
 * docs/design/windows-ui-xaml.md): an app started as xamlapptest
 * starts it (Application.Start on the desktop path) makes a Window in
 * OnLaunched, as a desktop app does: the framework's DesktopWindowImpl with
 * its Win32 window, the window chrome, the DesktopWindowXamlSource and the
 * XAML island root under it, on the Windows App SDK's lifted content islands
 * (NovaOS's Microsoft.UI.Input.dll: ContentIsland, DesktopChildSiteBridge,
 * the island's input sources, AppWindow).  The window's content is a page
 * loaded with XamlReader.Load (a Grid holding a Border and a Button: text,
 * which needs DirectWrite's Windows 10 font model, comes in the next part);
 * Activate shows the window, and once the framework has laid the page out
 * the test checks the window's bounds and the elements' laid-out sizes and
 * a pixel of the drawn Border, then uses the page as a user would: the
 * mouse clicks the Button (its Click handler counts the click and says so
 * in a TextBlock), keys typed reach the page (its CharacterReceived handler
 * adds them to a TextBlock) and Space on the focused Button clicks it
 * again.  The input is the window messages a user's mouse and keyboard
 * make, posted to the island's window (the content islands' input sources
 * hand them to the framework's input manager); then the window closes and
 * the app exits.
 *
 *   xamlpagetest       the self-test runs it with no arguments
 */
#include <stdio.h>
#include <string.h>
#include <new>
#include <corep.h>
#include <CStaticLock.h>
#include <SystemThemingInterop.h>
#include <DXamlInstanceStorage.h>
#include <XamlReader.g.h>
#include <WindowsXamlManager.g.h>
#include <XamlSourceFocusNavigationRequest.g.h>
#include <Grid.g.h>
#include <Button.g.h>
#include <TextBlock.g.h>
#include <TextBox.g.h>
#include <TextChangedEventArgs.g.h>
#include <PasswordBox.g.h>
#include <RichEditBox.g.h>
#include <TextControlPasteEventArgs.g.h>
#include <TextControlCopyingToClipboardEventArgs.g.h>
#include <TextControlCuttingToClipboardEventArgs.g.h>
#include <CharacterReceivedRoutedEventArgs.g.h>
#include <Window.g.h>
#include <roapi.h>
#include <FrameworkApplication_Partial.h>
#include <microsoft.ui.dispatching.h>

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); fflush(stdout); } } while (0)
#define CHECK_HR(what, expr) do { HRESULT h_ = (expr); if (SUCCEEDED(h_)) pass++; else { fail++; \
    printf("FAIL: %s: 0x%08lx (line %d)\n", what, static_cast<unsigned long>(h_), __LINE__); fflush(stdout); } } while (0)

#define XMLNS L"xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
#define XMLNS_X L"xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'"

static bool is(HSTRING h, const wchar_t *s)
{
    return wcscmp(h ? WindowsGetStringRawBuffer(h, nullptr) : L"", s) == 0;
}

/* what the app saw, checked after Application.Start returns */
static bool s_callback, s_launched, s_checked, s_exit_called;
static DWORD s_main_thread;
static int s_ticks;

/* the app's Application: its own object composed over the framework's (the
 * framework's is the inner object; this, the outer, answers
 * IApplicationOverrides itself and every other interface through the
 * inner), as a projection makes an app's App class */
class App : public xaml::IApplicationOverrides
{
public:
    IInspectable *m_inner = nullptr;   /* (the framework's, which this owns) */
    LONG m_refs = 1;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IInspectable) || iid == __uuidof(xaml::IApplicationOverrides)) {
            *out = static_cast<xaml::IApplicationOverrides *>(this);
            AddRef();
            return S_OK;
        }
        if (!m_inner) { *out = nullptr; return E_NOINTERFACE; }
        return m_inner->QueryInterface(iid, out);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_refs); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        LONG r = InterlockedDecrement(&m_refs);
        if (!r) { if (m_inner) m_inner->Release(); delete this; }
        return (ULONG)r;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG *n, IID **iids) override { *n = 0; *iids = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *name) override
    {
        return WindowsCreateString(L"XamlPageTest.App", 16, name);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel *t) override { *t = BaseTrust; return S_OK; }

    HRESULT STDMETHODCALLTYPE OnLaunched(xaml::ILaunchActivatedEventArgs *args) override;
};
static App *s_app;

/* the page's elements, kept for the checks the timer makes */
static ctl::ComPtr<xaml::IWindow> s_window;
static ctl::ComPtr<xaml::IFrameworkElement> s_panel, s_button, s_grid, s_label;
static ctl::ComPtr<xaml_controls::ITextBlock> s_status, s_typed;
static ctl::ComPtr<xaml_controls::ITextBox> s_box;     /* (the page's TextBox: XAML text input, part 3) */
static ctl::ComPtr<xaml::IFrameworkElement> s_boxElement;
static int s_text_changed;      /* the TextBox's TextChanged handler's count */
static COLORREF s_box_before[200 * 32];         /* the TextBox's pixels before typing */
/* (XAML text input, part 4: a PasswordBox and a RichEditBox, and the text
 * boxes' clipboard) */
static ctl::ComPtr<xaml_controls::IPasswordBox> s_password;
static ctl::ComPtr<xaml_controls::IRichEditBox> s_rich;
static int s_password_changed, s_rich_changed;  /* their PasswordChanged and TextChanged counts */
static int s_pasted, s_copied, s_cut;           /* the TextBox's Paste, CopyingToClipboard, CuttingToClipboard */
#define LAST_STEP 21
static int s_shown;     /* ticks since the checks, while the page stays on screen */
static int s_step;      /* the input steps, one per tick */
static int s_pixel_ticks;       /* ticks waited for the first frame's pixels */
static int s_clicks;    /* the Button's Click handler's count */
static wchar_t s_text[64];      /* what the page's CharacterReceived handler was given */
static ctl::ComPtr<msy::IDispatcherQueueTimer> s_timer;

static HWND find_window()
{
    HWND found = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND h, LPARAM p) -> BOOL {
        wchar_t cls[64];
        if (GetClassNameW(h, cls, 64) && !wcscmp(cls, L"WinUIDesktopWin32WindowClass")) {
            *reinterpret_cast<HWND *>(p) = h;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    return found;
}

/* the content island's window (the bridge, a child of the window's), and a
 * point of the window's client area in it */
static HWND find_island(HWND top)
{
    return top ? FindWindowExW(top, nullptr, L"Microsoft.UI.Content.DesktopChildSiteBridge", nullptr) : nullptr;
}

static LPARAM island_point(HWND top, HWND island, int x, int y)
{
    POINT p = { x, y };
    MapWindowPoints(top, island, &p, 1);
    return MAKELPARAM(p.x, p.y);
}

/* the TextBox's pixels (its 200x32 at the window's client point 24,260) */
static void box_pixels(HWND top, HWND island, COLORREF *out)
{
    POINT at = { 24, 260 };
    MapWindowPoints(top, island, &at, 1);
    HDC dc = GetDC(island);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 200; x++)
            out[y * 200 + x] = dc ? GetPixel(dc, at.x + x, at.y + y) : CLR_INVALID;
    if (dc) ReleaseDC(island, dc);
}

static HRESULT on_text_changed(IInspectable *, xaml_controls::ITextChangedEventArgs *)
{
    s_text_changed++;
    return S_OK;
}

static bool box_text_is(const wchar_t *text)
{
    wrl_wrappers::HString h;
    return s_box && SUCCEEDED(s_box->get_Text(h.GetAddressOf())) && is(h.Get(), text);
}

/* the clipboard's text is @want */
static bool clipboard_is(const wchar_t *want)
{
    bool ok = false;
    if (!OpenClipboard(nullptr)) return false;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        const wchar_t *p = static_cast<const wchar_t *>(GlobalLock(h));
        ok = p && !wcscmp(p, want);
        printf("xamlpagetest: clipboard \"%ls\"\n", p ? p : L"(none)");
        GlobalUnlock(h);
    }
    CloseClipboard();
    return ok;
}

/* a key pressed and let go, with Ctrl held when @ctrl (the island's thread
 * keeps the key state as its messages are taken) */
static void post_key(HWND island, UINT vk, bool ctrl = false)
{
    UINT ctl = MapVirtualKeyW(VK_CONTROL, MAPVK_VK_TO_VSC), sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (ctrl) PostMessageW(island, WM_KEYDOWN, VK_CONTROL, MAKELPARAM(1, ctl));
    PostMessageW(island, WM_KEYDOWN, vk, MAKELPARAM(1, sc));
    PostMessageW(island, WM_KEYUP, vk, MAKELPARAM(1, sc) | 0xC0000000);
    if (ctrl) PostMessageW(island, WM_KEYUP, VK_CONTROL, MAKELPARAM(1, ctl) | 0xC0000000);
}

static void click(HWND top, HWND island, int x, int y)
{
    LPARAM at = island_point(top, island, x, y);
    PostMessageW(island, WM_MOUSEMOVE, 0, at);
    PostMessageW(island, WM_LBUTTONDOWN, MK_LBUTTON, at);
    PostMessageW(island, WM_LBUTTONUP, 0, at);
}

static bool focused(IUnknown *control)
{
    ctl::ComPtr<xaml::IUIElement> e;
    xaml::FocusState focus = xaml::FocusState_Unfocused;
    return control && SUCCEEDED(control->QueryInterface(__uuidof(xaml::IUIElement), reinterpret_cast<void **>(e.GetAddressOf()))) &&
           SUCCEEDED(e->get_FocusState(&focus)) && focus != xaml::FocusState_Unfocused;
}

static bool selected_is(const wchar_t *text)
{
    wrl_wrappers::HString h;
    return s_box && SUCCEEDED(s_box->get_SelectedText(h.GetAddressOf())) && is(h.Get(), text);
}

/* pixels of the 200x32 at the window's client point @x,@y that differ from
 * the one at @bx (the control's own background), in its left part */
static int text_pixels(HWND top, HWND island, int x0, int y0, int bx)
{
    POINT at = { x0, y0 };
    MapWindowPoints(top, island, &at, 1);
    HDC dc = GetDC(island);
    int n = 0;
    if (!dc) return 0;
    for (int y = 6; y < 26; y++) {
        COLORREF bg = GetPixel(dc, at.x + bx, at.y + y);
        for (int x = 8; x < 60; x++)
            if (GetPixel(dc, at.x + x, at.y + y) != bg) n++;
    }
    ReleaseDC(island, dc);
    return n;
}

static void set_text(xaml_controls::ITextBlock *block, const wchar_t *text)
{
    if (block) block->put_Text(wrl_wrappers::HStringReference(text).Get());
}

static bool text_is(xaml_controls::ITextBlock *block, const wchar_t *text)
{
    wrl_wrappers::HString h;
    return block && SUCCEEDED(block->get_Text(h.GetAddressOf())) && is(h.Get(), text);
}

/* the pixel at the window's client point 100,100 (inside the Border) */
static COLORREF border_pixel(HWND top, HWND island)
{
    POINT at = { 100, 100 };
    if (!island) return CLR_INVALID;
    MapWindowPoints(top, island, &at, 1);
    HDC dc = GetDC(island);
    COLORREF c = dc ? GetPixel(dc, at.x, at.y) : CLR_INVALID;
    if (dc) ReleaseDC(island, dc);
    return c;
}

/* the page's handlers, as an app's code-behind */
static HRESULT on_click(IInspectable *, xaml::IRoutedEventArgs *)
{
    wchar_t line[32];
    swprintf(line, 32, L"Clicked %d", ++s_clicks);
    set_text(s_status.Get(), line);
    printf("xamlpagetest: Click (%d)\n", s_clicks);
    fflush(stdout);
    return S_OK;
}

static HRESULT on_character(xaml::IUIElement *, xaml_input::ICharacterReceivedRoutedEventArgs *args)
{
    WCHAR c = 0;
    if (FAILED(args->get_Character(&c)) || c < L' ') return S_OK;
    size_t n = wcslen(s_text);
    if (n + 1 < sizeof(s_text) / sizeof(s_text[0])) {
        s_text[n] = c;
        s_text[n + 1] = 0;
    }
    wchar_t line[80];
    swprintf(line, 80, L"Typed: %ls", s_text);
    set_text(s_typed.Get(), line);
    args->put_Handled(TRUE);
    return S_OK;
}

/* the input, one step per tick (the framework handles each step's messages
 * before the next): the mouse clicks the Button, "Nova" is typed, Space
 * clicks the focused Button; then the mouse clicks the TextBox, which takes
 * the focus, "Hi!" is typed into it, Backspace takes the "!" off, and its
 * text is drawn */
static void input_step(HWND top, HWND island)
{
    LPARAM button = island_point(top, island, 24 + 60, 148 + 20);  /* (the Button's middle) */
    switch (s_step) {
    case 1:
        PostMessageW(island, WM_MOUSEMOVE, 0, island_point(top, island, 10, 10));
        PostMessageW(island, WM_MOUSEMOVE, 0, button);
        break;
    case 2:
        PostMessageW(island, WM_LBUTTONDOWN, MK_LBUTTON, button);
        PostMessageW(island, WM_LBUTTONUP, 0, button);
        break;
    case 3:
        CHECK("the mouse clicks the Button (its Click handler runs once)", s_clicks == 1);
        CHECK("which the page shows", text_is(s_status.Get(), L"Clicked 1"));
        CHECK("the click focuses the window", GetFocus() == island);
        for (const wchar_t *c = L"Nova"; *c; c++)
            PostMessageW(island, WM_CHAR, *c, 1);
        break;
    case 4:
        CHECK("typed keys reach the page (CharacterReceived)", !wcscmp(s_text, L"Nova"));
        CHECK("which shows them", text_is(s_typed.Get(), L"Typed: Nova"));
        PostMessageW(island, WM_KEYDOWN, VK_SPACE, MAKELPARAM(1, MapVirtualKeyW(VK_SPACE, MAPVK_VK_TO_VSC)));
        PostMessageW(island, WM_KEYUP, VK_SPACE, MAKELPARAM(1, MapVirtualKeyW(VK_SPACE, MAPVK_VK_TO_VSC)) | 0xC0000000);
        break;
    case 5: {
        CHECK("Space clicks the focused Button", s_clicks == 2);
        CHECK("which the page shows", text_is(s_status.Get(), L"Clicked 2"));
        box_pixels(top, island, s_box_before);
        LPARAM box = island_point(top, island, 24 + 100, 260 + 16);    /* (the TextBox's middle) */
        PostMessageW(island, WM_MOUSEMOVE, 0, box);
        PostMessageW(island, WM_LBUTTONDOWN, MK_LBUTTON, box);
        PostMessageW(island, WM_LBUTTONUP, 0, box);
        break;
    }
    case 6: {
        ctl::ComPtr<xaml::IUIElement> control;
        xaml::FocusState focus = xaml::FocusState_Unfocused;
        CHECK("the click focuses the TextBox", s_box && SUCCEEDED(s_box.As(&control)) &&
              SUCCEEDED(control->get_FocusState(&focus)) && focus != xaml::FocusState_Unfocused);
        printf("xamlpagetest: TextBox focus state %d\n", static_cast<int>(focus));
        for (const wchar_t *c = L"Hi!"; *c; c++)
            PostMessageW(island, WM_CHAR, *c, 1);
        break;
    }
    case 7: {
        wrl_wrappers::HString h;
        if (s_box) s_box->get_Text(h.GetAddressOf());
        printf("xamlpagetest: TextBox text \"%ls\"\n", h.Get() ? WindowsGetStringRawBuffer(h.Get(), nullptr) : L"");
        CHECK("typed keys reach the TextBox (its Text)", box_text_is(L"Hi!"));
        /* (and the page sees them too: XAML's text boxes leave CharacterReceived
         * unhandled, so an app's handlers get it, CTextBoxBase::OnCharacterReceived) */
        CHECK("the page's CharacterReceived handler sees them too", !wcscmp(s_text, L"NovaHi!"));
        /* (Backspace's WM_CHAR is the island's TranslateMessage's, as a key's) */
        PostMessageW(island, WM_KEYDOWN, VK_BACK, MAKELPARAM(1, MapVirtualKeyW(VK_BACK, MAPVK_VK_TO_VSC)));
        PostMessageW(island, WM_KEYUP, VK_BACK, MAKELPARAM(1, MapVirtualKeyW(VK_BACK, MAPVK_VK_TO_VSC)) | 0xC0000000);
        break;
    }
    case 8:
        CHECK("Backspace deletes the last character", box_text_is(L"Hi"));
        break;
    case 9: {
        /* the text drawn: the TextBox's pixels changed where it is */
        static COLORREF after[200 * 32];
        box_pixels(top, island, after);
        int changed = 0, inner = 0;
        for (int y = 6; y < 26; y++)
            for (int x = 8; x < 60; x++)
                if (after[y * 200 + x] != after[y * 200 + 120]) inner++;   /* (text against the box's own background) */
        for (int i = 0; i < 200 * 32; i++)
            if (after[i] != s_box_before[i]) changed++;
        printf("xamlpagetest: TextBox pixels changed %d, text pixels %d\n", changed, inner);
        /* (TextChanged is raised asynchronously, as on Windows: by now, once
         * for each character typed and for the Backspace) */
        CHECK("the TextBox's TextChanged fires for each change", s_text_changed == 4);
        printf("xamlpagetest: TextChanged %d times\n", s_text_changed);
        CHECK("the TextBox draws its text", inner >= 20);
        /* (XAML text input, part 4) the selection and the clipboard: Ctrl+A
         * selects all of it, Ctrl+C copies it (XAML takes the edit keys and
         * copies through the Rich Edit's selection, which asks the TextBox
         * first: its CopyingToClipboard event) */
        post_key(island, 'A', true);
        post_key(island, 'C', true);
        break;
    }
    case 10:
        CHECK("Ctrl+A selects the TextBox's text", selected_is(L"Hi"));
        CHECK("Ctrl+C copies it to the clipboard", clipboard_is(L"Hi"));
        CHECK("its CopyingToClipboard event", s_copied == 1);
        post_key(island, VK_END);
        post_key(island, 'V', true);
        break;
    case 11:
        CHECK("End, Ctrl+V pastes it after", box_text_is(L"HiHi"));
        CHECK("its Paste event", s_pasted == 1);
        post_key(island, 'A', true);
        post_key(island, 'X', true);
        break;
    case 12:
        CHECK("Ctrl+A, Ctrl+X cuts all of it", box_text_is(L""));
        CHECK("to the clipboard", clipboard_is(L"HiHi"));
        CHECK("its CuttingToClipboard event", s_cut == 1);
        post_key(island, 'V', true);
        break;
    case 13:
        CHECK("Ctrl+V pastes it back", box_text_is(L"HiHi"));
        click(top, island, 240 + 60, 260 + 16);         /* (the PasswordBox) */
        break;
    case 14:
        CHECK("the click focuses the PasswordBox", focused(s_password.Get()));
        for (const wchar_t *c = L"pw1"; *c; c++)
            PostMessageW(island, WM_CHAR, *c, 1);
        break;
    case 15: {
        wrl_wrappers::HString h;
        if (s_password) s_password->get_Password(h.GetAddressOf());
        printf("xamlpagetest: Password \"%ls\"\n", h.Get() ? WindowsGetStringRawBuffer(h.Get(), nullptr) : L"");
        CHECK("typed keys reach the PasswordBox (its Password)", is(h.Get(), L"pw1"));
        /* (a password is never copied: the clipboard keeps what it had) */
        post_key(island, 'A', true);
        post_key(island, 'C', true);
        break;
    }
    case 16: {
        CHECK("Ctrl+C in a PasswordBox copies nothing", clipboard_is(L"HiHi"));
        printf("xamlpagetest: PasswordChanged %d times\n", s_password_changed);
        CHECK("its PasswordChanged fires", s_password_changed >= 1);
        int n = text_pixels(top, island, 240, 260, 120);
        printf("xamlpagetest: PasswordBox text pixels %d\n", n);
        CHECK("the PasswordBox draws its password characters", n >= 10);
        click(top, island, 24 + 100, 308 + 32);         /* (the RichEditBox) */
        break;
    }
    case 17:
        CHECK("the click focuses the RichEditBox", focused(s_rich.Get()));
        for (const wchar_t *c = L"Rich"; *c; c++)
            PostMessageW(island, WM_CHAR, *c, 1);
        break;
    case 18: {
        /* its text, through its Document (Microsoft.UI.Text's ITextDocument:
         * WinUIEdit's Windows Runtime document) */
        ctl::ComPtr<mut::ITextDocument> doc;
        wrl_wrappers::HString h;
        CHECK_HR("RichEditBox.Document", s_rich ? s_rich->get_Document(&doc) : E_FAIL);
        if (doc) doc->GetText(mut::TextGetOptions_None, h.GetAddressOf());
        const wchar_t *t = h.Get() ? WindowsGetStringRawBuffer(h.Get(), nullptr) : L"";
        printf("xamlpagetest: RichEditBox text \"%ls\"\n", t);
        CHECK("typed keys reach the RichEditBox (Document.GetText)", !wcsncmp(t, L"Rich", 4) && wcslen(t) <= 5);
        post_key(island, 'A', true);
        post_key(island, 'C', true);
        break;
    }
    case 19:
        CHECK("Ctrl+A, Ctrl+C in the RichEditBox copies its text", clipboard_is(L"Rich"));
        printf("xamlpagetest: RichEditBox TextChanged %d times\n", s_rich_changed);
        CHECK("its TextChanged fires", s_rich_changed >= 1);
        CHECK("the RichEditBox draws its text", text_pixels(top, island, 24, 308, 120) >= 20);
        /* (XAML text input, part 5: rich text's formats) Ctrl+B bolds the
         * selection, which its Document's selection reads back */
        post_key(island, 'B', true);
        break;
    case 20: {
        ctl::ComPtr<mut::ITextDocument> doc;
        ctl::ComPtr<mut::ITextSelection> sel;
        ctl::ComPtr<mut::ITextRange> range;
        ctl::ComPtr<mut::ITextCharacterFormat> format;
        mut::FormatEffect bold = mut::FormatEffect_Undefined;
        if (s_rich) s_rich->get_Document(&doc);
        if (doc) doc->get_Selection(&sel);
        if (sel) sel.As(&range);
        CHECK_HR("Document.Selection.CharacterFormat", range ? range->get_CharacterFormat(&format) : E_FAIL);
        if (format) format->get_Bold(&bold);
        printf("xamlpagetest: RichEditBox selection Bold %d\n", static_cast<int>(bold));
        CHECK("Ctrl+B in the RichEditBox bolds the selection", bold == mut::FormatEffect_On);
        break;
    }
    }
}

/* once the page is laid out (or after ten seconds): the checks, the input,
 * then the window closes and the app exits */
static HRESULT on_tick(msy::IDispatcherQueueTimer *, IInspectable *)
{
    double w = 0, h = 0;
    bool laid_out = s_panel && SUCCEEDED(s_panel->get_ActualWidth(&w)) && w > 0;
    if (s_checked && s_step < LAST_STEP) {
        HWND top = find_window();
        HWND island = find_island(top);
        if (!s_step) {
            /* the Border's pixels are its colour, SteelBlue, once the first
             * frame is drawn (the window's pixels: NOVA_PRESENT=gdi, see
             * main) */
            COLORREF c = border_pixel(top, island);
            if (c != RGB(70, 130, 180) && ++s_pixel_ticks < 20) return S_OK;
            printf("xamlpagetest: pixel at 100,100 is %d,%d,%d\n", GetRValue(c), GetGValue(c), GetBValue(c));
            CHECK("the Border is drawn in its colour", c == RGB(70, 130, 180));
        }
        if (++s_step == 1) CHECK("the content island's window", island != nullptr);
        if (island) input_step(top, island);
        if (s_step == LAST_STEP) {
            printf("xamlpagetest: page on screen\n");
            fflush(stdout);
        }
        return S_OK;
    }
    if (s_checked) {
        /* the page stays on screen for three seconds (the self-test's
         * screenshot), then the window closes and the app exits */
        if (++s_shown < 12) return S_OK;
        s_timer->Stop();
        CHECK_HR("Window.Close", s_window->Close());
        CHECK("closes its Win32 window", !find_window());
        ctl::ComPtr<xaml::IApplication> app;
        CHECK("the app", s_app && SUCCEEDED(s_app->QueryInterface(__uuidof(xaml::IApplication), reinterpret_cast<void **>(app.GetAddressOf()))));
        CHECK_HR("Application.Exit", app ? app->Exit() : E_FAIL);
        s_exit_called = true;
        return S_OK;
    }
    if (!laid_out && ++s_ticks < 40) return S_OK;
    s_checked = true;
    printf("xamlpagetest: page laid out after %d ticks\n", s_ticks);
    HWND hwnd = find_window();
    CHECK("the window's Win32 window", hwnd != nullptr);
    CHECK("is visible", hwnd && IsWindowVisible(hwnd));
    RECT rc = {};
    if (hwnd) GetClientRect(hwnd, &rc);
    wf::Rect bounds = {};
    CHECK_HR("Window.Bounds", s_window->get_Bounds(&bounds));
    printf("xamlpagetest: client %ldx%ld, bounds %gx%g\n", rc.right, rc.bottom, bounds.Width, bounds.Height);
    CHECK("are the window's client area", bounds.Width == rc.right && bounds.Height == rc.bottom && bounds.Height > 0);
    double gw = 0, gh = 0;
    CHECK("the Grid fills the window", s_grid && SUCCEEDED(s_grid->get_ActualWidth(&gw)) && SUCCEEDED(s_grid->get_ActualHeight(&gh)) &&
          gw == bounds.Width && gh == bounds.Height);
    CHECK("the Border is laid out", laid_out);
    if (s_panel) s_panel->get_ActualHeight(&h);
    printf("xamlpagetest: grid %gx%g, border %gx%g\n", gw, gh, w, h);
    CHECK("at its size", w == 200 && h == 100);
    double bw = 0, bh = 0;
    CHECK("the Button is laid out at its size", s_button && SUCCEEDED(s_button->get_ActualWidth(&bw)) &&
          SUCCEEDED(s_button->get_ActualHeight(&bh)) && bw == 120 && bh == 40);
    printf("xamlpagetest: button %gx%g\n", bw, bh);
    double tw = 0, th = 0;
    CHECK("the TextBlock is laid out to its text", s_label && SUCCEEDED(s_label->get_ActualWidth(&tw)) &&
          SUCCEEDED(s_label->get_ActualHeight(&th)) && tw > 0 && th > 0);
    printf("xamlpagetest: text %gx%g\n", tw, th);
    double xw = 0, xh = 0;
    CHECK("the TextBox is laid out at its size", s_boxElement && SUCCEEDED(s_boxElement->get_ActualWidth(&xw)) &&
          SUCCEEDED(s_boxElement->get_ActualHeight(&xh)) && xw == 200 && xh == 32);
    printf("xamlpagetest: textbox %gx%g\n", xw, xh);
    fflush(stdout);
    return S_OK;
}

/* OnLaunched: as an app's: a window, its content, activated */
HRESULT App::OnLaunched(xaml::ILaunchActivatedEventArgs *)
{
    s_launched = true;
    CHECK("OnLaunched on the thread Application.Start was called on", GetCurrentThreadId() == s_main_thread);

    /* Application.Current is the app's own object */
    ctl::ComPtr<DirectUI::FrameworkApplicationFactory> appFactory;
    ctl::ComObject<DirectUI::FrameworkApplicationFactory>::CreateInstance(appFactory.ReleaseAndGetAddressOf());
    ctl::ComPtr<xaml::IApplicationStatics> appStatics;
    ctl::ComPtr<xaml::IApplication> current;
    CHECK("Application.Current", appFactory && SUCCEEDED(appFactory.As(&appStatics)) &&
          SUCCEEDED(appStatics->get_Current(&current)) && current);

    /* the page: a Grid with a Border and a Button */
    ctl::ComPtr<DirectUI::XamlReaderFactory> reader;
    ctl::ComObject<DirectUI::XamlReaderFactory>::CreateInstance(reader.ReleaseAndGetAddressOf());
    ctl::ComPtr<xaml_markup::IXamlReaderStatics> readerStatics;
    if (reader) reader.As(&readerStatics);
    ctl::ComPtr<IInspectable> page;
    CHECK_HR("XamlReader.Load of a page", readerStatics ? readerStatics->Load(wrl_wrappers::HStringReference(
        L"<Grid " XMLNS XMLNS_X L">"
        L"<Border x:Name='panel' Width='200' Height='100' Margin='24,24,0,0' HorizontalAlignment='Left' VerticalAlignment='Top' Background='SteelBlue'/>"
        L"<TextBlock x:Name='label' Text='Hello from NovaOS' FontSize='20' Margin='240,24,0,0' HorizontalAlignment='Left' VerticalAlignment='Top'/>"
        L"<Button x:Name='go' Content='Click me' Width='120' Height='40' Margin='24,148,0,0' HorizontalAlignment='Left' VerticalAlignment='Top'/>"
        L"<TextBlock x:Name='status' Text='Not clicked' FontSize='16' Margin='168,156,0,0' HorizontalAlignment='Left' VerticalAlignment='Top'/>"
        L"<TextBlock x:Name='typed' Text='Typed: ' FontSize='16' Margin='24,212,0,0' HorizontalAlignment='Left' VerticalAlignment='Top'/>"
        L"<TextBox x:Name='box' Width='200' Height='32' Margin='24,260,0,0' HorizontalAlignment='Left' VerticalAlignment='Top'/>"
        L"<PasswordBox x:Name='password' Width='200' Height='32' Margin='240,260,0,0' HorizontalAlignment='Left' VerticalAlignment='Top'/>"
        L"<RichEditBox x:Name='rich' Width='200' Height='64' Margin='24,308,0,0' HorizontalAlignment='Left' VerticalAlignment='Top'/>"
        L"</Grid>").Get(), page.ReleaseAndGetAddressOf()) : E_FAIL);
    if (page) page.As(&s_grid);
    ctl::ComPtr<IInspectable> found;
    wrl_wrappers::HStringReference panel(L"panel"), go(L"go"), label(L"label");
    if (s_grid && SUCCEEDED(s_grid->FindName(panel.Get(), &found)) && found) found.As(&s_panel);
    if (s_grid && SUCCEEDED(s_grid->FindName(go.Get(), found.ReleaseAndGetAddressOf())) && found) found.As(&s_button);
    if (s_grid && SUCCEEDED(s_grid->FindName(label.Get(), found.ReleaseAndGetAddressOf())) && found) found.As(&s_label);
    wrl_wrappers::HStringReference status(L"status"), typed(L"typed");
    if (s_grid && SUCCEEDED(s_grid->FindName(status.Get(), found.ReleaseAndGetAddressOf())) && found) found.As(&s_status);
    if (s_grid && SUCCEEDED(s_grid->FindName(typed.Get(), found.ReleaseAndGetAddressOf())) && found) found.As(&s_typed);
    CHECK("the page's Border, TextBlocks and Button", s_panel && s_button && s_label && s_status && s_typed);
    wrl_wrappers::HStringReference boxName(L"box");
    if (s_grid && SUCCEEDED(s_grid->FindName(boxName.Get(), found.ReleaseAndGetAddressOf())) && found) {
        found.As(&s_box);
        found.As(&s_boxElement);
    }
    CHECK("the page's TextBox", s_box != nullptr);
    wrl_wrappers::HStringReference passwordName(L"password"), richName(L"rich");
    if (s_grid && SUCCEEDED(s_grid->FindName(passwordName.Get(), found.ReleaseAndGetAddressOf())) && found) found.As(&s_password);
    if (s_grid && SUCCEEDED(s_grid->FindName(richName.Get(), found.ReleaseAndGetAddressOf())) && found) found.As(&s_rich);
    CHECK("the page's PasswordBox and RichEditBox", s_password && s_rich);

    /* its handlers: the Button's Click, the page's CharacterReceived */
    ctl::ComPtr<xaml_primitives::IButtonBase> buttonBase;
    EventRegistrationToken token;
    CHECK_HR("Click handler", s_button && SUCCEEDED(s_button.As(&buttonBase)) ?
             buttonBase->add_Click(wrl::Callback<xaml::IRoutedEventHandler>(on_click).Get(), &token) : E_FAIL);
    CHECK_HR("TextChanged handler", s_box ? s_box->add_TextChanged(wrl::Callback<xaml_controls::ITextChangedEventHandler>(
                 on_text_changed).Get(), &token) : E_FAIL);
    CHECK_HR("Paste handler", s_box ? s_box->add_Paste(wrl::Callback<xaml_controls::ITextControlPasteEventHandler>(
                 [](IInspectable *, xaml_controls::ITextControlPasteEventArgs *) { s_pasted++; return S_OK; }).Get(), &token) : E_FAIL);
    CHECK_HR("CopyingToClipboard handler", s_box ? s_box->add_CopyingToClipboard(wrl::Callback<wf::ITypedEventHandler<
                 xaml_controls::TextBox *, xaml_controls::TextControlCopyingToClipboardEventArgs *>>(
                 [](xaml_controls::ITextBox *, xaml_controls::ITextControlCopyingToClipboardEventArgs *) { s_copied++; return S_OK; }).Get(),
                 &token) : E_FAIL);
    CHECK_HR("CuttingToClipboard handler", s_box ? s_box->add_CuttingToClipboard(wrl::Callback<wf::ITypedEventHandler<
                 xaml_controls::TextBox *, xaml_controls::TextControlCuttingToClipboardEventArgs *>>(
                 [](xaml_controls::ITextBox *, xaml_controls::ITextControlCuttingToClipboardEventArgs *) { s_cut++; return S_OK; }).Get(),
                 &token) : E_FAIL);
    CHECK_HR("PasswordChanged handler", s_password ? s_password->add_PasswordChanged(wrl::Callback<xaml::IRoutedEventHandler>(
                 [](IInspectable *, xaml::IRoutedEventArgs *) { s_password_changed++; return S_OK; }).Get(), &token) : E_FAIL);
    CHECK_HR("RichEditBox TextChanged handler", s_rich ? s_rich->add_TextChanged(wrl::Callback<xaml::IRoutedEventHandler>(
                 [](IInspectable *, xaml::IRoutedEventArgs *) { s_rich_changed++; return S_OK; }).Get(), &token) : E_FAIL);
    ctl::ComPtr<xaml::IUIElement> gridElement;
    CHECK_HR("CharacterReceived handler", s_grid && SUCCEEDED(s_grid.As(&gridElement)) ?
             gridElement->add_CharacterReceived(wrl::Callback<wf::ITypedEventHandler<xaml::UIElement *,
                 xaml_input::CharacterReceivedRoutedEventArgs *>>(on_character).Get(), &token) : E_FAIL);

    /* a Window (made as a projection makes one: the factory, no outer object) */
    ctl::ComPtr<DirectUI::WindowFactory> windowFactory;
    ctl::ComObject<DirectUI::WindowFactory>::CreateInstance(windowFactory.ReleaseAndGetAddressOf());
    ctl::ComPtr<xaml::IWindowFactory> factory;
    if (windowFactory) windowFactory.As(&factory);
    ctl::ComPtr<IInspectable> inner;
    CHECK_HR("a Window", factory ? factory->CreateInstance(nullptr, &inner, s_window.ReleaseAndGetAddressOf()) : E_FAIL);
    if (!s_window) return S_OK;
    CHECK("with its Win32 window", find_window() != nullptr);
    ctl::ComPtr<xaml::IUIElement> content;
    if (s_grid) s_grid.As(&content);
    CHECK_HR("Window.Content", s_window->put_Content(content.Get()));
    CHECK_HR("Window.Activate", s_window->Activate());

    /* then a timer on the thread's DispatcherQueue, which the message loop runs */
    ctl::ComPtr<msy::IDispatcherQueueStatics> queueStatics;
    ctl::ComPtr<msy::IDispatcherQueue> queue;
    CHECK("the thread's DispatcherQueue", SUCCEEDED(wf::GetActivationFactory(
        wrl_wrappers::HStringReference(RuntimeClass_Microsoft_UI_Dispatching_DispatcherQueue).Get(), queueStatics.ReleaseAndGetAddressOf())) &&
        SUCCEEDED(queueStatics->GetForCurrentThread(&queue)) && queue);
    if (!queue) return S_OK;
    CHECK_HR("a timer on it", queue->CreateTimer(&s_timer));
    if (!s_timer) return S_OK;
    s_timer->put_Interval(wf::TimeSpan{ 2500000 });     /* (250 ms) */
    s_timer->add_Tick(wrl::Callback<wf::ITypedEventHandler<msy::DispatcherQueueTimer *, IInspectable *>>(on_tick).Get(), &token);
    CHECK_HR("started", s_timer->Start());
    return S_OK;
}

/* Application.Start's callback: the app makes its Application */
static HRESULT on_start(xaml::IApplicationInitializationCallbackParams *params)
{
    s_callback = true;
    CHECK("the initialization callback, given its parameters", params != nullptr);
    CHECK("on the thread Application.Start was called on", GetCurrentThreadId() == s_main_thread);
    ctl::ComPtr<DirectUI::FrameworkApplicationFactory> appFactory;
    ctl::ComObject<DirectUI::FrameworkApplicationFactory>::CreateInstance(appFactory.ReleaseAndGetAddressOf());
    ctl::ComPtr<xaml::IApplicationFactory> factory;
    if (appFactory) appFactory.As(&factory);
    s_app = new App();
    ctl::ComPtr<xaml::IApplication> instance;
    CHECK_HR("the app's Application, composed over the framework's", factory ? factory->CreateInstance(
        static_cast<xaml::IApplicationOverrides *>(s_app), &s_app->m_inner,
        instance.ReleaseAndGetAddressOf()) : E_FAIL);
    return S_OK;
}

/* (a framework that never returns from Application.Start fails the test) */
static DWORD WINAPI watchdog(void *)
{
    Sleep(120000);
    printf("FAIL: Application.Start did not return\nxamlpagetest: %d passed, %d failed (%s)\n", pass, fail + 1,
           sizeof(void *) == 8 ? "64-bit" : "32-bit");
    fflush(stdout);
    ExitProcess(1);
}

/* The framework's own classes Application.Start activates by name (the
 * thread's WindowsXamlManager), served by this program as Windows.UI.Xaml.dll
 * serves them to a packaged app: registered for the process
 * (RoRegisterActivationFactories), as an executable serves its classes */
static HRESULT WINAPI make_factory(HSTRING cls, IActivationFactory **out)
{
    *out = nullptr;
    if (is(cls, RuntimeClass_Microsoft_UI_Xaml_Hosting_XamlSourceFocusNavigationRequest)) {
        ctl::ComPtr<DirectUI::XamlSourceFocusNavigationRequestFactory> f;
        IFC_RETURN(ctl::ComObject<DirectUI::XamlSourceFocusNavigationRequestFactory>::CreateInstance(f.ReleaseAndGetAddressOf()));
        return f.CopyTo(out);
    }
    if (!is(cls, RuntimeClass_Microsoft_UI_Xaml_Hosting_WindowsXamlManager)) return REGDB_E_CLASSNOTREG;
    ctl::ComPtr<DirectUI::WindowsXamlManagerFactory> f;
    IFC_RETURN(ctl::ComObject<DirectUI::WindowsXamlManagerFactory>::CreateInstance(f.ReleaseAndGetAddressOf()));
    return f.CopyTo(out);
}

int main()
{
    SetEnvironmentVariableA("NOVA_PRESENT", "gdi");     /* (the frames in the window's pixels: the pixel check) */
    DirectUI::StaticLockGlobalInit();
    DXamlInstanceStorage::Initialize();     /* (as the framework's DllMain does: its per-thread core's slot, */
    DirectUI::FrameworkApplication::GlobalInit();   /* the application's lock, */
    IPlatformServices *pal = nullptr;                   /* and the platform layer's services) */
    CHECK_HR("ObtainPlatformServices", ObtainPlatformServices(&pal));
    SystemThemingInterop::OverrideSystemTheme(Theming::Theme::Dark);
    s_main_thread = GetCurrentThreadId();
    wrl_wrappers::HStringReference manager(RuntimeClass_Microsoft_UI_Xaml_Hosting_WindowsXamlManager),
        request(RuntimeClass_Microsoft_UI_Xaml_Hosting_XamlSourceFocusNavigationRequest);
    HSTRING classes[] = { manager.Get(), request.Get() };
    PFNGETACTIVATIONFACTORY makers[] = { make_factory, make_factory };
    RO_REGISTRATION_COOKIE cookie = nullptr;
    CHECK_HR("RoRegisterActivationFactories (the framework's classes)", RoRegisterActivationFactories(classes, makers, 2, &cookie));
    CloseHandle(CreateThread(nullptr, 0, watchdog, nullptr, 0, nullptr));

    ctl::ComPtr<DirectUI::FrameworkApplicationFactory> appFactory;
    ctl::ComObject<DirectUI::FrameworkApplicationFactory>::CreateInstance(appFactory.ReleaseAndGetAddressOf());
    ctl::ComPtr<xaml::IApplicationStatics> statics;
    CHECK("Application's statics", appFactory && SUCCEEDED(appFactory.As(&statics)));
    if (!statics) return 1;
    ctl::ComPtr<xaml::IApplication> none;
    CHECK("no Application.Current before Application.Start", FAILED(statics->get_Current(&none)) || !none);

    CHECK_HR("Application.Start", statics->Start(wrl::Callback<xaml::IApplicationInitializationCallback>(on_start).Get()));
    CHECK("the initialization callback ran", s_callback);
    CHECK("OnLaunched ran", s_launched);
    CHECK("the timer ran the checks", s_checked);
    CHECK("Application.Exit returned from Application.Start", s_exit_called);

    RoRevokeActivationFactories(cookie);
    printf("xamlpagetest: %d passed, %d failed (%s)\n", pass, fail, sizeof(void *) == 8 ? "64-bit" : "32-bit");
    fflush(stdout);
    ExitProcess(fail != 0);
}
