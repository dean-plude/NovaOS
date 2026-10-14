/*
 * oledlg.dll — the OLE user-interface dialogs (Insert Object, Paste
 * Special, Change Icon, Edit Links...).  NovaOS has none of these dialogs
 * yet, so each one answers as if the user pressed Cancel, which every
 * caller handles: LibreOffice imports OleUIInsertObjectW for its
 * Insert > OLE Object > Further Objects command and loads only if the DLL
 * is there.
 */
#include <windows.h>

#define OLEDLG __declspec(dllexport)
#define OLEUI_CANCEL_ 2                 /* OLEUI_CANCEL: the user cancelled */

OLEDLG UINT WINAPI OleUIInsertObjectW(void *dlg) { (void)dlg; return OLEUI_CANCEL_; }
OLEDLG UINT WINAPI OleUIInsertObjectA(void *dlg) { (void)dlg; return OLEUI_CANCEL_; }
OLEDLG UINT WINAPI OleUIPasteSpecialW(void *dlg) { (void)dlg; return OLEUI_CANCEL_; }
OLEDLG UINT WINAPI OleUIPasteSpecialA(void *dlg) { (void)dlg; return OLEUI_CANCEL_; }
OLEDLG UINT WINAPI OleUIEditLinksW(void *dlg) { (void)dlg; return OLEUI_CANCEL_; }
OLEDLG UINT WINAPI OleUIEditLinksA(void *dlg) { (void)dlg; return OLEUI_CANCEL_; }
OLEDLG UINT WINAPI OleUIChangeIconW(void *dlg) { (void)dlg; return OLEUI_CANCEL_; }
OLEDLG UINT WINAPI OleUIChangeIconA(void *dlg) { (void)dlg; return OLEUI_CANCEL_; }
OLEDLG UINT WINAPI OleUIConvertW(void *dlg) { (void)dlg; return OLEUI_CANCEL_; }
OLEDLG UINT WINAPI OleUIConvertA(void *dlg) { (void)dlg; return OLEUI_CANCEL_; }
OLEDLG UINT WINAPI OleUIBusyW(void *dlg) { (void)dlg; return OLEUI_CANCEL_; }
OLEDLG UINT WINAPI OleUIBusyA(void *dlg) { (void)dlg; return OLEUI_CANCEL_; }
OLEDLG UINT WINAPI OleUIObjectPropertiesW(void *dlg) { (void)dlg; return OLEUI_CANCEL_; }
OLEDLG UINT WINAPI OleUIObjectPropertiesA(void *dlg) { (void)dlg; return OLEUI_CANCEL_; }
OLEDLG UINT WINAPI OleUIChangeSourceW(void *dlg) { (void)dlg; return OLEUI_CANCEL_; }
OLEDLG UINT WINAPI OleUIChangeSourceA(void *dlg) { (void)dlg; return OLEUI_CANCEL_; }

/* No "Add verbs to this menu": FALSE, the menu is left as it was */
OLEDLG BOOL WINAPI OleUIAddVerbMenuW(void *obj, LPCWSTR short_type, HMENU menu, UINT pos, UINT first,
                                     UINT last, BOOL convert, UINT id_convert, HMENU *popup)
{
    (void)obj; (void)short_type; (void)menu; (void)pos; (void)first; (void)last; (void)convert; (void)id_convert;
    if (popup) *popup = NULL;
    return FALSE;
}
