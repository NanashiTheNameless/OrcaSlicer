#include "UnsupportedLanguageNotice.hpp"

#include "MsgDialog.hpp"
#include "libslic3r/AppConfig.hpp"

#include <wx/defs.h>
#include <wx/intl.h>
#include <wx/string.h>

#include <algorithm>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

namespace Slic3r {
namespace GUI {

namespace {

constexpr const char* ACKNOWLEDGED_KEY = "unsupported_language_acknowledged";

struct NoticeText
{
    const char* title;
    const char* message;
    const char* use_anyway;
    const char* keep_english;
};

const NoticeText ENGLISH{
    "Language not officially supported",
    "This fork only officially supports English. You can use other languages, but this fork offers no support in languages "
    "other than English and makes no promise that everything is translated.",
    "Use this language anyway",
    "Keep English",
};

// Keyed by catalog name, as in localization/i18n. The translations are machine-made.
const std::map<std::string, NoticeText>& translations()
{
    static const std::map<std::string, NoticeText> table{
        {"ca",
         {"Idioma sense suport oficial",
          "Aquesta bifurcació només admet oficialment l'anglès. Pots utilitzar altres idiomes, però aquesta bifurcació no "
          "ofereix suport en idiomes diferents de l'anglès i no garanteix que tot estigui traduït.",
          "Utilitza aquest idioma igualment", "Mantén l'anglès"}},
        {"cs",
         {"Jazyk bez oficiální podpory",
          "Tento fork oficiálně podporuje pouze angličtinu. Můžete používat i jiné jazyky, ale tento fork v jiných jazycích "
          "než v angličtině neposkytuje žádnou podporu a neslibuje, že bude vše přeloženo.",
          "Přesto použít tento jazyk", "Ponechat angličtinu"}},
        {"de",
         {"Sprache ohne offizielle Unterstützung",
          "Dieser Fork unterstützt offiziell nur Englisch. Sie können andere Sprachen verwenden, aber dieser Fork bietet "
          "keinen Support in anderen Sprachen als Englisch und garantiert nicht, dass alles übersetzt ist.",
          "Diese Sprache trotzdem verwenden", "Bei Englisch bleiben"}},
        {"es",
         {"Idioma sin soporte oficial",
          "Este fork solo admite oficialmente el inglés. Puede usar otros idiomas, pero este fork no ofrece soporte en "
          "idiomas distintos del inglés ni garantiza que todo esté traducido.",
          "Usar este idioma de todos modos", "Mantener el inglés"}},
        {"eu",
         {"Ofizialki onartzen ez den hizkuntza",
          "Fork honek ingelesa soilik onartzen du ofizialki. Beste hizkuntza batzuk erabil ditzakezu, baina fork honek ez "
          "du laguntzarik eskaintzen ingelesa ez den beste hizkuntzetan, eta ez du bermatzen dena itzulita dagoenik.",
          "Erabili hizkuntza hau hala ere", "Mantendu ingelesa"}},
        {"fr",
         {"Langue non officiellement prise en charge",
          "Ce fork ne prend officiellement en charge que l'anglais. Vous pouvez utiliser d'autres langues, mais ce fork "
          "n'offre aucune assistance dans les langues autres que l'anglais et ne garantit pas que tout soit traduit.",
          "Utiliser cette langue quand même", "Garder l'anglais"}},
        {"hu",
         {"Hivatalosan nem támogatott nyelv",
          "Ez a fork hivatalosan csak az angol nyelvet támogatja. Használhat más nyelveket is, de ez a fork nem nyújt "
          "támogatást az angoltól eltérő nyelveken, és nem ígéri, hogy minden le van fordítva.",
          "Mégis ezt a nyelvet használom", "Maradok az angolnál"}},
        {"it",
         {"Lingua non supportata ufficialmente",
          "Questo fork supporta ufficialmente solo l'inglese. Puoi usare altre lingue, ma questo fork non offre assistenza "
          "in lingue diverse dall'inglese e non garantisce che tutto sia tradotto.",
          "Usa comunque questa lingua", "Mantieni l'inglese"}},
        {"ja",
         {"公式サポート対象外の言語",
          "このフォークが公式にサポートしているのは英語のみです。他の言語も使用できますが、このフォークは英語以外の言語でのサポート"
          "を提供しておらず、すべてが翻訳されていることも保証しません。",
          "それでもこの言語を使用する", "英語のままにする"}},
        {"ko",
         {"공식적으로 지원되지 않는 언어",
          "이 포크는 공식적으로 영어만 지원합니다. 다른 언어를 사용할 수 있지만, 이 포크는 영어 이외의 언어에 대한 지원을 "
          "제공하지 않으며 모든 내용이 번역되어 있다고 보장하지 않습니다.",
          "그래도 이 언어 사용하기", "영어 유지"}},
        {"lt",
         {"Oficialiai nepalaikoma kalba",
          "Ši atšaka oficialiai palaiko tik anglų kalbą. Galite naudoti kitas kalbas, tačiau ši atšaka neteikia pagalbos "
          "kitomis nei anglų kalbomis ir nežada, kad viskas išversta.",
          "Vis tiek naudoti šią kalbą", "Palikti anglų kalbą"}},
        {"nl",
         {"Taal zonder officiële ondersteuning",
          "Deze fork ondersteunt officieel alleen Engels. Je kunt andere talen gebruiken, maar deze fork biedt geen "
          "ondersteuning in andere talen dan Engels en belooft niet dat alles vertaald is.",
          "Deze taal toch gebruiken", "Engels blijven gebruiken"}},
        {"pl",
         {"Język bez oficjalnego wsparcia",
          "Ten fork oficjalnie obsługuje tylko język angielski. Możesz używać innych języków, ale ten fork nie zapewnia "
          "wsparcia w językach innych niż angielski i nie obiecuje, że wszystko zostało przetłumaczone.",
          "Mimo to użyj tego języka", "Pozostań przy angielskim"}},
        {"pt_BR",
         {"Idioma sem suporte oficial",
          "Este fork oferece suporte oficial apenas ao inglês. Você pode usar outros idiomas, mas este fork não oferece "
          "suporte em idiomas diferentes do inglês e não garante que tudo esteja traduzido.",
          "Usar este idioma mesmo assim", "Manter o inglês"}},
        {"ro",
         {"Limbă fără suport oficial",
          "Acest fork oferă suport oficial doar pentru limba engleză. Puteți folosi alte limbi, dar acest fork nu oferă asistență în "
          "alte limbi decât engleza și nu garantează că totul este tradus.",
          "Folosiți totuși această limbă", "Păstrați limba engleză"}},
        {"ru",
         {"Язык без официальной поддержки",
          "Этот форк официально поддерживает только английский язык. Вы можете использовать другие языки, но этот форк не "
          "предоставляет поддержку на языках, отличных от английского, и не гарантирует, что всё переведено.",
          "Всё равно использовать этот язык", "Оставить английский"}},
        {"sv",
         {"Språk utan officiellt stöd",
          "Den här forken stöder officiellt endast engelska. Du kan använda andra språk, men den här forken erbjuder ingen "
          "support på andra språk än engelska och lovar inte att allt är översatt.",
          "Använd det här språket ändå", "Fortsätt använda engelska"}},
        {"th",
         {"ภาษาที่ไม่ได้รับการรองรับอย่างเป็นทางการ",
          "ฟอร์กนี้รองรับภาษาอังกฤษอย่างเป็นทางการเท่านั้น คุณสามารถใช้ภาษาอื่นได้ แต่ฟอร์กนี้ไม่มีการให้ความช่วยเหลือในภาษาอื่นนอกจาก"
          "ภาษาอังกฤษ และไม่รับประกันว่าทุกอย่างได้รับการแปลแล้ว",
          "ใช้ภาษานี้แม้จะไม่รองรับ", "ใช้ภาษาอังกฤษต่อ"}},
        {"tr",
         {"Resmî olarak desteklenmeyen dil",
          "Bu fork resmi olarak yalnızca İngilizceyi destekler. Başka diller kullanabilirsiniz, ancak bu fork İngilizce "
          "dışındaki dillerde destek sunmaz ve her şeyin çevrilmiş olduğunu garanti etmez.",
          "Yine de bu dili kullan", "İngilizce kalsın"}},
        {"uk",
         {"Мова без офіційної підтримки",
          "Цей форк офіційно підтримує лише англійську мову. Ви можете використовувати інші мови, але цей форк не надає "
          "підтримки іншими мовами, крім англійської, і не гарантує, що все перекладено.",
          "Усе одно використовувати цю мову", "Залишити англійську"}},
        {"vi",
         {"Ngôn ngữ không được hỗ trợ chính thức",
          "Bản fork này chỉ chính thức hỗ trợ tiếng Anh. Bạn có thể sử dụng các ngôn ngữ khác, nhưng bản fork này không "
          "cung cấp hỗ trợ bằng các ngôn ngữ ngoài tiếng Anh và không đảm bảo mọi nội dung đều được dịch.",
          "Vẫn dùng ngôn ngữ này", "Giữ tiếng Anh"}},
        {"zh_CN",
         {"未获官方支持的语言",
          "此派生版本仅正式支持英语。您可以使用其他语言，但此派生版本不提供英语以外语言的支持，也不保证所有内容均已翻译。",
          "仍然使用此语言", "继续使用英语"}},
        {"zh_TW",
         {"未獲官方支援的語言",
          "此衍生版本僅正式支援英文。您可以使用其他語言，但此衍生版本不提供英文以外語言的支援，也不保證所有內容皆已翻譯。",
          "仍要使用此語言", "繼續使用英文"}},
    };
    return table;
}

// An exact catalog name first (zh_TW, pt_BR), then the language alone (de_DE -> de, zh_HK -> zh_CN).
const NoticeText* find_translation(const std::string& language)
{
    const auto& table = translations();
    if (auto it = table.find(language); it != table.end())
        return &it->second;

    const std::string base = language.substr(0, language.find_first_of("_-"));
    if (auto it = table.find(base); it != table.end())
        return &it->second;
    if (base == "zh")
        return &table.at("zh_CN");
    return nullptr;
}

} // namespace

bool is_english(const std::string& language) { return language == "en" || language.rfind("en_", 0) == 0 || language.rfind("en-", 0) == 0; }

bool has_unsupported_language_notice_translation(const std::string& language) { return find_translation(language) != nullptr; }

bool confirm_unsupported_language(wxWindow* parent, AppConfig& config, const std::string& language, const std::string& previous_language)
{
    if (language.empty() || is_english(language) || config.get_bool(ACKNOWLEDGED_KEY))
        return true;

    // Show the notice in English, the system language, the language in use and the chosen one, each once,
    // so whoever is switching can read at least one of them.
    std::string system_language;
    if (const wxLanguageInfo* info = wxLocale::GetLanguageInfo(wxLocale::GetSystemLanguage()))
        system_language = info->CanonicalName.ToUTF8().data();

    std::vector<const NoticeText*> shown;
    for (const std::string& lang : {std::string("en"), system_language, previous_language, language}) {
        const NoticeText* text = lang.empty() ? nullptr : is_english(lang) ? &ENGLISH : find_translation(lang);
        if (text && std::find(shown.begin(), shown.end(), text) == shown.end())
            shown.push_back(text);
    }

    // The message lists each language's text under a divider; the buttons join their labels, English first,
    // like "Use this language anyway / Diese Sprache trotzdem verwenden".
    const wxString divider = "\n\n" + wxString::FromUTF8("────────────") + "\n\n";
    wxString       message, use_anyway, keep_english;
    for (const NoticeText* text : shown) {
        if (!message.IsEmpty()) {
            message += divider;
            use_anyway += " / ";
            keep_english += " / ";
        }
        message += wxString::FromUTF8(text->message);
        use_anyway += wxString::FromUTF8(text->use_anyway);
        keep_english += wxString::FromUTF8(text->keep_english);
    }

    // One title: in the chosen language if possible, else the system language, else English.
    const NoticeText* chosen = find_translation(language);
    const NoticeText* system = system_language.empty() || is_english(system_language) ? nullptr : find_translation(system_language);
    const NoticeText& title  = chosen ? *chosen : system ? *system : ENGLISH;

    MessageDialog dlg(parent, message, wxString::FromUTF8(title.title), wxICON_WARNING | wxOK | wxCANCEL);
    dlg.SetButtonLabel(wxID_OK, use_anyway);
    dlg.SetButtonLabel(wxID_CANCEL, keep_english, true);
    // The labels are longer than the defaults the dialog was laid out for.
    dlg.Layout();
    dlg.Fit();
    dlg.CentreOnParent();
    if (dlg.ShowModal() != wxID_OK)
        return false;

    config.set_bool(ACKNOWLEDGED_KEY, true);
    config.save();
    return true;
}

} // namespace GUI
} // namespace Slic3r
