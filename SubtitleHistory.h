#pragma once

#include "DialoguePauser.h"
#include <regex>


enum ESubtitlesRenderMode
{
    SUBTITLE_RENDER_NONE = 0,
    SUBTITLE_RENDER_DEFAULT = 1,
    SUBTITLE_RENDER_TOP = 2,
    SUBTITLE_RENDER_BOTTOM = 3,
    SUBTITLE_RENDER_ABOVE_WHEEL = 4,
    SUBTITLE_RENDER_LOADSCREEN = 5,
    SUBTITLE_RENDER_MAX = 6
};

class SubtitleHistory
{
protected:
    static const int bufsize = 2048;
    wchar subs[8][bufsize];
    wchar outstr[10 * bufsize];
    wchar *header, *outptr = nullptr;
    struct UBioSubtitlesData {
        FString                                            m_sSubtitle;
        float                                              m_fTimeRemaining;
        float                                              m_FontSize;
    };
    UBioSubtitlesData saved;
    UBioSubtitles* shown = nullptr;
    int current = 8;

public:
    wchar* laststr = nullptr;
    int lastsize = 0;
#if defined GAMELE2
    wchar* lasttitle = nullptr, * lastsubtitle = nullptr, * lastbody = nullptr;
    int lasttitlesize = 0, lastsubtitlesize = 0, lastbodysize = 0;
#endif

    SubtitleHistory(wchar* header);
    int SaveSubtitle(FString* f);
#if defined GAMELE2
    int SaveNotification(FString* fTitle, FString* fSubtitle, FString* fBody);
#endif
    int Rebuild(bool reverse);
    bool ShowSubtitle(ABioWorldInfo* bioWorldInfo);
    void RestoreSubtitle(ABioWorldInfo* bioWorldInfo);
    wchar* GetHistory();
    bool IsShown();
};

SubtitleHistory::SubtitleHistory(wchar* Header = L"")
{
    for (int i = 0; i < 8; i++)
        subs[i][0] = 0;
    header = Header;
    outstr[0] = 0;
}

int SubtitleHistory::SaveSubtitle(FString* f)
{
    if (!f->Count || !f->Data)
        return -1;

    if (f->Data == outptr || laststr == f->Data && lastsize == f->Count)
        return 1;

    laststr = f->Data;
    lastsize = f->Count;
    
    if (--current < 0)
        current = 7;

    if (wcsncpy_s(subs[current], f->Data, min(f->Count, bufsize)))
        LOGFORMAT("SaveSubtitle buffer size too low");

    return Rebuild(false);
};

#if defined GAMELE2
int SubtitleHistory::SaveNotification(FString* fTitle, FString* fSubtitle, FString* fBody)
{
    if (!fTitle->Count || !fTitle->Data || !fSubtitle->Count || !fSubtitle->Data || !fBody->Count || !fBody->Data)
        return -1;

    if (lasttitle == fTitle->Data && lasttitlesize == fTitle->Count &&
        lastsubtitle == fSubtitle->Data && lastsubtitlesize == fSubtitle->Count &&
        lastbody == fBody->Data && lastbodysize == fBody->Count
        )
        return 1;

    lasttitle = fTitle->Data;
    lasttitlesize = fTitle->Count;
    lastsubtitle = fSubtitle->Data;
    lastsubtitlesize = fSubtitle->Count;
    lastbody = fBody->Data;
    lastbodysize = fBody->Count;

    if (--current < 0)
        current = 7;

    basic_regex r(L"<.+?>");
    if (swprintf_s(subs[current], bufsize, L"%s\n%s\n%s", lasttitle, regex_replace(lastsubtitle, r, L"").c_str(), regex_replace(lastbody, r, L"").c_str()));
    LOGFORMAT("SaveNotification buffer size too low");

    return Rebuild(false);
}
#endif

int SubtitleHistory::Rebuild(bool reverse = false)
{
    if (reverse)
    {
        return swprintf(outstr, sizeof(outstr) / sizeof(wchar), L"%s%s\n\n%s\n\n%s\n\n%s\n\n%s\n\n%s\n\n%s\n\n%s",
            header,
            subs[(current) % 8],
            subs[(current + 1) % 8],
            subs[(current + 2) % 8],
            subs[(current + 3) % 8],
            subs[(current + 4) % 8],
            subs[(current + 5) % 8],
            subs[(current + 6) % 8],
            subs[(current + 7) % 8]
        );
    }
    else {
        return swprintf(outstr, sizeof(outstr) / sizeof(wchar), L"%s%s\n\n%s\n\n%s\n\n%s\n\n%s\n\n%s\n\n%s\n\n%s",
            header,
            subs[(current + 7) % 8],
            subs[(current + 6) % 8],
            subs[(current + 5) % 8],
            subs[(current + 4) % 8],
            subs[(current + 3) % 8],
            subs[(current + 2) % 8],
            subs[(current + 1) % 8],
            subs[(current) % 8]
        );
    }
}

bool SubtitleHistory::ShowSubtitle(ABioWorldInfo* bioWorldInfo)
{
    if (shown)
        return false;

    UBioSubtitles* dest = bioWorldInfo->m_Subtitles;

    saved = {
        .m_sSubtitle = FString(dest->m_sSubtitle),
        .m_fTimeRemaining = dest->m_fTimeRemaining,
        .m_FontSize = dest->m_FontSize,
    };

    // original string will be freed
    wchar* t = new wchar[saved.m_sSubtitle.Max];
    wcsncpy(t, saved.m_sSubtitle.Data, saved.m_sSubtitle.Max);
    saved.m_sSubtitle.Data = t;

#if defined GAMELE1
    if (!bioWorldInfo->m_oCurrentConversation || bioWorldInfo->m_oCurrentConversation->IsAmbient())
        Rebuild(true);
#else
    switch (dest->m_CurrentRenderMode)
    {
    case SUBTITLE_RENDER_DEFAULT:
        if (!bioWorldInfo->m_oCurrentConversation || bioWorldInfo->m_oCurrentConversation->IsAmbient())
            Rebuild(true);
        break;
    case SUBTITLE_RENDER_TOP:
        Rebuild(true);
        break;
    case SUBTITLE_RENDER_BOTTOM:
    case SUBTITLE_RENDER_ABOVE_WHEEL:
        break;
    }
#endif

    auto size = wcslen(outstr) + 1;
    outptr = new wchar[size];
    wcsncpy(outptr, outstr, size);

#if defined GAMELE1
    dest->DisplaySubtitle(FString(outptr), 1.0);
#else
    dest->DisplaySubtitle(FString(outptr), 1.0, 22);
#endif

    shown = dest;

    return true;
}

void SubtitleHistory::RestoreSubtitle(ABioWorldInfo* bioWorldInfo)
{
    if (!shown)
        return;

    UBioSubtitles* dest = bioWorldInfo->m_Subtitles;

    if (dest != shown)
    {
        LOGFORMAT("Restore called for different subtitle object!");
        return;
    }

#if defined GAMELE1
    dest->DisplaySubtitle(saved.m_sSubtitle, saved.m_fTimeRemaining);
#else
    dest->DisplaySubtitle(saved.m_sSubtitle, saved.m_fTimeRemaining, saved.m_FontSize);
#endif
    free(saved.m_sSubtitle.Data);
    saved.m_sSubtitle = FString();

    shown = nullptr;
}

wchar* SubtitleHistory::GetHistory()
{
    return outstr;
}

bool SubtitleHistory::IsShown()
{
    return shown;
}
