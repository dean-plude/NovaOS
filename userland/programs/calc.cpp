/*
 * calc.exe — Calculator, on Windows Calculator's own engine.
 *
 * The arithmetic is Microsoft's: CalcManager from
 * https://github.com/microsoft/calculator (MIT), vendored unchanged under
 * third_party/calcmanager and compiled into this program ("sources" in
 * calc.json): the infinite-precision rational arithmetic (Ratpack), the
 * engine's operator precedence, the expression line, memory and the
 * error strings are the shipped app's.  Windows' Calculator draws that
 * engine with XAML in a UWP package; NovaOS has no Windows Runtime or
 * XAML yet, so this program draws the same layouts with the GDI: the
 * Standard, Scientific and Programmer calculators of Windows 11's
 * Calculator, with its keyboard shortcuts.
 *
 * Keys, as Windows Calculator's: Alt+1 Standard, Alt+2 Scientific, Alt+4
 * Programmer; digits, . + - * / Enter = Esc (C) Delete (CE) Backspace,
 * F9 (+/-), % ; r (1/x), @ (square root), q (x²), ^ or y (x^y), ( ), s c t
 * (sin cos tan), l (log), n (ln), ! (n!), p (pi), x (exp), v (F-E), F3 F4
 * F5 (DEG RAD GRAD); in Programmer a-f, & | ~ < > ^ (XOR), F5-F8 (HEX DEC
 * OCT BIN); Ctrl+M MS, Ctrl+P M+, Ctrl+Q M-, Ctrl+R MR, Ctrl+L MC; Ctrl+C
 * copies the result and Ctrl+V pastes an expression.  What it shows goes
 * to the debug log (OutputDebugString: "calc: ..."), which the OS test
 * reads (tests/selftest/core/167-calc.py).
 */
#include <windows.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <memory>
#include <string>
#include <vector>

#include "CalculatorManager.h"
#include "CalculatorResource.h"

using namespace CalculationManager;

static void say(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
}

static std::string utf8(const std::wstring &w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}

/* -----------------------------------------------------------------------
 * The engine's strings: Calculator's en-US CEngineStrings.resw
 * (third_party/calcmanager/Resources/en-US), and the number format
 * ----------------------------------------------------------------------- */
static const struct { const wchar_t *id, *text; } g_strings[] = {
    { L"10", L"Rsh" }, { L"100", L"Invalid input" }, { L"101", L"Result is undefined" },
    { L"105", L"Not enough memory" }, { L"107", L"Overflow" }, { L"108", L"Result not defined" },
    { L"11", L"\x00F7" }, { L"118", L"Result not defined" }, { L"119", L"Overflow" },
    { L"12", L"\x00D7" }, { L"120", L"Overflow" }, { L"13", L"+" }, { L"14", L"-" },
    { L"15", L"Mod" }, { L"16", L"yroot" }, { L"17", L"^" }, { L"18", L"Int" }, { L"19", L"RoL" },
    { L"2", L"CE" }, { L"20", L"RoR" }, { L"21", L"NOT" }, { L"22", L"sin" }, { L"23", L"cis" },
    { L"24", L"tan" }, { L"25", L"sinh" }, { L"26", L"cosh" }, { L"27", L"tanh" }, { L"28", L"ln" },
    { L"29", L"log" }, { L"30", L"\x221A" }, { L"35", L"dms" }, { L"37", L"10^" }, { L"38", L"%" },
    { L"4", L"." }, { L"40", L"Pi" }, { L"41", L"=" }, { L"47", L"Exp" }, { L"48", L"(" },
    { L"49", L")" }, { L"6", L"AND" }, { L"66", L"frac" },
    { L"67", L"sin\x2080" }, { L"68", L"cos\x2080" }, { L"69", L"tan\x2080" }, { L"7", L"OR" },
    { L"70", L"sin\x2080\x207B\x00B9" }, { L"71", L"cos\x2080\x207B\x00B9" }, { L"72", L"tan\x2080\x207B\x00B9" },
    { L"73", L"sin\x1D63" }, { L"74", L"cos\x1D63" }, { L"75", L"tan\x1D63" },
    { L"76", L"sin\x1D63\x207B\x00B9" }, { L"77", L"cos\x1D63\x207B\x00B9" }, { L"78", L"tan\x1D63\x207B\x00B9" },
    { L"79", L"sin\x2089" }, { L"8", L"XOR" }, { L"80", L"cos\x2089" }, { L"81", L"tan\x2089" },
    { L"82", L"sin\x2089\x207B\x00B9" }, { L"83", L"cos\x2089\x207B\x00B9" }, { L"84", L"tan\x2089\x207B\x00B9" },
    { L"85", L"sinh\x207B\x00B9" }, { L"86", L"cosh\x207B\x00B9" }, { L"87", L"tanh\x207B\x00B9" },
    { L"88", L"e^" }, { L"89", L"10^" }, { L"9", L"Lsh" }, { L"90", L"\x221A" }, { L"91", L"sqr" },
    { L"92", L"cube" }, { L"94", L"fact" }, { L"95", L"1/" }, { L"96", L"degrees" }, { L"97", L"negate" },
    { L"99", L"Cannot divide by zero" },
    { L"SecDeg", L"sec\x2080" }, { L"SecRad", L"sec\x1D63" }, { L"SecGrad", L"sec\x2089" },
    { L"InverseSecDeg", L"sec\x2080\x207B\x00B9" }, { L"InverseSecRad", L"sec\x1D63\x207B\x00B9" },
    { L"InverseSecGrad", L"sec\x2089\x207B\x00B9" },
    { L"CscDeg", L"csc\x2080" }, { L"CscRad", L"csc\x1D63" }, { L"CscGrad", L"csc\x2089" },
    { L"InverseCscDeg", L"csc\x2080\x207B\x00B9" }, { L"InverseCscRad", L"csc\x1D63\x207B\x00B9" },
    { L"InverseCscGrad", L"csc\x2089\x207B\x00B9" },
    { L"CotDeg", L"cot\x2080" }, { L"CotRad", L"cot\x1D63" }, { L"CotGrad", L"cot\x2089" },
    { L"InverseCotDeg", L"cot\x2080\x207B\x00B9" }, { L"InverseCotRad", L"cot\x1D63\x207B\x00B9" },
    { L"InverseCotGrad", L"cot\x2089\x207B\x00B9" },
    { L"Sech", L"sech" }, { L"InverseSech", L"sech\x207B\x00B9" }, { L"Csch", L"csch" },
    { L"InverseCsch", L"csch\x207B\x00B9" }, { L"Coth", L"coth" }, { L"InverseCoth", L"coth\x207B\x00B9" },
    { L"TwoPowX", L"2^" }, { L"LogBaseY", L"log base" }, { L"Abs", L"abs" }, { L"Ceil", L"ceil" },
    { L"Floor", L"floor" }, { L"Nand", L"NAND" }, { L"Nor", L"NOR" }, { L"CubeRoot", L"cuberoot" },
    { L"ProgrammerMod", L"%" },
    /* the number format (Windows' locale settings, en-US) */
    { L"sDecimal", L"." }, { L"sThousand", L"," }, { L"sGrouping", L"3;0" },
};

class Resources : public IResourceProvider
{
public:
    std::wstring GetCEngineString(std::wstring_view id) override
    {
        for (auto &s : g_strings)
            if (id == s.id) return s.text;
        return L"";
    }
};

/* -----------------------------------------------------------------------
 * What the engine shows
 * ----------------------------------------------------------------------- */
static HWND g_wnd;
static std::wstring g_primary = L"0", g_expr;
static bool g_error;
static std::vector<std::wstring> g_memory;
static unsigned g_parens;

static void refresh();

class Display : public ICalcDisplay
{
public:
    void SetPrimaryDisplay(const std::wstring &text, bool isError) override
    {
        g_primary = text;
        g_error = isError;
        refresh();
    }
    void SetIsInError(bool isError) override { g_error = isError; }
    void SetExpressionDisplay(std::shared_ptr<std::vector<std::pair<std::wstring, int>>> const &tokens,
                              std::shared_ptr<std::vector<std::shared_ptr<IExpressionCommand>>> const &) override
    {
        g_expr.clear();
        for (auto &t : *tokens) {                /* (operators come with their spaces) */
            if (t.first == L"=" && !g_expr.empty() && g_expr.back() != L' ') g_expr += L' ';
            g_expr += t.first;
        }
        refresh();
    }
    void SetParenthesisNumber(unsigned int count) override { g_parens = count; refresh(); }
    void OnNoRightParenAdded() override {}
    void MaxDigitsReached() override {}
    void BinaryOperatorReceived() override {}
    void OnHistoryItemAdded(unsigned int) override {}
    void SetMemorizedNumbers(const std::vector<std::wstring> &numbers) override { g_memory = numbers; refresh(); }
    void MemoryItemChanged(unsigned int) override {}
    void InputChanged() override {}
};

static Resources g_res;
static Display g_display;
static CalculatorManager *g_calc;

/* -----------------------------------------------------------------------
 * The three calculators' buttons, as Windows 11's Calculator lays them out
 * ----------------------------------------------------------------------- */
enum Mode { M_STANDARD, M_SCIENTIFIC, M_PROGRAMMER };
enum Kind { K_FN, K_NUM, K_EQ };
enum {                          /* actions of the program's own (not the engine's) */
    A_SECOND = -1, A_ANGLE = -2, A_WORD = -3,
    A_MC = -10, A_MR = -11, A_MPLUS = -12, A_MMINUS = -13, A_MS = -14,
};
#define C(x) ((int)Command::x)

struct Button {
    const wchar_t *label, *label2;  /* label2: with 2nd */
    int cmd, cmd2;
    Kind kind;
};

static const Button std_rows[][4] = {
    { { L"%", 0, C(CommandPERCENT) }, { L"CE", 0, C(CommandCENTR) }, { L"C", 0, C(CommandCLEAR) }, { L"\x232B", 0, C(CommandBACK) } },
    { { L"\x00B9\x2044\x2093", 0, C(CommandREC) }, { L"x\x00B2", 0, C(CommandSQR) }, { L"\x00B2\x221Ax", 0, C(CommandSQRT) }, { L"\x00F7", 0, C(CommandDIV) } },
    { { L"7", 0, C(Command7), 0, K_NUM }, { L"8", 0, C(Command8), 0, K_NUM }, { L"9", 0, C(Command9), 0, K_NUM }, { L"\x00D7", 0, C(CommandMUL) } },
    { { L"4", 0, C(Command4), 0, K_NUM }, { L"5", 0, C(Command5), 0, K_NUM }, { L"6", 0, C(Command6), 0, K_NUM }, { L"\x2212", 0, C(CommandSUB) } },
    { { L"1", 0, C(Command1), 0, K_NUM }, { L"2", 0, C(Command2), 0, K_NUM }, { L"3", 0, C(Command3), 0, K_NUM }, { L"+", 0, C(CommandADD) } },
    { { L"+/\x2212", 0, C(CommandSIGN), 0, K_NUM }, { L"0", 0, C(Command0), 0, K_NUM }, { L".", 0, C(CommandPNT), 0, K_NUM }, { L"=", 0, C(CommandEQU), 0, K_EQ } },
};

static const Button sci_rows[][5] = {
    { { L"sin", L"sin\x207B\x00B9", C(CommandSIN), C(CommandASIN) }, { L"cos", L"cos\x207B\x00B9", C(CommandCOS), C(CommandACOS) },
      { L"tan", L"tan\x207B\x00B9", C(CommandTAN), C(CommandATAN) }, { L"floor", 0, C(CommandFloor) }, { L"ceil", 0, C(CommandCeil) } },
    { { L"2\x207F\x1D48", 0, A_SECOND }, { L"\x03C0", 0, C(CommandPI) }, { L"e", 0, C(CommandEuler) }, { L"C", 0, C(CommandCLEAR) }, { L"\x232B", 0, C(CommandBACK) } },
    { { L"x\x00B2", L"x\x00B3", C(CommandSQR), C(CommandCUB) }, { L"\x00B9\x2044\x2093", 0, C(CommandREC) }, { L"|x|", 0, C(CommandAbs) },
      { L"exp", 0, C(CommandEXP) }, { L"mod", 0, C(CommandMOD) } },
    { { L"\x00B2\x221Ax", L"\x00B3\x221Ax", C(CommandSQRT), C(CommandCUBEROOT) }, { L"(", 0, C(CommandOPENP) }, { L")", 0, C(CommandCLOSEP) },
      { L"n!", 0, C(CommandFAC) }, { L"\x00F7", 0, C(CommandDIV) } },
    { { L"x\x02B8", L"\x02B8\x221Ax", C(CommandPWR), C(CommandROOT) }, { L"7", 0, C(Command7), 0, K_NUM }, { L"8", 0, C(Command8), 0, K_NUM },
      { L"9", 0, C(Command9), 0, K_NUM }, { L"\x00D7", 0, C(CommandMUL) } },
    { { L"10\x02E3", L"2\x02E3", C(CommandPOW10), C(CommandPOW2) }, { L"4", 0, C(Command4), 0, K_NUM }, { L"5", 0, C(Command5), 0, K_NUM },
      { L"6", 0, C(Command6), 0, K_NUM }, { L"\x2212", 0, C(CommandSUB) } },
    { { L"log", L"log\x1D67x", C(CommandLOG), C(CommandLogBaseY) }, { L"1", 0, C(Command1), 0, K_NUM }, { L"2", 0, C(Command2), 0, K_NUM },
      { L"3", 0, C(Command3), 0, K_NUM }, { L"+", 0, C(CommandADD) } },
    { { L"ln", L"e\x02E3", C(CommandLN), C(CommandPOWE) }, { L"+/\x2212", 0, C(CommandSIGN), 0, K_NUM }, { L"0", 0, C(Command0), 0, K_NUM },
      { L".", 0, C(CommandPNT), 0, K_NUM }, { L"=", 0, C(CommandEQU), 0, K_EQ } },
};

static const Button prog_bits[6] = {
    { L"AND", 0, C(CommandAnd) }, { L"OR", 0, C(CommandOR) }, { L"NOT", 0, C(CommandNot) },
    { L"NAND", 0, C(CommandNand) }, { L"NOR", 0, C(CommandNor) }, { L"XOR", 0, C(CommandXor) },
};

static const Button prog_rows[][5] = {
    { { L"A", 0, C(CommandA), 0, K_NUM }, { L"<<", 0, C(CommandLSHF) }, { L">>", 0, C(CommandRSHF) }, { L"C", 0, C(CommandCLEAR) }, { L"\x232B", 0, C(CommandBACK) } },
    { { L"B", 0, C(CommandB), 0, K_NUM }, { L"(", 0, C(CommandOPENP) }, { L")", 0, C(CommandCLOSEP) }, { L"%", 0, C(CommandMOD) }, { L"\x00F7", 0, C(CommandDIV) } },
    { { L"C", 0, C(CommandC), 0, K_NUM }, { L"7", 0, C(Command7), 0, K_NUM }, { L"8", 0, C(Command8), 0, K_NUM }, { L"9", 0, C(Command9), 0, K_NUM }, { L"\x00D7", 0, C(CommandMUL) } },
    { { L"D", 0, C(CommandD), 0, K_NUM }, { L"4", 0, C(Command4), 0, K_NUM }, { L"5", 0, C(Command5), 0, K_NUM }, { L"6", 0, C(Command6), 0, K_NUM }, { L"\x2212", 0, C(CommandSUB) } },
    { { L"E", 0, C(CommandE), 0, K_NUM }, { L"1", 0, C(Command1), 0, K_NUM }, { L"2", 0, C(Command2), 0, K_NUM }, { L"3", 0, C(Command3), 0, K_NUM }, { L"+", 0, C(CommandADD) } },
    { { L"F", 0, C(CommandF), 0, K_NUM }, { L"+/\x2212", 0, C(CommandSIGN), 0, K_NUM }, { L"0", 0, C(Command0), 0, K_NUM }, { L".", 0, C(CommandPNT), 0, K_NUM }, { L"=", 0, C(CommandEQU), 0, K_EQ } },
};

static const Button mem_row[5] = {
    { L"MC", 0, A_MC }, { L"MR", 0, A_MR }, { L"M+", 0, A_MPLUS }, { L"M\x2212", 0, A_MMINUS }, { L"MS", 0, A_MS },
};

static Mode g_mode = M_STANDARD;
static bool g_second;
static int g_angle;             /* 0 DEG, 1 RAD, 2 GRAD */
static bool g_fe;
static int g_radix = 10;        /* Programmer */
static int g_word;              /* 0 QWORD, 1 DWORD, 2 WORD, 3 BYTE */
static const wchar_t *const g_mode_name[] = { L"Standard", L"Scientific", L"Programmer" };
static const wchar_t *const g_angle_name[] = { L"DEG", L"RAD", L"GRAD" };
static const wchar_t *const g_word_name[] = { L"QWORD", L"DWORD", L"WORD", L"BYTE" };

/* a button on screen */
struct Hot {
    RECT r;
    const Button *b;
    int radix;                  /* a radix line (Programmer), else 0 */
};
static std::vector<Hot> g_hot;
static int g_hover = -1, g_down = -1;

static bool enabled(const Button *b)
{
    if (!b) return true;
    int c = b->cmd;
    if (g_mode == M_PROGRAMMER) {
        if (c >= C(Command0) && c <= C(CommandF)) return c - C(Command0) < g_radix;
        if (c == C(CommandPNT)) return false;
    }
    if ((c == A_MC || c == A_MR) && g_memory.empty()) return false;
    return true;
}

static void log_display()
{
    say("calc: %ls display %s%s%s\n", g_mode_name[g_mode], utf8(g_primary).c_str(),
        g_expr.empty() ? "" : " after ", utf8(g_expr).c_str());
}

static void send(int cmd)
{
    if (cmd == C(CommandCLEAR) && g_mode == M_PROGRAMMER) {
        g_calc->SendCommand(Command::CommandCLEAR);
        return;
    }
    g_calc->SendCommand((Command)cmd);
}

static void set_radix(int radix)
{
    static const struct { int radix; Command cmd; } r[] = {
        { 16, Command::CommandHex }, { 10, Command::CommandDec }, { 8, Command::CommandOct }, { 2, Command::CommandBin } };
    for (auto &e : r)
        if (e.radix == radix) {
            g_radix = radix;
            g_calc->SendCommand(e.cmd);
            say("calc: radix %d\n", radix);
        }
    refresh();
}

static void set_mode(Mode m)
{
    static const Command cmds[] = { Command::ModeBasic, Command::ModeScientific, Command::ModeProgrammer };
    g_mode = m;
    g_second = false;
    g_calc->SendCommand(cmds[m]);
    if (m == M_SCIENTIFIC) {
        static const Command a[] = { Command::CommandDEG, Command::CommandRAD, Command::CommandGRAD };
        g_calc->SendCommand(a[g_angle]);
    }
    if (m == M_PROGRAMMER) {
        g_radix = 10;
        static const Command w[] = { Command::CommandQword, Command::CommandDword, Command::CommandWord, Command::CommandByte };
        g_calc->SendCommand(w[g_word]);
    }
    say("calc: mode %ls\n", g_mode_name[m]);
    static const int size[][2] = { { 340, 540 }, { 420, 620 }, { 420, 620 } };
    RECT wr;
    GetWindowRect(g_wnd, &wr);
    if (wr.right - wr.left < size[m][0] || wr.bottom - wr.top < size[m][1])
        SetWindowPos(g_wnd, NULL, 0, 0, std::max((int)(wr.right - wr.left), size[m][0]),
                     std::max((int)(wr.bottom - wr.top), size[m][1]), SWP_NOMOVE | SWP_NOZORDER);
    refresh();
}

static void press(const Button *b)
{
    if (!enabled(b)) return;
    int cmd = g_second && b->label2 ? b->cmd2 : b->cmd;
    switch (cmd) {
    case A_SECOND: g_second = !g_second; refresh(); return;
    case A_ANGLE: {
        static const Command a[] = { Command::CommandDEG, Command::CommandRAD, Command::CommandGRAD };
        g_angle = (g_angle + 1) % 3;
        g_calc->SendCommand(a[g_angle]);
        say("calc: angle %ls\n", g_angle_name[g_angle]);
        refresh();
        return; }
    case A_WORD: {
        static const Command w[] = { Command::CommandQword, Command::CommandDword, Command::CommandWord, Command::CommandByte };
        g_word = (g_word + 1) % 4;
        g_calc->SendCommand(w[g_word]);
        say("calc: word %ls\n", g_word_name[g_word]);
        refresh();
        return; }
    case A_MC: g_calc->MemorizedNumberClearAll(); return;
    case A_MR: if (!g_memory.empty()) g_calc->MemorizedNumberLoad(0); return;
    case A_MPLUS: g_calc->MemorizedNumberAdd(0); return;
    case A_MMINUS: g_calc->MemorizedNumberSubtract(0); return;
    case A_MS: g_calc->MemorizeNumber(); return;
    }
    if (cmd == C(CommandFE)) g_fe = !g_fe;
    send(cmd);
    if (g_second && b->label2) { g_second = false; refresh(); }
}

/* -----------------------------------------------------------------------
 * Drawing, in Windows 11 Calculator's light colours
 * ----------------------------------------------------------------------- */
#define RGBC(r, g, b) RGB(r, g, b)
static const COLORREF COL_BG = RGBC(0xF3, 0xF3, 0xF3), COL_NUM = RGBC(0xFF, 0xFF, 0xFF), COL_FN = RGBC(0xF9, 0xF9, 0xF9),
    COL_EQ = RGBC(0x00, 0x5F, 0xB8), COL_TEXT = RGBC(0x1A, 0x1A, 0x1A), COL_DIM = RGBC(0x60, 0x60, 0x60),
    COL_OFF = RGBC(0xB0, 0xB0, 0xB0), COL_EDGE = RGBC(0xE5, 0xE5, 0xE5), COL_ACCENT = RGBC(0x00, 0x5F, 0xB8);
static HFONT g_font, g_small, g_title, g_expr_font;

static HFONT display_font(int px)
{
    static HFONT cache[200];
    if (px < 12) px = 12;
    if (px > 199) px = 199;
    if (!cache[px])
        cache[px] = CreateFontW(-px, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    return cache[px];
}

static void fill(HDC dc, const RECT &r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static void round_box(HDC dc, const RECT &r, COLORREF fillc, COLORREF edge)
{
    HBRUSH b = CreateSolidBrush(fillc);
    HPEN p = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, 8, 8);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

static void text(HDC dc, const std::wstring &s, RECT r, HFONT f, COLORREF c, UINT how)
{
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, s.c_str(), (int)s.size(), &r, how | DT_SINGLELINE | DT_NOPREFIX);
}

static void add_button(HDC dc, RECT r, const Button *b)
{
    int i = (int)g_hot.size();
    g_hot.push_back({ r, b, 0 });
    bool on = enabled(b), hover = i == g_hover && on, down = i == g_down && on;
    COLORREF bg = b->kind == K_EQ ? COL_EQ : b->kind == K_NUM ? COL_NUM : COL_FN;
    if (b->kind == K_EQ) {
        if (hover) bg = RGBC(0x19, 0x6E, 0xBF);
        if (down) bg = RGBC(0x31, 0x7E, 0xC5);
    } else if (down) {
        bg = RGBC(0xF0, 0xF0, 0xF0);
    } else if (hover) {
        bg = RGBC(0xF6, 0xF6, 0xF6);
    }
    if (b->cmd == A_SECOND && g_second) bg = COL_ACCENT;
    round_box(dc, r, bg, b->kind == K_EQ || (b->cmd == A_SECOND && g_second) ? bg : COL_EDGE);
    const wchar_t *label = g_second && b->label2 ? b->label2 : b->label;
    COLORREF fg = !on ? COL_OFF : b->kind == K_EQ || (b->cmd == A_SECOND && g_second) ? RGBC(0xFF, 0xFF, 0xFF) : COL_TEXT;
    text(dc, label, r, b->kind == K_NUM ? g_font : g_small, fg, DT_CENTER | DT_VCENTER);
}

static void grid(HDC dc, RECT area, const Button *rows, int nrows, int ncols)
{
    int gap = 2, w = area.right - area.left, h = area.bottom - area.top;
    for (int y = 0; y < nrows; y++)
        for (int x = 0; x < ncols; x++) {
            RECT r = { area.left + x * w / ncols + gap / 2, area.top + y * h / nrows + gap / 2,
                       area.left + (x + 1) * w / ncols - gap / 2, area.top + (y + 1) * h / nrows - gap / 2 };
            add_button(dc, r, &rows[y * ncols + x]);
        }
}

static void paint(HDC dc, int cw, int ch)
{
    g_hot.clear();
    RECT all = { 0, 0, cw, ch };
    fill(dc, all, COL_BG);
    SetBkMode(dc, TRANSPARENT);

    /* the title bar's menu button and the calculator's name */
    for (int i = 0; i < 3; i++) {                   /* (the menu button's three lines) */
        RECT bar = { 20, 16 + i * 6, 36, 18 + i * 6 };
        fill(dc, bar, COL_TEXT);
    }
    RECT r = { 52, 6, cw - 8, 40 };
    text(dc, g_mode_name[g_mode], r, g_title, COL_TEXT, DT_LEFT | DT_VCENTER);
    int y = 44;

    /* the expression line and the result, as large as fits */
    r = { 12, y, cw - 14, y + 22 };
    std::wstring expr = g_expr;
    if (g_parens && g_mode != M_STANDARD) expr += L"  (" + std::to_wstring(g_parens);
    text(dc, expr, r, g_expr_font, COL_DIM, DT_RIGHT | DT_VCENTER | DT_END_ELLIPSIS);
    y += 22;
    int big = g_mode == M_PROGRAMMER ? 48 : 64;
    int px = big;
    for (; px > 16; px -= 2) {
        SelectObject(dc, display_font(px));
        SIZE sz;
        GetTextExtentPoint32W(dc, g_primary.c_str(), (int)g_primary.size(), &sz);
        if (sz.cx <= cw - 28) break;
    }
    r = { 12, y, cw - 14, y + big + 12 };
    text(dc, g_primary, r, display_font(g_error ? std::min(px, 32) : px), COL_TEXT, DT_RIGHT | DT_VCENTER);
    y += big + 16;

    if (g_mode == M_PROGRAMMER) {
        /* the value in each radix; clicking one makes it the input's */
        static const struct { int radix; const wchar_t *name; } lines[] = {
            { 16, L"HEX" }, { 10, L"DEC" }, { 8, L"OCT" }, { 2, L"BIN" } };
        for (auto &l : lines) {
            std::wstring v = g_error ? L"" : g_calc->GetResultForRadix(l.radix, 64, true);
            RECT lr = { 8, y, cw - 8, y + 24 };
            g_hot.push_back({ lr, nullptr, l.radix });
            if (l.radix == g_radix) {
                RECT bar = { 8, y + 5, 11, y + 19 };
                fill(dc, bar, COL_ACCENT);
            }
            RECT nr = { 16, y, 60, y + 24 };
            text(dc, l.name, nr, l.radix == g_radix ? g_font : g_small, COL_TEXT, DT_LEFT | DT_VCENTER);
            RECT vr = { 64, y, cw - 12, y + 24 };
            text(dc, v, vr, g_small, COL_TEXT, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
            y += 24;
        }
        y += 4;
        static const Button word = { L"QWORD", 0, A_WORD };
        static Button w;
        w = word;
        w.label = g_word_name[g_word];
        RECT wr = { 8, y, 8 + 90, y + 30 };
        g_hot.push_back({ wr, &w, 0 });
        text(dc, w.label, wr, g_small, COL_TEXT, DT_CENTER | DT_VCENTER);
        y += 34;
        RECT area = { 6, y, cw - 6, y + 40 };
        grid(dc, area, prog_bits, 1, 6);
        y += 42;
        area = { 6, y, cw - 6, ch - 6 };
        grid(dc, area, &prog_rows[0][0], 6, 5);
        return;
    }

    if (g_mode == M_SCIENTIFIC) {
        static Button angle = { L"DEG", 0, A_ANGLE }, fe = { L"F-E", 0, C(CommandFE) };
        angle.label = g_angle_name[g_angle];
        RECT ar = { 8, y, 8 + 64, y + 28 }, fr = { 76, y, 76 + 64, y + 28 };
        g_hot.push_back({ ar, &angle, 0 });
        g_hot.push_back({ fr, &fe, 0 });
        text(dc, angle.label, ar, g_small, COL_TEXT, DT_CENTER | DT_VCENTER);
        text(dc, fe.label, fr, g_small, g_fe ? COL_ACCENT : COL_TEXT, DT_CENTER | DT_VCENTER);
        y += 30;
    }

    /* memory: MC MR M+ M- MS */
    for (int i = 0; i < 5; i++) {
        RECT mr = { 6 + i * (cw - 12) / 5, y, 6 + (i + 1) * (cw - 12) / 5, y + 30 };
        g_hot.push_back({ mr, &mem_row[i], 0 });
        text(dc, mem_row[i].label, mr, g_small, enabled(&mem_row[i]) ? COL_TEXT : COL_OFF, DT_CENTER | DT_VCENTER);
    }
    y += 34;
    RECT area = { 6, y, cw - 6, ch - 6 };
    if (g_mode == M_STANDARD) grid(dc, area, &std_rows[0][0], 6, 4);
    else grid(dc, area, &sci_rows[0][0], 8, 5);
}

static void refresh()
{
    if (g_wnd) InvalidateRect(g_wnd, NULL, FALSE);
}

/* -----------------------------------------------------------------------
 * Keyboard and clipboard
 * ----------------------------------------------------------------------- */
static int char_command(wchar_t ch)
{
    if (ch >= L'0' && ch <= L'9') return C(Command0) + (ch - L'0');
    if (g_mode == M_PROGRAMMER) {
        wchar_t l = (wchar_t)towlower(ch);
        if (l >= L'a' && l <= L'f') return C(CommandA) + (l - L'a');
        switch (ch) {
        case L'&': return C(CommandAnd);
        case L'|': return C(CommandOR);
        case L'~': return C(CommandNot);
        case L'^': return C(CommandXor);
        case L'<': return C(CommandLSHF);
        case L'>': return C(CommandRSHF);
        case L'%': return C(CommandMOD);
        }
    }
    switch (ch) {
    case L'.': case L',': return C(CommandPNT);
    case L'+': return C(CommandADD);
    case L'-': return C(CommandSUB);
    case L'*': return C(CommandMUL);
    case L'/': return C(CommandDIV);
    case L'=': case L'\r': return C(CommandEQU);
    case L'%': return C(CommandPERCENT);
    case L'(': return C(CommandOPENP);
    case L')': return C(CommandCLOSEP);
    case L'r': return C(CommandREC);
    case L'@': return C(CommandSQRT);
    case L'q': return C(CommandSQR);
    }
    if (g_mode == M_SCIENTIFIC) {
        switch (ch) {
        case L'^': case L'y': return C(CommandPWR);
        case L's': return C(CommandSIN);
        case L'o': return C(CommandCOS);
        case L't': return C(CommandTAN);
        case L'l': return C(CommandLOG);
        case L'n': return C(CommandLN);
        case L'!': return C(CommandFAC);
        case L'p': return C(CommandPI);
        case L'x': return C(CommandEXP);
        case L'v': return C(CommandFE);
        case L'#': return C(CommandCUB);
        }
    }
    return -1;
}

static void key_command(int cmd)
{
    Button b = { L"", 0, cmd };
    if (cmd >= C(Command0) && cmd <= C(CommandF)) b.kind = K_NUM;
    press(&b);
}

static void copy_result()
{
    if (!OpenClipboard(g_wnd)) return;
    EmptyClipboard();
    std::wstring s;
    for (wchar_t c : g_primary)
        if (c != L',') s += c;                     /* (as Calculator copies: no group separators) */
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
    if (h) {
        memcpy(GlobalLock(h), s.c_str(), (s.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
    say("calc: copied %s\n", utf8(s).c_str());
}

static void paste()
{
    if (!OpenClipboard(g_wnd)) return;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    std::wstring s = h ? (const wchar_t *)GlobalLock(h) : L"";
    if (h) GlobalUnlock(h);
    CloseClipboard();
    for (wchar_t c : s) {
        if (c == L' ' || c == L'\n' || c == L'\t') continue;
        int cmd = char_command(c);
        if (cmd >= 0) key_command(cmd);
    }
}

/* -----------------------------------------------------------------------
 * The window
 * ----------------------------------------------------------------------- */
static int hit(int x, int y)
{
    for (size_t i = 0; i < g_hot.size(); i++) {
        const RECT &r = g_hot[i].r;
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return (int)i;
    }
    return -1;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT cr;
        GetClientRect(h, &cr);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bm = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
        HGDIOBJ old = SelectObject(mem, bm);
        paint(mem, cr.right, cr.bottom);
        BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bm);
        DeleteDC(mem);
        EndPaint(h, &ps);
        static std::wstring last;
        std::wstring now = g_primary + L"|" + g_expr;
        if (now != last) {
            last = now;
            log_display();
        }
        return 0; }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        refresh();
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = 320;
        mm->ptMinTrackSize.y = 500;
        return 0; }
    case WM_MOUSEMOVE: {
        int i = hit((short)LOWORD(lp), (short)HIWORD(lp));
        if (i != g_hover) {
            g_hover = i;
            TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
            refresh();
        }
        return 0; }
    case WM_MOUSELEAVE:
        g_hover = -1;
        refresh();
        return 0;
    case WM_LBUTTONDOWN:
        g_down = hit((short)LOWORD(lp), (short)HIWORD(lp));
        SetCapture(h);
        refresh();
        return 0;
    case WM_LBUTTONUP: {
        int i = hit((short)LOWORD(lp), (short)HIWORD(lp));
        ReleaseCapture();
        int was = g_down;
        g_down = -1;
        if (i >= 0 && i == was) {
            if (g_hot[i].radix) set_radix(g_hot[i].radix);
            else if (g_hot[i].b) press(g_hot[i].b);
        } else if (was < 0) {
            /* the title: a click on the menu button cycles the calculators */
            short x = (short)LOWORD(lp), y = (short)HIWORD(lp);
            if (x < 48 && y < 44) set_mode((Mode)((g_mode + 1) % 3));
        }
        refresh();
        return 0; }
    case WM_SYSKEYDOWN:
        if (wp == '1') { set_mode(M_STANDARD); return 0; }
        if (wp == '2') { set_mode(M_SCIENTIFIC); return 0; }
        if (wp == '4') { set_mode(M_PROGRAMMER); return 0; }
        break;
    case WM_SYSCHAR:
        if (wp == '1' || wp == '2' || wp == '3' || wp == '4' || wp == '5') return 0;   /* (no beep) */
        break;
    case WM_KEYDOWN: {
        bool ctrl = GetKeyState(VK_CONTROL) < 0;
        if (ctrl) {
            switch (wp) {
            case 'C': copy_result(); return 0;
            case 'V': paste(); return 0;
            case 'M': press(&mem_row[4]); return 0;
            case 'P': press(&mem_row[2]); return 0;
            case 'Q': press(&mem_row[3]); return 0;
            case 'R': press(&mem_row[1]); return 0;
            case 'L': press(&mem_row[0]); return 0;
            }
            return 0;
        }
        switch (wp) {
        case VK_ESCAPE: key_command(C(CommandCLEAR)); return 0;
        case VK_DELETE: key_command(C(CommandCENTR)); return 0;
        case VK_BACK: key_command(C(CommandBACK)); return 0;
        case VK_F9: key_command(C(CommandSIGN)); return 0;
        case VK_RETURN: key_command(C(CommandEQU)); return 0;
        case VK_ADD: key_command(C(CommandADD)); return 0;
        case VK_SUBTRACT: key_command(C(CommandSUB)); return 0;
        case VK_MULTIPLY: key_command(C(CommandMUL)); return 0;
        case VK_DIVIDE: key_command(C(CommandDIV)); return 0;
        }
        if (g_mode == M_PROGRAMMER) {
            if (wp == VK_F5) { set_radix(16); return 0; }
            if (wp == VK_F6) { set_radix(10); return 0; }
            if (wp == VK_F7) { set_radix(8); return 0; }
            if (wp == VK_F8) { set_radix(2); return 0; }
        } else if (g_mode == M_SCIENTIFIC && wp >= VK_F3 && wp <= VK_F5) {
            static const Command a[] = { Command::CommandDEG, Command::CommandRAD, Command::CommandGRAD };
            g_angle = (int)(wp - VK_F3);
            g_calc->SendCommand(a[g_angle]);
            say("calc: angle %ls\n", g_angle_name[g_angle]);
            refresh();
            return 0;
        }
        break; }
    case WM_CHAR: {
        if (wp == '\r' || wp == 8 || wp == 27 || wp < 32) return 0;   /* (WM_KEYDOWN did those) */
        int cmd = char_command((wchar_t)wp);
        if (cmd >= 0) key_command(cmd);
        return 0; }
    case WM_DESTROY:
        say("calc: closed\n");
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int main(int argc, char **argv)
{
    g_font = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_small = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_title = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_expr_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"CalculatorWindow";
    RegisterClassW(&wc);
    g_wnd = CreateWindowExW(0, L"CalculatorWindow", L"Calculator", WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT, 340, 540, NULL, NULL, wc.hInstance, NULL);
    if (!g_wnd) { say("calc: cannot open its window\n"); return 1; }

    CalculatorManager calc(&g_display, &g_res);
    g_calc = &calc;
    Mode start = M_STANDARD;
    for (int i = 1; i < argc; i++) {
        if (!lstrcmpiA(argv[i], "/scientific")) start = M_SCIENTIFIC;
        if (!lstrcmpiA(argv[i], "/programmer")) start = M_PROGRAMMER;
    }
    set_mode(start);
    ShowWindow(g_wnd, SW_SHOW);
    say("calc: open\n");
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_calc = nullptr;
    return 0;
}
