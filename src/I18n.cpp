#include "I18n.h"

static const char* const kEnglish[] = {
#define X(id, en, it) en,
    STRING_TABLE(X)
#undef X
};

static const char* const kItalian[] = {
#define X(id, en, it) it,
    STRING_TABLE(X)
#undef X
};

static Lang g_lang = Lang::English;

void SetLanguage(Lang lang) { g_lang = lang; }
Lang GetLanguage() { return g_lang; }

const char* tr(S id)
{
    const int i = (int)id;
    if (i < 0 || i >= (int)S::Count) return "";
    return g_lang == Lang::Italian ? kItalian[i] : kEnglish[i];
}
