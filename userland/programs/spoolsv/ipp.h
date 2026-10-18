/*
 * ipp.h — the spooler's IPP port: driverless network printing
 *
 * A printer whose port is an http://, https://, ipp:// or ipps:// URL is
 * an IPP printer (RFC 8011; IPP Everywhere, PWG 5100.14), what Windows'
 * "Microsoft IPP Class Driver" prints to.  The spooler asks it for its
 * attributes (Get-Printer-Attributes) and sends each job with Print-Job
 * over WinHTTP: a PDF as it is when the printer takes PDF, otherwise as
 * PWG Raster (PWG 5102.4) made from the PDF's page images; other data
 * (raw jobs) as application/octet-stream.
 */
#pragma once
#include <windows.h>

typedef struct {
    char  model[128];                   /* printer-make-and-model */
    char  info[128];                    /* printer-info */
    char  location[128];                /* printer-location */
    DWORD state;                        /* printer-state: 3 idle, 4 processing, 5 stopped */
    BOOL  accepting;                    /* printer-is-accepting-jobs */
    BOOL  pdf, pwg, octet;              /* document-format-supported */
    int   res[8], nres;                 /* pwg-raster-document-resolution-supported (dpi) */
    BOOL  srgb, sgray;                  /* pwg-raster-document-type-supported */
} IppPrinter;

/* Is @port an IPP printer's URL */
BOOL  ipp_is_port(const WCHAR *port);
/* Get-Printer-Attributes: ERROR_SUCCESS, or why not (a Win32 error; @why says it in words) */
DWORD ipp_attributes(const WCHAR *port, IppPrinter *out, char *why, int whycap);
/* Print-Job: @data (@n bytes, a whole job) to the printer.  *@format gets
 * the document format sent.  ERROR_SUCCESS, or why not */
DWORD ipp_print(const WCHAR *port, const IppPrinter *pr, const WCHAR *job_name, const WCHAR *user,
                const BYTE *data, DWORD n, const char **format, char *why, int whycap);
/* NovaOS's own PDF (gdi32's printer driver) as PWG Raster at @dpi, sRGB
 * (or grey with @gray): a malloc'd buffer (*@out_n bytes) or NULL with @why */
BYTE *ipp_pdf_to_pwg(const BYTE *pdf, DWORD n, int dpi, BOOL gray, DWORD *out_n, char *why, int whycap);
