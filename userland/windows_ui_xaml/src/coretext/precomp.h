// NovaOS: the precompiled header the text core's sources NovaOS builds
// (build.py CORE_LIBRARIES' core/text/coretext.vcxproj entry: the text
// block, its inlines and runs, the inherited text properties) are compiled
// with, in place of core/text/precomp.h, which they include as "precomp.h"
// (build.py puts this directory first): Microsoft's, in its order, less the
// closed Page/Table Layout Services' header (Ptls-min.h) and the line
// services headers written against it (Ls.h, LsClient.h, LineBreaking.h),
// and with its
// headers spelled as they are on disk (a case-sensitive file system).
#pragma once
#include "../core_precomp.h"
#include "winnls.h"
#include <MuiUdk.h>

#undef CDECL
#include <combaseapi.h>

#ifdef GetClassName
#undef GetClassName
#endif

#include "xcperror.h"
#include "macros.h"
#include "xcpdebug.h"
#include "pal.h"
#include "core.h"
#include "refcounting.h"
#include "values.h"
#include "core_media.h"
#include "matrix.h"

#include "xstring_ptr.h"
#include "xstrutil.h"
#include "XcpList.h"
#include "corep.h"
#include "DependencyObjectTraits.g.h"
#include "rendertypes.h"
#include "real.h"
#include "memutils.h"
#include "Span.h"
#include "ctypes.h"
#include "xcpmath.h"
#include "elements.h"
#include "text.h"
#include "EnumValueTable.h"
#include "LineFormatter.h"
#include <TextPosition.h>
#include <SelectionWordBreaker.h>

#include "DWriteTypes.h"

#include "ShapingTypes.h"

typedef unsigned char BYTE;

#include "UcdProperties.h"
#include "UcdData.h"
#include "RichTextServicesHelper.h"
#include "TextAdapter.h"
#include "TextRangeAdapter.h"
#include "TextBoxHelpers.h"
#include "TxUtil.h"

#include "EventArgs.h"
#include "RoutedEventArgs.h"
#include "KeyboardEventArgs.h"
#include "InputPointEventArgs.h"
#include "TappedEventArgs.h"
#include "pointereventargs.h"
#include "RightTappedEventArgs.h"
#include "DoubleTappedEventArgs.h"
#include "PageNode.h"
#include "TextPosition.h"
#include "ITextSelection.h"
#include "TextSelection.h"
#include "SelectionWordBreaker.h"
#include "FocusRectangle.h"

#include <Fonts.h>

#include <xmllite_error.h>

#include <host.h>

#include "CharacterProperties.h"

#include "LineMetricsCache.h"

#include "RichTextBlockBreak.h"

#include "TextSchema.h"
#include "HyperlinkClickEventArgs.h"
#include "IsTextTrimmedChangedEventArgs.h"

#include "hwwalk.h"
#include "compositor-all.h"
#include "hw-all.h"

#include "pixelformatutils.h"
#include "XcpTextGripperInputProcessor.h"
#include <new.h>

#include "MUX-ETWEvents.h"
