#include <QApplication>
#include <QClipboard>
#include <QWidget>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMenu>
#include <QDialog>
#include <QListWidget>
#include <QTreeWidget>
#include <QHeaderView>
#include <QProgressBar>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QStackedWidget>
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPixmap>
#include <QPainter>
#include <QBuffer>
#include <QSet>
#include <QMap>
#include <QTimer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#ifdef HAVE_QTPDF
#include <QPdfDocument>
#include <QPdfSelection>
#endif

static const char* CHAT_URL   = "http://127.0.0.1:11434/api/chat";
static const char* TAGS_URL   = "http://127.0.0.1:11434/api/tags";
static const char* PULL_URL   = "http://127.0.0.1:11434/api/pull";
static const char* DELETE_URL = "http://127.0.0.1:11434/api/delete";
static const char* DEFAULT_MODEL = "qwen2.5:7b";

static const int NUM_CTX          = 8192;        // Kontextfenster (mehr = mehr Grafikspeicher)
static const int MAX_ATTACH_CHARS = 24000;       // max. Text aus Anhaengen pro Nachricht
static const qint64 MAX_FILE_BYTES = 25LL * 1024 * 1024;   // max. Dateigroesse
static const qint64 MAX_TEXT_READ  = 2LL * 1024 * 1024;    // max. gelesene Bytes bei Textdateien

// ---------------------------------------------------------------------------
//  Einfacher Syntax-Highlighter (Farben fuer Code)
// ---------------------------------------------------------------------------
class CodeHighlighter : public QSyntaxHighlighter {
public:
    CodeHighlighter(QTextDocument *doc, const QString &lang) : QSyntaxHighlighter(doc) {
        auto fmt = [](const char *color, bool italic = false) {
            QTextCharFormat f;
            f.setForeground(QColor(color));
            f.setFontItalic(italic);
            return f;
        };
        fKeyword = fmt("#c678dd");
        fString  = fmt("#98c379");
        fComment = fmt("#7f848e", true);
        fNumber  = fmt("#d19a66");
        fFunc    = fmt("#61afef");
        fType    = fmt("#e5c07b");
        fPre     = fmt("#56b6c2");
        setLanguage(lang);
    }

    void changeLanguage(const QString &lang) {
        setLanguage(lang);
        rehighlight();
    }

protected:
    void highlightBlock(const QString &t) override {
        const int n = t.size();
        int i = 0;
        bool inBlock = (previousBlockState() == 1);
        while (i < n) {
            if (inBlock) {
                int e = t.indexOf("*/", i);
                if (e < 0) { setFormat(i, n - i, fComment); setCurrentBlockState(1); return; }
                setFormat(i, e + 2 - i, fComment);
                i = e + 2;
                inBlock = false;
                continue;
            }
            const QChar c = t[i];
            if (slashComment && c == '/' && i + 1 < n && t[i + 1] == '/') {
                setFormat(i, n - i, fComment);
                break;
            }
            if (slashComment && c == '/' && i + 1 < n && t[i + 1] == '*') {
                inBlock = true;
                setFormat(i, 2, fComment);
                i += 2;
                continue;
            }
            if (hashComment && c == '#') {
                setFormat(i, n - i, fComment);
                break;
            }
            if (preproc && c == '#' && t.left(i).trimmed().isEmpty()) {
                setFormat(i, n - i, fPre);
                break;
            }
            if (c == '"' || c == '\'' || c == '`') {
                int j = i + 1;
                while (j < n && t[j] != c) { if (t[j] == '\\') ++j; ++j; }
                int end = qMin(j + 1, n);
                setFormat(i, end - i, fString);
                i = end;
                continue;
            }
            if (c.isDigit() && (i == 0 || !(t[i - 1].isLetterOrNumber() || t[i - 1] == '_'))) {
                int j = i + 1;
                while (j < n && (t[j].isLetterOrNumber() || t[j] == '.')) ++j;
                setFormat(i, j - i, fNumber);
                i = j;
                continue;
            }
            if (c.isLetter() || c == '_') {
                int j = i + 1;
                while (j < n && (t[j].isLetterOrNumber() || t[j] == '_')) ++j;
                const QString w = t.mid(i, j - i);
                if (words.contains(w))                     setFormat(i, j - i, fKeyword);
                else if (j < n && t[j] == '(')             setFormat(i, j - i, fFunc);
                else if (w.size() > 1 && w[0].isUpper())   setFormat(i, j - i, fType);
                i = j;
                continue;
            }
            ++i;
        }
        setCurrentBlockState(inBlock ? 1 : 0);
    }

private:
    void setLanguage(const QString &langIn) {
        const QString lang = langIn.toLower();
        hashComment  = (lang == "python" || lang == "py" || lang == "bash" || lang == "sh" ||
                        lang == "shell" || lang == "zsh" || lang == "yaml" || lang == "yml" ||
                        lang == "toml" || lang == "ruby" || lang == "rb" ||
                        lang == "powershell" || lang == "ps1");
        slashComment = !hashComment && lang != "json";
        preproc      = !hashComment && (lang.isEmpty() || lang == "c" || lang == "cpp" || lang == "c++" ||
                        lang == "cc" || lang == "h" || lang == "hpp" || lang == "cs" || lang == "csharp");

        QString list;
        if (hashComment) {
            list = "def class import from as if elif else for while return in is not and or None True False "
                   "with try except finally raise lambda pass break continue yield global nonlocal async await "
                   "self then fi do done case esac function echo cd export sudo true false null";
        } else {
            list = "if else for while do switch case default break continue return class struct enum union "
                   "namespace using typedef template typename public private protected virtual override static "
                   "const constexpr auto void int long short char float double bool unsigned signed new delete "
                   "this nullptr true false null try catch throw finally import export from function let var "
                   "fn mut pub impl trait match go func package interface extends implements async await yield "
                   "in of is as not sizeof inline explicit final namespace string";
        }
        const QStringList l = list.split(' ', Qt::SkipEmptyParts);
        words = QSet<QString>(l.begin(), l.end());
    }

    QTextCharFormat fKeyword, fString, fComment, fNumber, fFunc, fType, fPre;
    QSet<QString> words;
    bool hashComment = false, slashComment = true, preproc = true;
};

// ---------------------------------------------------------------------------
//  Drag & Drop von Dateien ins Fenster
// ---------------------------------------------------------------------------
class DropFilter : public QObject {
public:
    std::function<void(const QStringList &)> onDrop;
    explicit DropFilter(QObject *parent) : QObject(parent) {}

protected:
    bool eventFilter(QObject *obj, QEvent *e) override {
        if (e->type() == QEvent::DragEnter) {
            auto *de = static_cast<QDragEnterEvent *>(e);
            if (de->mimeData()->hasUrls()) { de->acceptProposedAction(); return true; }
        } else if (e->type() == QEvent::DragMove) {
            auto *dm = static_cast<QDragMoveEvent *>(e);
            if (dm->mimeData()->hasUrls()) { dm->acceptProposedAction(); return true; }
        } else if (e->type() == QEvent::Drop) {
            auto *d = static_cast<QDropEvent *>(e);
            QStringList files;
            for (const QUrl &u : d->mimeData()->urls())
                if (u.isLocalFile()) files << u.toLocalFile();
            if (!files.isEmpty() && onDrop) {
                d->acceptProposedAction();
                onDrop(files);
                return true;
            }
        }
        return QObject::eventFilter(obj, e);
    }
};

// ---------------------------------------------------------------------------
//  Antworten in Text- und Codeteile zerlegen
// ---------------------------------------------------------------------------
struct Seg { bool code; QString lang; QString text; };

static std::vector<Seg> parseSegments(const QString &s) {
    std::vector<Seg> raw;
    int pos = 0;
    while (pos < s.size()) {
        int f = s.indexOf("```", pos);
        if (f < 0) { raw.push_back({false, QString(), s.mid(pos)}); break; }
        if (f > pos) raw.push_back({false, QString(), s.mid(pos, f - pos)});
        int nl = s.indexOf('\n', f + 3);
        if (nl < 0) { raw.push_back({true, s.mid(f + 3).trimmed(), QString()}); break; }
        QString lang = s.mid(f + 3, nl - (f + 3)).trimmed();
        int end = s.indexOf("```", nl + 1);
        if (end < 0) { raw.push_back({true, lang, s.mid(nl + 1)}); break; }
        raw.push_back({true, lang, s.mid(nl + 1, end - (nl + 1))});
        pos = end + 3;
    }
    std::vector<Seg> out;
    for (Seg &g : raw) {
        if (g.code) {
            while (g.text.endsWith('\n') || g.text.endsWith('\r')) g.text.chop(1);
            out.push_back(g);
        } else {
            g.text = g.text.trimmed();
            if (!g.text.isEmpty()) out.push_back(g);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
//  Nachrichten-Bausteine
// ---------------------------------------------------------------------------
struct Part {
    bool code = false;
    QLabel *label = nullptr;            // Textteil
    QFrame *frame = nullptr;            // Codeteil
    QLabel *langLabel = nullptr;
    QPlainTextEdit *edit = nullptr;
    CodeHighlighter *hl = nullptr;
    QString lastText, lastLang;
};

struct Message {
    QWidget *box = nullptr;
    QVBoxLayout *lay = nullptr;
    std::vector<Part> parts;
};

static void fitCodeHeight(QPlainTextEdit *e) {
    const int lines = qMax(1, e->document()->blockCount());
    QFontMetrics fm(e->font());
    e->setFixedHeight(lines * fm.lineSpacing() + 2 * int(e->document()->documentMargin()) + 14);
}

static Part newTextPart(QVBoxLayout *lay) {
    Part p;
    p.code = false;
    p.label = new QLabel;
    p.label->setWordWrap(true);
    p.label->setTextFormat(Qt::MarkdownText);
    p.label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    p.label->setOpenExternalLinks(true);
    p.label->setStyleSheet("color:#ececec; padding:2px;");
    lay->addWidget(p.label);
    return p;
}

static Part newCodePart(QVBoxLayout *lay) {
    Part p;
    p.code = true;
    p.frame = new QFrame;
    p.frame->setObjectName("codeFrame");
    auto *v = new QVBoxLayout(p.frame);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto *header = new QWidget;
    header->setObjectName("codeHeader");
    header->setAttribute(Qt::WA_StyledBackground, true);
    auto *h = new QHBoxLayout(header);
    h->setContentsMargins(14, 6, 8, 6);
    p.langLabel = new QLabel("code");
    p.langLabel->setObjectName("codeLang");
    auto *copy = new QPushButton("Kopieren");
    copy->setObjectName("copyBtn");
    copy->setCursor(Qt::PointingHandCursor);
    h->addWidget(p.langLabel);
    h->addStretch(1);
    h->addWidget(copy);

    p.edit = new QPlainTextEdit;
    p.edit->setObjectName("codeEdit");
    p.edit->setReadOnly(true);
    p.edit->setFrameShape(QFrame::NoFrame);
    p.edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    p.edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QFont mono("Consolas", 10);
    mono.setStyleHint(QFont::Monospace);
    p.edit->setFont(mono);
    p.hl = new CodeHighlighter(p.edit->document(), QString());
    fitCodeHeight(p.edit);

    QPlainTextEdit *edit = p.edit;
    QObject::connect(copy, &QPushButton::clicked, [edit, copy]() {
        QApplication::clipboard()->setText(edit->toPlainText());
        copy->setText("Kopiert!");
        QTimer::singleShot(1500, copy, [copy]() { copy->setText("Kopieren"); });
    });

    v->addWidget(header);
    v->addWidget(p.edit);
    lay->addWidget(p.frame);
    return p;
}

static void removeParts(Message &m, size_t from) {
    while (m.parts.size() > from) {
        Part &p = m.parts.back();
        QWidget *w = p.code ? static_cast<QWidget*>(p.frame) : static_cast<QWidget*>(p.label);
        m.lay->removeWidget(w);
        w->deleteLater();
        m.parts.pop_back();
    }
}

// Zeigt den aktuellen Antworttext an (wird bei jedem Stueck neu aufgerufen)
static void updateMessage(Message &m, const QString &text) {
    std::vector<Seg> segs = parseSegments(text);
    if (segs.empty()) segs.push_back({false, QString(), "..."});

    for (size_t i = 0; i < segs.size(); ++i) {
        if (i < m.parts.size() && m.parts[i].code != segs[i].code)
            removeParts(m, i);
        if (i >= m.parts.size())
            m.parts.push_back(segs[i].code ? newCodePart(m.lay) : newTextPart(m.lay));

        Part &p = m.parts[i];
        if (segs[i].code) {
            if (p.lastLang != segs[i].lang) {
                p.lastLang = segs[i].lang;
                p.langLabel->setText(segs[i].lang.isEmpty() ? QString("code") : segs[i].lang);
                p.hl->changeLanguage(segs[i].lang);
            }
            if (p.lastText != segs[i].text) {
                p.lastText = segs[i].text;
                p.edit->setPlainText(segs[i].text);
                fitCodeHeight(p.edit);
            }
        } else if (p.lastText != segs[i].text) {
            p.lastText = segs[i].text;
            p.label->setText(segs[i].text);
        }
    }
    removeParts(m, segs.size());
}

// ---------------------------------------------------------------------------
//  Einstellungen, Chat-Archiv und Projektmappen
// ---------------------------------------------------------------------------
static QString g_chatDir;   // leer = Standardordner

static QString appDataDir()     { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation); }
static QString settingsFile()   { return appDataDir() + "/settings.ini"; }
static QString defaultChatDir() { return appDataDir() + "/chats"; }
static QString chatDir()        { return g_chatDir.isEmpty() ? defaultChatDir() : g_chatDir; }
static QString chatPath(const QString &id) { return chatDir() + "/" + id + ".json"; }
static QString projectsPath()   { return chatDir() + "/_projects.json"; }

static void loadSettings() {
    QSettings s(settingsFile(), QSettings::IniFormat);
    g_chatDir = s.value("chatDir").toString();
}

static void saveChatDirSetting(const QString &dir) {
    QDir().mkpath(appDataDir());
    QSettings s(settingsFile(), QSettings::IniFormat);
    s.setValue("chatDir", dir);
    s.sync();
    g_chatDir = dir;
}

struct Chat {
    QString id, title, model, created, updated, projectId;
    QJsonArray messages;
};

struct Project {
    QString id, name, created;
};

static bool saveChatFile(Chat &c, bool touch = true) {
    if (touch) c.updated = QDateTime::currentDateTime().toString(Qt::ISODate);
    QDir().mkpath(chatDir());
    QJsonObject o{{"id", c.id}, {"title", c.title}, {"model", c.model},
                  {"created", c.created}, {"updated", c.updated},
                  {"projectId", c.projectId}, {"messages", c.messages}};
    QFile f(chatPath(c.id));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}

static std::vector<Chat> loadAllChats() {
    std::vector<Chat> out;
    QDir d(chatDir());
    const QFileInfoList files = d.entryInfoList(QStringList("*.json"), QDir::Files);
    for (const QFileInfo &fi : files) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        if (o.isEmpty()) continue;                 // z. B. _projects.json
        Chat c;
        c.id        = o.value("id").toString();
        c.title     = o.value("title").toString();
        c.model     = o.value("model").toString();
        c.created   = o.value("created").toString();
        c.updated   = o.value("updated").toString();
        c.projectId = o.value("projectId").toString();
        c.messages  = o.value("messages").toArray();
        if (c.id.isEmpty()) continue;
        out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](const Chat &a, const Chat &b) { return a.updated > b.updated; });
    return out;
}

static std::vector<Project> loadProjects() {
    std::vector<Project> out;
    QFile f(projectsPath());
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).object().value("projects").toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Project p;
        p.id      = o.value("id").toString();
        p.name    = o.value("name").toString();
        p.created = o.value("created").toString();
        if (!p.id.isEmpty()) out.push_back(p);
    }
    return out;
}

static bool saveProjects(const std::vector<Project> &list) {
    QJsonArray arr;
    for (const Project &p : list)
        arr.append(QJsonObject{{"id", p.id}, {"name", p.name}, {"created", p.created}});
    QDir().mkpath(chatDir());
    QFile f(projectsPath());
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(QJsonObject{{"projects", arr}}).toJson(QJsonDocument::Indented));
    return true;
}

// ---------------------------------------------------------------------------
//  KI-Modelle: Katalog, Familien-Zuordnung, Hilfsfunktionen
// ---------------------------------------------------------------------------
struct CatModel  { const char *tag; const char *info; };
struct CatFamily { const char *name; std::vector<CatModel> models; };

// Eine kuratierte Auswahl. Weitere Modelle: Namen von ollama.com/library unten eintippen.
static const std::vector<CatFamily> &catalog() {
    static const std::vector<CatFamily> c = {
        {"Qwen", {
            {"qwen3:0.6b", "0.6B · winzig"},
            {"qwen3:1.7b", "1.7B · klein, schnell"},
            {"qwen3:4b", "4B · schnell, denkt mit"},
            {"qwen3:8b", "8B · gut für 6 GB"},
            {"qwen3:14b", "14B · braucht viel Speicher"},
            {"qwen3:30b", "30B (MoE) · sehr groß"},
            {"qwen2.5:1.5b", "1.5B · klein"},
            {"qwen2.5:3b", "3B · klein"},
            {"qwen2.5:7b", "7B · Allrounder"},
            {"qwen2.5:14b", "14B · groß"},
            {"qwen2.5:32b", "32B · sehr groß"},
            {"qwen2.5-coder:1.5b", "1.5B · Programmieren"},
            {"qwen2.5-coder:7b", "7B · Programmieren"},
            {"qwen2.5-coder:14b", "14B · Programmieren"},
            {"qwen2.5vl:3b", "3B · versteht Bilder"},
            {"qwen2.5vl:7b", "7B · versteht Bilder"}}},
        {"Llama", {
            {"llama3.2:1b", "1B · winzig"},
            {"llama3.2:3b", "3B · klein, schnell"},
            {"llama3.1:8b", "8B · Allrounder"},
            {"llama3.3:70b", "70B · riesig"},
            {"llama3.2-vision:11b", "11B · versteht Bilder"}}},
        {"Gemma", {
            {"gemma3:1b", "1B · winzig"},
            {"gemma3:4b", "4B · versteht Bilder"},
            {"gemma3:12b", "12B · versteht Bilder"},
            {"gemma3:27b", "27B · sehr groß"},
            {"gemma2:2b", "2B · klein"},
            {"gemma2:9b", "9B · gut"}}},
        {"Mistral", {
            {"mistral:7b", "7B · Allrounder"},
            {"mistral-nemo:12b", "12B · mehrsprachig"},
            {"mistral-small:24b", "24B · groß"},
            {"mixtral:8x7b", "8x7B · sehr groß"}}},
        {"DeepSeek", {
            {"deepseek-r1:1.5b", "1.5B · denkt Schritt für Schritt"},
            {"deepseek-r1:7b", "7B · denkt Schritt für Schritt"},
            {"deepseek-r1:8b", "8B · denkt Schritt für Schritt"},
            {"deepseek-r1:14b", "14B · denkt Schritt für Schritt"},
            {"deepseek-r1:32b", "32B · sehr groß"},
            {"deepseek-coder-v2:16b", "16B · Programmieren"}}},
        {"Phi", {
            {"phi3:mini", "3.8B · klein"},
            {"phi4-mini:3.8b", "3.8B · klein, neuer"},
            {"phi4:14b", "14B · stark, groß"}}},
        {"GPT-OSS", {
            {"gpt-oss:20b", "20B · offenes OpenAI-Modell, groß"},
            {"gpt-oss:120b", "120B · riesig"}}},
        {"Vision (Bilder)", {
            {"llava:7b", "7B · Bilder beschreiben"},
            {"llava:13b", "13B · Bilder beschreiben"},
            {"moondream:1.8b", "1.8B · winzig, Bilder"},
            {"minicpm-v:8b", "8B · Bilder"}}},
        {"Code", {
            {"codellama:7b", "7B · Programmieren"},
            {"codellama:13b", "13B · Programmieren"},
            {"starcoder2:3b", "3B · Programmieren"},
            {"starcoder2:7b", "7B · Programmieren"}}},
        {"Weitere", {
            {"granite3.3:2b", "2B · IBM, klein"},
            {"granite3.3:8b", "8B · IBM"},
            {"smollm2:1.7b", "1.7B · winzig"},
            {"tinyllama:1.1b", "1.1B · winzig"}}},
        {"Embeddings (Dokumentensuche)", {
            {"nomic-embed-text:latest", "für spätere Notiz-/Dokumentensuche, kein Chat"},
            {"mxbai-embed-large:latest", "für Dokumentensuche, kein Chat"},
            {"bge-m3:latest", "mehrsprachig, für Dokumentensuche, kein Chat"}}},
    };
    return c;
}

// Zu welcher Familie gehoert ein Modellname?
static QString familyOf(const QString &name) {
    const QString base = name.section(':', 0, 0).toLower();
    for (const CatFamily &f : catalog())
        for (const CatModel &m : f.models)
            if (QString(m.tag).section(':', 0, 0) == base) return QString(f.name);

    static const std::vector<std::pair<QString, QString>> rules = {
        {"qwen", "Qwen"}, {"llama", "Llama"}, {"gemma", "Gemma"}, {"mistral", "Mistral"},
        {"mixtral", "Mistral"}, {"deepseek", "DeepSeek"}, {"phi", "Phi"}};
    for (const auto &r : rules)
        if (base.startsWith(r.first)) return r.second;

    if (base.contains('/')) return "Weitere";
    QString letters;
    for (const QChar &ch : base) {
        if (ch.isLetter()) letters += ch; else break;
    }
    if (letters.isEmpty()) return "Weitere";
    letters[0] = letters[0].toUpper();
    return letters;
}

static bool isEmbeddingModel(const QString &name) {
    const QString b = name.section(':', 0, 0).toLower();
    return b.contains("embed") || b.startsWith("bge");
}

static QString fmtBytes(double b) {
    const char *u[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    while (b >= 1024.0 && i < 4) { b /= 1024.0; ++i; }
    return QString::number(b, 'f', i >= 3 ? 1 : 0) + " " + u[i];
}

struct ModelRow { QString tag, info; bool installed; qint64 size; };

struct ModelDl {
    QNetworkReply *reply = nullptr;
    QByteArray buf;
    QString err;
    bool success = false;
    bool running = false;
    bool closing = false;
    bool first = true;
    QSet<QString> installed;
};

// ---------------------------------------------------------------------------
//  Anhaenge
// ---------------------------------------------------------------------------
enum AttachKind { KText = 0, KImage = 1, KPdf = 2 };

struct Attachment {
    QString path, name;
    int kind = KText;
};

static bool classifyFile(const QString &path, int &kind, QString &err) {
    const QFileInfo fi(path);
    const QString ext = fi.suffix().toLower();
    static const QStringList imgs{"png", "jpg", "jpeg", "bmp", "gif", "webp"};
    if (imgs.contains(ext)) { kind = KImage; return true; }
    if (ext == "pdf") {
#ifdef HAVE_QTPDF
        kind = KPdf;
        return true;
#else
        err = "PDF-Dateien werden in dieser Version nicht unterstützt (das Modul Qt PDF fehlt).\n"
              "Alle anderen Dateien gehen: Bilder und Textdateien.";
        return false;
#endif
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        err = "Datei kann nicht gelesen werden: " + fi.fileName();
        return false;
    }
    if (f.read(4096).contains('\0')) {
        err = "Dieser Dateityp wird nicht unterstützt: " + fi.fileName() +
              "\n(Unterstützt: Bilder und Textdateien" +
#ifdef HAVE_QTPDF
              QString(" und PDF") +
#endif
              ")";
        return false;
    }
    kind = KText;
    return true;
}

static QString readTextFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    const QByteArray b = f.read(MAX_TEXT_READ);
    QString s = QString::fromUtf8(b);
    if (s.contains(QChar::ReplacementCharacter))      // keine UTF-8-Datei: Windows-Zeichensatz probieren
        s = QString::fromLocal8Bit(b);
    return s;
}

static QString pdfText(const QString &path, bool &ok) {
    ok = false;
#ifdef HAVE_QTPDF
    QPdfDocument doc;
    if (doc.load(path) != QPdfDocument::Error::None) return QString();
    QString out;
    for (int p = 0; p < doc.pageCount() && out.size() < MAX_ATTACH_CHARS; ++p)
        out += doc.getAllText(p).text() + "\n";
    ok = true;
    return out.trimmed();
#else
    Q_UNUSED(path);
    return QString();
#endif
}

// Bild verkleinern, auf weissen Hintergrund setzen und als JPEG (Base64) liefern
static QString imageToBase64(const QString &path) {
    QImage img(path);
    if (img.isNull()) return QString();
    if (qMax(img.width(), img.height()) > 1280)
        img = img.scaled(1280, 1280, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QImage flat(img.size(), QImage::Format_RGB32);
    flat.fill(Qt::white);
    QPainter p(&flat);
    p.drawImage(0, 0, img);
    p.end();
    QByteArray ba;
    QBuffer buf(&ba);
    buf.open(QIODevice::WriteOnly);
    flat.save(&buf, "JPEG", 85);
    return QString::fromLatin1(ba.toBase64());
}

// ---------------------------------------------------------------------------
//  Programmzustand
// ---------------------------------------------------------------------------
struct State {
    Chat       chat;                       // der aktuell geoeffnete Chat
    QString    currentProject;             // gerade geoeffnete Projektmappe (Seite 2)
    QByteArray buffer;
    QString    reply;
    QString    errorText;                  // Fehlermeldung von Ollama
    QString    lastModel = DEFAULT_MODEL;
    std::vector<Attachment> attachments;   // angehaengte, noch nicht gesendete Dateien
    std::shared_ptr<Message> aiMsg;
    bool       busy = false;
};

// Funktionen, die sich gegenseitig aufrufen (so koennen Lambdas sie sicher kopieren)
struct Ops {
    std::function<void()> refreshChips;
    std::function<void(const Chat &)> openChat;
    std::function<void()> showHome;
    std::function<void()> refreshHome;
    std::function<void(const QString &)> showProject;
    std::function<void()> refreshProject;
    std::function<void(const Chat &, QWidget *, std::function<void()>)> showChatMenu;
    std::function<void(const Project &, QWidget *, std::function<void()>)> showProjectMenu;
};

static void clearLayout(QLayout *l) {
    while (QLayoutItem *it = l->takeAt(0)) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
}

static QLabel *makeSection(const QString &text) {
    auto *l = new QLabel(text);
    l->setObjectName("sectionTitle");
    return l;
}

static QLabel *makeHint(const QString &text) {
    auto *l = new QLabel(text);
    l->setObjectName("hint");
    l->setWordWrap(true);
    return l;
}

// ---------------------------------------------------------------------------
//  Design
// ---------------------------------------------------------------------------
static const char* STYLE = R"(
    QWidget#root { background: #1c1b1a; }
    QFrame#panel { background: #262624; border: 1px solid #3a3936; border-radius: 18px; }
    QDialog, QMessageBox { background: #262624; }
    QLabel { color: #ececec; background: transparent; }
    QScrollArea { background: transparent; border: none; }
    QScrollArea > QWidget > QWidget { background: transparent; }

    QLineEdit {
        background: #30302e; color: #ececec;
        border: 1px solid #4a4944; border-radius: 20px;
        padding: 11px 18px; font-size: 11pt;
    }
    QLineEdit:focus { border: 1px solid #d97757; }
    QPushButton {
        background: #d97757; color: white; border: none;
        border-radius: 20px; padding: 11px 24px; font-weight: bold;
    }
    QPushButton:hover { background: #e58a6b; }
    QPushButton:disabled { background: #5a4a44; color: #999; }
    QPushButton#ghostBtn {
        background: transparent; color: #c9c7bf; border: 1px solid #4a4944;
        border-radius: 20px; padding: 11px 24px; font-weight: normal;
    }
    QPushButton#ghostBtn:hover { background: #3a3936; }
    QPushButton#ghostBtn:disabled { background: transparent; color: #666; border: 1px solid #3a3936; }
    QPushButton#smallBtn {
        background: transparent; color: #c9c7bf; border: 1px solid #4a4944;
        border-radius: 14px; padding: 6px 16px; font-weight: normal; font-size: 10pt;
    }
    QPushButton#smallBtn:hover { background: #3a3936; }

    QPushButton#attachBtn {
        background: #30302e; color: #c9c7bf; border: 1px solid #4a4944;
        border-radius: 21px; padding: 0; font-size: 17pt; font-weight: normal;
        min-width: 42px; max-width: 42px; min-height: 42px; max-height: 42px;
    }
    QPushButton#attachBtn:hover { background: #3a3936; color: white; }
    QPushButton#chip {
        background: #3b3a37; color: #ececec; border: 1px solid #4a4944;
        border-radius: 12px; padding: 4px 12px; font-size: 9pt; font-weight: normal;
    }
    QPushButton#chip:hover { background: #4a3a38; border: 1px solid #e06c75; }

    QWidget#userBubble { background: #3b3a37; border-radius: 16px; }
    QLabel#fileTag { color: #a8a69e; font-size: 9pt; }

    QPushButton#menuBtn {
        background: transparent; color: #c9c7bf; border: 1px solid #4a4944;
        border-radius: 14px; padding: 4px 13px; font-size: 13pt; font-weight: normal;
    }
    QPushButton#menuBtn:hover { background: #3a3936; }
    QPushButton#menuBtn:disabled { color: #777; }
    QPushButton#gearBtn {
        background: transparent; color: #8a8880; border: none; border-radius: 8px;
        padding: 3px 9px; font-size: 13pt; font-weight: normal;
    }
    QPushButton#gearBtn:hover { background: #3a3936; color: white; }
    QPushButton#gearBtn:disabled { color: #555; background: transparent; }
    QLabel#chatTitle { font-size: 11pt; font-weight: bold; }
    QLabel#modelLabel { color: #8a8880; font-size: 9pt; }

    QMenu {
        background: #30302e; color: #ececec; border: 1px solid #4a4944;
        border-radius: 8px; padding: 6px;
    }
    QMenu::item { padding: 7px 24px; border-radius: 6px; }
    QMenu::item:selected { background: #d97757; color: white; }
    QMenu::item:disabled { color: #777; }
    QMenu::separator { height: 1px; background: #4a4944; margin: 5px 8px; }

    QLabel#homeTitle { font-size: 20pt; font-weight: bold; }
    QLabel#sectionTitle { color: #8a8880; font-size: 9pt; font-weight: bold; padding: 8px 2px 0 2px; }
    QLabel#hint { color: #8a8880; font-size: 10pt; padding: 12px 4px; }
    QPushButton#chatRow {
        background: #2e2d2b; border: 1px solid #3a3936; border-radius: 12px;
        padding: 0; text-align: left; font-weight: normal;
    }
    QPushButton#chatRow:hover { background: #353431; border: 1px solid #4a4944; }
    QLabel#rowTitle { font-size: 11pt; font-weight: bold; }
    QLabel#rowSub { color: #8a8880; font-size: 9pt; }

    QLabel#dlgLabel { color: #a8a69e; font-size: 9pt; }
    QListWidget, QTreeWidget {
        background: #30302e; color: #ececec; border: 1px solid #4a4944;
        border-radius: 10px; padding: 4px; outline: 0; font-size: 10pt;
    }
    QListWidget::item { padding: 8px 12px; border-radius: 6px; }
    QListWidget::item:hover { background: #3a3936; }
    QListWidget::item:selected { background: #d97757; color: white; }
    QTreeWidget::item { padding: 6px 6px; border-radius: 6px; }
    QTreeWidget::item:hover { background: #3a3936; }
    QTreeWidget::item:selected { background: #d97757; color: white; }
    QHeaderView::section {
        background: #30302e; color: #a8a69e; border: none;
        padding: 4px 8px; font-size: 9pt;
    }
    QProgressBar {
        background: #30302e; border: 1px solid #4a4944; border-radius: 8px;
        text-align: center; color: #ececec; min-height: 16px; max-height: 16px;
    }
    QProgressBar::chunk { background: #d97757; border-radius: 7px; }

    QFrame#codeFrame { background: #1e1e1e; border: 1px solid #3f3e3a; border-radius: 10px; }
    QWidget#codeHeader { background: #2f2e2b; border-top-left-radius: 9px; border-top-right-radius: 9px; }
    QLabel#codeLang { color: #a8a69e; font-size: 9pt; background: transparent; }
    QPushButton#copyBtn {
        background: transparent; color: #c9c7bf; border: 1px solid #4a4944;
        border-radius: 6px; padding: 3px 12px; font-weight: normal; font-size: 9pt;
    }
    QPushButton#copyBtn:hover { background: #3a3936; }
    QPlainTextEdit#codeEdit {
        background: transparent; color: #e6e6e6; border: none;
        selection-background-color: #3d5a80; padding: 4px 6px;
    }

    QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
    QScrollBar::handle:vertical { background: #4a4944; border-radius: 4px; min-height: 30px; }
    QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
    QScrollBar::handle:horizontal { background: #4a4944; border-radius: 4px; min-width: 30px; }
    QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
    QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)";

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    // Absichtlich der alte Name: bestimmt den Datenordner (AppData/.../OllamaUI), damit die bisherigen Chats gefunden werden
    QCoreApplication::setApplicationName("OllamaUI");
    app.setFont(QFont("Segoe UI", 11));
    loadSettings();

    QWidget window;
    QWidget *win = &window;     // Zeiger aufs Fenster (der darf in Lambdas kopiert werden)
    window.setObjectName("root");
    window.setAttribute(Qt::WA_StyledBackground, true);
    window.setStyleSheet(STYLE);
    window.setWindowTitle("LocalAI");
    window.resize(900, 760);
    window.setAcceptDrops(true);

    auto *net   = new QNetworkAccessManager(win);
    auto  state = std::make_shared<State>();
    auto  ops   = std::make_shared<Ops>();

    // =====================  Seite 1: Chat  =====================
    auto *chatScroll = new QScrollArea;
    chatScroll->setWidgetResizable(true);
    chatScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *chatContent = new QWidget;
    auto *chatOuter = new QHBoxLayout(chatContent);
    chatOuter->setContentsMargins(24, 16, 24, 16);
    auto *chatColumn = new QWidget;
    chatColumn->setMaximumWidth(820);
    auto *chatLayout = new QVBoxLayout(chatColumn);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->setSpacing(18);
    chatLayout->addStretch(1);
    chatOuter->addStretch(1);
    chatOuter->addWidget(chatColumn, 100);
    chatOuter->addStretch(1);
    chatScroll->setWidget(chatContent);

    // Anhang-Chips und Eingabezeile
    auto *chipBar = new QWidget;
    auto *chipLayout = new QHBoxLayout(chipBar);
    chipLayout->setContentsMargins(24, 0, 24, 0);
    chipLayout->setSpacing(8);
    chipBar->hide();

    auto *attachBtn = new QPushButton("+");
    attachBtn->setObjectName("attachBtn");
    attachBtn->setCursor(Qt::PointingHandCursor);
    attachBtn->setToolTip("Datei anhängen (oder Datei ins Fenster ziehen)");
    auto *input = new QLineEdit;
    input->setPlaceholderText("Nachricht eingeben ...");
    auto *send = new QPushButton("Senden");

    auto *inputRow = new QHBoxLayout;
    inputRow->setContentsMargins(24, 4, 24, 20);
    inputRow->setSpacing(10);
    inputRow->addWidget(attachBtn);
    inputRow->addWidget(input, 1);
    inputRow->addWidget(send);

    auto *chatPage = new QWidget;
    auto *chatPageLayout = new QVBoxLayout(chatPage);
    chatPageLayout->setContentsMargins(0, 0, 0, 0);
    chatPageLayout->setSpacing(6);
    chatPageLayout->addWidget(chatScroll, 1);
    chatPageLayout->addWidget(chipBar);
    chatPageLayout->addLayout(inputRow);

    // =====================  Seite 0: Startseite  =====================
    auto *homeScroll = new QScrollArea;
    homeScroll->setWidgetResizable(true);
    homeScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *homeContent = new QWidget;
    auto *homeOuter = new QHBoxLayout(homeContent);
    homeOuter->setContentsMargins(24, 16, 24, 16);
    auto *homeColumn = new QWidget;
    homeColumn->setMaximumWidth(820);
    auto *homeV = new QVBoxLayout(homeColumn);
    homeV->setContentsMargins(0, 0, 0, 0);
    homeV->setSpacing(14);

    auto *homeHead = new QHBoxLayout;
    auto *homeTitle = new QLabel("Deine Chats");
    homeTitle->setObjectName("homeTitle");
    auto *homeNewProject = new QPushButton("+  Neue Projektmappe");
    homeNewProject->setObjectName("ghostBtn");
    homeNewProject->setCursor(Qt::PointingHandCursor);
    auto *homeNew = new QPushButton("+  Neuer Chat");
    homeNew->setCursor(Qt::PointingHandCursor);
    homeHead->addWidget(homeTitle);
    homeHead->addStretch(1);
    homeHead->addWidget(homeNewProject);
    homeHead->addSpacing(8);
    homeHead->addWidget(homeNew);
    homeV->addLayout(homeHead);

    auto *listBox = new QWidget;
    auto *listLayout = new QVBoxLayout(listBox);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(10);
    homeV->addWidget(listBox);
    homeV->addStretch(1);

    homeOuter->addStretch(1);
    homeOuter->addWidget(homeColumn, 100);
    homeOuter->addStretch(1);
    homeScroll->setWidget(homeContent);

    // =====================  Seite 2: Projektmappe  =====================
    auto *projScroll = new QScrollArea;
    projScroll->setWidgetResizable(true);
    projScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *projContent = new QWidget;
    auto *projOuter = new QHBoxLayout(projContent);
    projOuter->setContentsMargins(24, 16, 24, 16);
    auto *projColumn = new QWidget;
    projColumn->setMaximumWidth(820);
    auto *projV = new QVBoxLayout(projColumn);
    projV->setContentsMargins(0, 0, 0, 0);
    projV->setSpacing(14);

    auto *projBack = new QPushButton("‹  Zurück");
    projBack->setObjectName("smallBtn");
    projBack->setCursor(Qt::PointingHandCursor);
    auto *projTitle = new QLabel;
    projTitle->setObjectName("homeTitle");
    auto *projGear = new QPushButton("⚙");
    projGear->setObjectName("gearBtn");
    projGear->setCursor(Qt::PointingHandCursor);
    projGear->setToolTip("Einstellungen der Projektmappe");
    auto *projNew = new QPushButton("+  Neuer Chat");
    projNew->setCursor(Qt::PointingHandCursor);

    auto *projHead = new QHBoxLayout;
    projHead->addWidget(projBack);
    projHead->addSpacing(10);
    projHead->addWidget(projTitle);
    projHead->addWidget(projGear);
    projHead->addStretch(1);
    projHead->addWidget(projNew);
    projV->addLayout(projHead);

    auto *projListBox = new QWidget;
    auto *projList = new QVBoxLayout(projListBox);
    projList->setContentsMargins(0, 0, 0, 0);
    projList->setSpacing(10);
    projV->addWidget(projListBox);
    projV->addStretch(1);

    projOuter->addStretch(1);
    projOuter->addWidget(projColumn, 100);
    projOuter->addStretch(1);
    projScroll->setWidget(projContent);

    auto *stack = new QStackedWidget;
    stack->addWidget(homeScroll);   // Index 0
    stack->addWidget(chatPage);     // Index 1
    stack->addWidget(projScroll);   // Index 2

    // =====================  Obere Leiste  =====================
    auto *menuBtn = new QPushButton("☰");
    menuBtn->setObjectName("menuBtn");
    menuBtn->setCursor(Qt::PointingHandCursor);
    auto *mainMenu = new QMenu(menuBtn);
    QAction *actNew      = mainMenu->addAction("Neuer Chat");
    QAction *actHome     = mainMenu->addAction("Alle Chats");
    QAction *actProject  = mainMenu->addAction("Zur Projektmappe");
    mainMenu->addSeparator();
    QAction *actModels   = mainMenu->addAction("KI-Modelle verwalten ...");
    QAction *actFolder   = mainMenu->addAction("Chat-Ordner öffnen");
    QAction *actSettings = mainMenu->addAction("Einstellungen ...");

    auto *chatTitle = new QLabel;
    chatTitle->setObjectName("chatTitle");
    auto *chatGear = new QPushButton("⚙");
    chatGear->setObjectName("gearBtn");
    chatGear->setCursor(Qt::PointingHandCursor);
    chatGear->setToolTip("Chat-Einstellungen");
    chatGear->hide();
    auto *modelLabel = new QLabel;
    modelLabel->setObjectName("modelLabel");

    auto *top = new QHBoxLayout;
    top->setContentsMargins(24, 4, 24, 0);
    top->addWidget(menuBtn);
    top->addSpacing(12);
    top->addWidget(chatTitle);
    top->addSpacing(2);
    top->addWidget(chatGear);
    top->addStretch(1);
    top->addWidget(modelLabel);

    // =====================  Rahmen um das Ganze  =====================
    auto *outer = new QVBoxLayout(win);
    outer->setContentsMargins(16, 16, 16, 16);
    auto *panel = new QFrame;
    panel->setObjectName("panel");
    outer->addWidget(panel);
    auto *pv = new QVBoxLayout(panel);
    pv->setContentsMargins(0, 12, 0, 0);
    pv->setSpacing(0);
    pv->addLayout(top);
    pv->addWidget(stack, 1);

    QObject::connect(chatScroll->verticalScrollBar(), &QScrollBar::rangeChanged,
                     [=](int, int max) { if (state->busy) chatScroll->verticalScrollBar()->setValue(max); });

    // =====================  Anhaenge verwalten  =====================
    ops->refreshChips = [=]() {
        clearLayout(chipLayout);
        for (size_t i = 0; i < state->attachments.size(); ++i) {
            QString n = state->attachments[i].name;
            if (n.size() > 26) n = n.left(24) + "...";
            auto *chip = new QPushButton(n + "  ✕");
            chip->setObjectName("chip");
            chip->setCursor(Qt::PointingHandCursor);
            chip->setToolTip("Klicken zum Entfernen");
            chipLayout->addWidget(chip);
            QObject::connect(chip, &QPushButton::clicked, [=]() {
                if (i < state->attachments.size())
                    state->attachments.erase(state->attachments.begin() + i);
                ops->refreshChips();
            });
        }
        chipLayout->addStretch(1);
        chipBar->setVisible(!state->attachments.empty());
    };

    auto addAttachments = [=](const QStringList &paths) {
        for (const QString &p : paths) {
            const QFileInfo fi(p);
            if (!fi.isFile()) continue;
            if (fi.size() > MAX_FILE_BYTES) {
                QMessageBox::warning(win, "Anhang", "Die Datei ist zu groß (max. 25 MB): " + fi.fileName());
                continue;
            }
            bool dup = false;
            for (const Attachment &a : state->attachments) if (a.path == p) dup = true;
            if (dup) continue;
            int kind = KText;
            QString err;
            if (!classifyFile(p, kind, err)) {
                QMessageBox::warning(win, "Anhang", err);
                continue;
            }
            Attachment a;
            a.path = p;
            a.name = fi.fileName();
            a.kind = kind;
            state->attachments.push_back(a);
        }
        ops->refreshChips();
    };

    QObject::connect(attachBtn, &QPushButton::clicked, [=]() {
        const QString filter = "Unterstützte Dateien (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.pdf *.txt *.md "
                               "*.csv *.json *.xml *.log *.ini *.yaml *.yml *.py *.cpp *.c *.h *.hpp *.js *.ts "
                               "*.html *.css *.sql *.sh *.bat);;Alle Dateien (*)";
        const QStringList files = QFileDialog::getOpenFileNames(win, "Dateien anhängen", QString(), filter);
        if (!files.isEmpty()) addAttachments(files);
    });

    auto *dropFilter = new DropFilter(win);
    dropFilter->onDrop = [=](const QStringList &files) { addAttachments(files); };
    window.installEventFilter(dropFilter);

    // =====================  Nachrichten anzeigen  =====================
    auto addUser = [=](const QString &text, const QStringList &files, const QStringList &images) {
        auto *bubble = new QWidget;
        bubble->setObjectName("userBubble");
        bubble->setAttribute(Qt::WA_StyledBackground, true);
        bubble->setMaximumWidth(560);
        auto *bl = new QVBoxLayout(bubble);
        bl->setContentsMargins(16, 11, 16, 11);
        bl->setSpacing(8);

        if (!images.isEmpty()) {
            auto *thumbRow = new QHBoxLayout;
            thumbRow->setSpacing(8);
            for (const QString &b64 : images) {
                const QImage img = QImage::fromData(QByteArray::fromBase64(b64.toLatin1()));
                if (img.isNull()) continue;
                auto *pic = new QLabel;
                pic->setPixmap(QPixmap::fromImage(img.scaledToHeight(150, Qt::SmoothTransformation)));
                thumbRow->addWidget(pic);
            }
            thumbRow->addStretch(1);
            bl->addLayout(thumbRow);
        }
        for (const QString &f : files) {
            auto *tag = new QLabel("Datei: " + f);
            tag->setObjectName("fileTag");
            bl->addWidget(tag);
        }
        if (!text.isEmpty()) {
            auto *label = new QLabel;
            label->setTextFormat(Qt::PlainText);
            label->setWordWrap(true);
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
            label->setText(text);
            bl->addWidget(label);
        }

        auto *rowWidget = new QWidget;
        auto *row = new QHBoxLayout(rowWidget);
        row->setContentsMargins(0, 0, 0, 0);
        row->addStretch(1);
        row->addWidget(bubble);
        chatLayout->insertWidget(chatLayout->count() - 1, rowWidget);
    };

    auto addAi = [=]() -> std::shared_ptr<Message> {
        auto m = std::make_shared<Message>();
        m->box = new QWidget;
        m->lay = new QVBoxLayout(m->box);
        m->lay->setContentsMargins(0, 0, 0, 0);
        m->lay->setSpacing(10);
        chatLayout->insertWidget(chatLayout->count() - 1, m->box);
        updateMessage(*m, QString());
        return m;
    };

    auto clearChatView = [=]() {
        while (chatLayout->count() > 1) {            // alles ausser dem Stretch am Ende
            QLayoutItem *it = chatLayout->takeAt(0);
            if (it->widget()) it->widget()->deleteLater();
            delete it;
        }
    };

    // Titel, Modell und Projektmappe oben im Chat anzeigen
    auto updateChatHeader = [=]() {
        chatTitle->setText(state->chat.title);
        QString info = state->chat.model;
        if (!state->chat.projectId.isEmpty()) {
            for (const Project &p : loadProjects())
                if (p.id == state->chat.projectId) info += "   ·   Mappe: " + p.name;
        }
        modelLabel->setText(info);
    };

    // =====================  Chat oeffnen / Seiten wechseln  =====================
    ops->openChat = [=](const Chat &c) {
        state->chat = c;
        state->aiMsg.reset();
        state->attachments.clear();
        ops->refreshChips();
        clearChatView();
        for (const QJsonValue &v : c.messages) {
            const QJsonObject m = v.toObject();
            const QString role = m.value("role").toString();
            if (role == "user") {
                QString shown = m.contains("display") ? m.value("display").toString()
                                                      : m.value("content").toString();
                QStringList files, images;
                for (const QJsonValue &f : m.value("files").toArray())  files  << f.toString();
                for (const QJsonValue &i : m.value("images").toArray()) images << i.toString();
                addUser(shown, files, images);
            } else {
                auto msg = addAi();
                updateMessage(*msg, m.value("content").toString());
            }
        }
        updateChatHeader();
        chatGear->show();
        chatGear->setEnabled(true);
        stack->setCurrentIndex(1);
        input->setFocus();
        QTimer::singleShot(150, [=]() {
            chatScroll->verticalScrollBar()->setValue(chatScroll->verticalScrollBar()->maximum());
        });
    };

    ops->showHome = [=]() {
        ops->refreshHome();
        chatTitle->clear();
        modelLabel->clear();
        chatGear->hide();
        stack->setCurrentIndex(0);
    };

    ops->showProject = [=](const QString &pid) {
        state->currentProject = pid;
        ops->refreshProject();
        if (state->currentProject.isEmpty()) return;     // Mappe gab es nicht mehr -> Startseite wurde gezeigt
        chatTitle->clear();
        modelLabel->clear();
        chatGear->hide();
        stack->setCurrentIndex(2);
    };

    // =====================  Eine Zeile fuer einen Chat in einer Liste  =====================
    auto addChatRow = [=](QVBoxLayout *target, const Chat &c, std::function<void()> after) {
        auto *row = new QPushButton;
        row->setObjectName("chatRow");
        row->setCursor(Qt::PointingHandCursor);
        row->setMinimumHeight(70);

        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(18, 10, 10, 10);
        auto *v = new QVBoxLayout;
        v->setSpacing(2);

        auto *t = new QLabel(c.title);
        t->setObjectName("rowTitle");
        t->setAttribute(Qt::WA_TransparentForMouseEvents);
        const QString date = QDateTime::fromString(c.updated, Qt::ISODate).toString("dd.MM.yyyy  HH:mm");
        auto *s = new QLabel(c.model + "   ·   " + date);
        s->setObjectName("rowSub");
        s->setAttribute(Qt::WA_TransparentForMouseEvents);
        v->addWidget(t);
        v->addWidget(s);

        auto *gear = new QPushButton("⚙");
        gear->setObjectName("gearBtn");
        gear->setCursor(Qt::PointingHandCursor);
        gear->setToolTip("Chat-Einstellungen");

        h->addLayout(v, 1);
        h->addWidget(gear);
        target->addWidget(row);

        const Chat cc = c;
        QObject::connect(row,  &QPushButton::clicked, [=]() { ops->openChat(cc); });
        QObject::connect(gear, &QPushButton::clicked, [=]() { ops->showChatMenu(cc, gear, after); });
    };

    ops->refreshHome = [=]() {
        clearLayout(listLayout);
        const std::vector<Project> projects = loadProjects();
        const std::vector<Chat> chats = loadAllChats();

        if (projects.empty() && chats.empty()) {
            listLayout->addWidget(makeHint("Noch keine Chats vorhanden. Starte oben rechts einen neuen Chat "
                                           "oder lege eine Projektmappe an."));
            return;
        }

        if (!projects.empty()) {
            listLayout->addWidget(makeSection("PROJEKTMAPPEN"));
            for (const Project &p : projects) {
                int n = 0;
                for (const Chat &c : chats) if (c.projectId == p.id) ++n;

                auto *row = new QPushButton;
                row->setObjectName("chatRow");
                row->setCursor(Qt::PointingHandCursor);
                row->setMinimumHeight(70);
                auto *h = new QHBoxLayout(row);
                h->setContentsMargins(18, 10, 10, 10);
                auto *v = new QVBoxLayout;
                v->setSpacing(2);
                auto *t = new QLabel(p.name);
                t->setObjectName("rowTitle");
                t->setAttribute(Qt::WA_TransparentForMouseEvents);
                auto *s = new QLabel(QString::number(n) + (n == 1 ? " Chat" : " Chats"));
                s->setObjectName("rowSub");
                s->setAttribute(Qt::WA_TransparentForMouseEvents);
                v->addWidget(t);
                v->addWidget(s);
                auto *gear = new QPushButton("⚙");
                gear->setObjectName("gearBtn");
                gear->setCursor(Qt::PointingHandCursor);
                gear->setToolTip("Einstellungen der Projektmappe");
                h->addLayout(v, 1);
                h->addWidget(gear);
                listLayout->addWidget(row);

                const Project pc = p;
                QObject::connect(row,  &QPushButton::clicked, [=]() { ops->showProject(pc.id); });
                QObject::connect(gear, &QPushButton::clicked, [=]() {
                    ops->showProjectMenu(pc, gear, [=]() { ops->refreshHome(); });
                });
            }
        }

        listLayout->addWidget(makeSection("CHATS"));
        int free = 0;
        for (const Chat &c : chats) {
            if (!c.projectId.isEmpty()) {
                bool exists = false;
                for (const Project &p : projects) if (p.id == c.projectId) exists = true;
                if (exists) continue;
            }
            addChatRow(listLayout, c, [=]() { ops->refreshHome(); });
            ++free;
        }
        if (free == 0)
            listLayout->addWidget(makeHint("Keine Chats außerhalb von Projektmappen."));
    };

    ops->refreshProject = [=]() {
        const std::vector<Project> projects = loadProjects();
        const Project *found = nullptr;
        for (const Project &p : projects) if (p.id == state->currentProject) found = &p;
        if (!found) {                       // Mappe existiert nicht mehr
            state->currentProject.clear();
            ops->showHome();
            return;
        }
        projTitle->setText(found->name);
        clearLayout(projList);
        int n = 0;
        for (const Chat &c : loadAllChats()) {
            if (c.projectId != found->id) continue;
            addChatRow(projList, c, [=]() { ops->refreshProject(); });
            ++n;
        }
        if (n == 0) projList->addWidget(makeHint("Noch keine Chats in dieser Projektmappe."));
    };

    // =====================  Auswahl einer Projektmappe  =====================
    auto pickProject = [=](QWidget *parent, const QString &currentId, QString &outId) -> bool {
        const std::vector<Project> projects = loadProjects();
        if (projects.empty()) {
            QMessageBox::information(parent, "Projektmappen",
                "Es gibt noch keine Projektmappen. Lege zuerst auf der Startseite eine an.");
            return false;
        }
        QDialog dlg(parent);
        dlg.setWindowTitle("Projektmappe wählen");
        dlg.setMinimumWidth(420);
        auto *v = new QVBoxLayout(&dlg);
        v->setContentsMargins(26, 24, 26, 22);
        v->setSpacing(10);
        auto *l = new QLabel("In welche Projektmappe soll der Chat?");
        l->setObjectName("dlgLabel");
        auto *list = new QListWidget;
        list->setMinimumHeight(200);
        auto *none = new QListWidgetItem("–  Keine Projektmappe  –");
        none->setData(Qt::UserRole, QString());
        list->addItem(none);
        int sel = 0;
        for (const Project &p : projects) {
            auto *it = new QListWidgetItem(p.name);
            it->setData(Qt::UserRole, p.id);
            list->addItem(it);
            if (p.id == currentId) sel = list->count() - 1;
        }
        list->setCurrentRow(sel);

        auto *btns = new QHBoxLayout;
        btns->addStretch(1);
        auto *cancel = new QPushButton("Abbrechen");
        cancel->setObjectName("ghostBtn");
        auto *ok = new QPushButton("Übernehmen");
        btns->addWidget(cancel);
        btns->addWidget(ok);
        v->addWidget(l);
        v->addWidget(list, 1);
        v->addLayout(btns);

        QObject::connect(cancel, &QPushButton::clicked,           &dlg, &QDialog::reject);
        QObject::connect(ok,     &QPushButton::clicked,           &dlg, &QDialog::accept);
        QObject::connect(list,   &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);

        if (dlg.exec() != QDialog::Accepted || !list->currentItem()) return false;
        outId = list->currentItem()->data(Qt::UserRole).toString();
        return true;
    };

    // Aenderung an einem Chat speichern (ohne ihn "nach oben" zu schieben)
    auto applyChatChange = [=](Chat &c) {
        saveChatFile(c, false);
        if (state->chat.id == c.id) {
            state->chat.title     = c.title;
            state->chat.projectId = c.projectId;
            updateChatHeader();
        }
    };

    // =====================  Zahnrad-Menue eines Chats  =====================
    ops->showChatMenu = [=](const Chat &cIn, QWidget *anchor, std::function<void()> after) {
        Chat c = cIn;
        QMenu menu(win);
        QAction *aRename = menu.addAction("Umbenennen ...");
        QAction *aMove   = menu.addAction(c.projectId.isEmpty() ? "Zu Projektmappe hinzufügen ..."
                                                                : "In andere Projektmappe verschieben ...");
        QAction *aRemove = nullptr;
        if (!c.projectId.isEmpty()) aRemove = menu.addAction("Aus Projektmappe entfernen");
        menu.addSeparator();
        QAction *aDelete = menu.addAction("Löschen ...");

        QAction *chosen = menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height() + 4)));
        if (!chosen) return;

        if (chosen == aRename) {
            bool ok = false;
            const QString name = QInputDialog::getText(win, "Chat umbenennen", "Neuer Name:",
                                                       QLineEdit::Normal, c.title, &ok).trimmed();
            if (!ok || name.isEmpty()) return;
            c.title = name;
            applyChatChange(c);
            after();
        } else if (chosen == aMove) {
            QString outId;
            if (!pickProject(win, c.projectId, outId)) return;
            c.projectId = outId;
            applyChatChange(c);
            after();
        } else if (aRemove && chosen == aRemove) {
            c.projectId.clear();
            applyChatChange(c);
            after();
        } else if (chosen == aDelete) {
            const auto answer = QMessageBox::question(win, "Chat löschen",
                    QString("Chat \"%1\" wirklich löschen?").arg(c.title));
            if (answer != QMessageBox::Yes) return;
            QFile::remove(chatPath(c.id));
            if (state->chat.id == c.id) state->chat = Chat();
            if (stack->currentIndex() == 1) ops->showHome();
            else after();
        }
    };

    // =====================  Zahnrad-Menue einer Projektmappe  =====================
    ops->showProjectMenu = [=](const Project &pIn, QWidget *anchor, std::function<void()> after) {
        const Project p = pIn;
        QMenu menu(win);
        QAction *aRename = menu.addAction("Umbenennen ...");
        menu.addSeparator();
        QAction *aDelete = menu.addAction("Projektmappe löschen ...");

        QAction *chosen = menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height() + 4)));
        if (!chosen) return;

        if (chosen == aRename) {
            bool ok = false;
            const QString name = QInputDialog::getText(win, "Projektmappe umbenennen", "Neuer Name:",
                                                       QLineEdit::Normal, p.name, &ok).trimmed();
            if (!ok || name.isEmpty()) return;
            std::vector<Project> all = loadProjects();
            for (Project &x : all) if (x.id == p.id) x.name = name;
            saveProjects(all);
            after();
        } else if (chosen == aDelete) {
            const auto answer = QMessageBox::question(win, "Projektmappe löschen",
                    QString("Projektmappe \"%1\" löschen?\nDie Chats darin bleiben erhalten.").arg(p.name));
            if (answer != QMessageBox::Yes) return;
            std::vector<Project> all = loadProjects();
            all.erase(std::remove_if(all.begin(), all.end(),
                                     [&](const Project &x) { return x.id == p.id; }), all.end());
            saveProjects(all);
            for (Chat c : loadAllChats()) {                 // Chats aus der Mappe loesen
                if (c.projectId == p.id) { c.projectId.clear(); saveChatFile(c, false); }
            }
            if (state->chat.projectId == p.id) state->chat.projectId.clear();
            if (state->currentProject == p.id) {
                state->currentProject.clear();
                ops->showHome();
            } else {
                after();
            }
        }
    };

    auto createProject = [=]() {
        bool ok = false;
        const QString name = QInputDialog::getText(win, "Neue Projektmappe", "Name der Projektmappe:",
                                                   QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        std::vector<Project> all = loadProjects();
        Project p;
        p.id      = QString::number(QDateTime::currentMSecsSinceEpoch());
        p.name    = name;
        p.created = QDateTime::currentDateTime().toString(Qt::ISODate);
        all.push_back(p);
        saveProjects(all);
        ops->refreshHome();
    };

    // =====================  Neuer Chat: Dialog mit Titel und Modell  =====================
    auto showNewChatDialog = [=](const QStringList &namesIn, const QString &projectId) {
        QString projectName;
        if (!projectId.isEmpty())
            for (const Project &p : loadProjects()) if (p.id == projectId) projectName = p.name;

        // Embedding-Modelle koennen nicht chatten -> ausblenden
        QStringList names;
        for (const QString &n : namesIn) if (!isEmbeddingModel(n)) names << n;
        if (names.isEmpty()) names = namesIn;

        QDialog dlg(win);
        QDialog *dp = &dlg;
        dlg.setWindowTitle(projectName.isEmpty() ? QString("Neuer Chat")
                                                 : "Neuer Chat in: " + projectName);
        dlg.setMinimumWidth(480);

        auto *v = new QVBoxLayout(&dlg);
        v->setContentsMargins(26, 24, 26, 22);
        v->setSpacing(8);

        auto *l1 = new QLabel("Überschrift (für das Archiv)");
        l1->setObjectName("dlgLabel");
        auto *titleEdit = new QLineEdit;
        titleEdit->setPlaceholderText("z. B. Meshbot-Projekt");

        auto *l2 = new QLabel("KI-Modell");
        l2->setObjectName("dlgLabel");

        // Modelle nach Familie gruppiert
        auto *tree = new QTreeWidget;
        tree->setHeaderHidden(true);
        tree->setRootIsDecorated(false);
        tree->setItemsExpandable(false);
        tree->setIndentation(10);
        tree->setMinimumHeight(240);

        QMap<QString, QStringList> groups;
        for (const QString &n : names) groups[familyOf(n)] << n;
        QTreeWidgetItem *firstChild = nullptr, *preselect = nullptr;
        for (auto it = groups.begin(); it != groups.end(); ++it) {
            auto *topItem = new QTreeWidgetItem(tree);
            topItem->setText(0, it.key().toUpper());
            QFont bf = topItem->font(0);
            bf.setBold(true);
            bf.setPointSize(9);
            topItem->setFont(0, bf);
            topItem->setForeground(0, QColor("#8a8880"));
            topItem->setFlags(Qt::ItemIsEnabled);              // nicht auswaehlbar
            QStringList sorted = it.value();
            sorted.sort();
            for (const QString &n : sorted) {
                auto *child = new QTreeWidgetItem(topItem);
                child->setText(0, n);
                child->setData(0, Qt::UserRole, n);
                if (!firstChild) firstChild = child;
                if (n == state->lastModel) preselect = child;
            }
        }
        tree->expandAll();
        tree->setCurrentItem(preselect ? preselect : firstChild);

        auto *btns = new QHBoxLayout;
        btns->addStretch(1);
        auto *cancel = new QPushButton("Abbrechen");
        cancel->setObjectName("ghostBtn");
        auto *ok = new QPushButton("Starten");
        btns->addWidget(cancel);
        btns->addWidget(ok);

        v->addWidget(l1);
        v->addWidget(titleEdit);
        v->addSpacing(8);
        v->addWidget(l2);
        v->addWidget(tree, 1);
        v->addSpacing(8);
        v->addLayout(btns);

        QObject::connect(cancel,    &QPushButton::clicked,     &dlg, &QDialog::reject);
        QObject::connect(ok,        &QPushButton::clicked,     &dlg, &QDialog::accept);
        QObject::connect(titleEdit, &QLineEdit::returnPressed, &dlg, &QDialog::accept);
        QObject::connect(tree, &QTreeWidget::itemDoubleClicked, &dlg, [=](QTreeWidgetItem *it, int) {
            if (it->parent()) dp->accept();
        });
        titleEdit->setFocus();

        if (dlg.exec() != QDialog::Accepted) return;

        QString model;
        if (tree->currentItem()) model = tree->currentItem()->data(0, Qt::UserRole).toString();
        if (model.isEmpty() && firstChild) model = firstChild->data(0, Qt::UserRole).toString();
        if (model.isEmpty()) model = names.first();

        const QDateTime now = QDateTime::currentDateTime();
        Chat c;
        c.id        = QString::number(now.toMSecsSinceEpoch());
        c.title     = titleEdit->text().trimmed();
        if (c.title.isEmpty()) c.title = "Chat vom " + now.toString("dd.MM.yyyy HH:mm");
        c.model     = model;
        c.created   = now.toString(Qt::ISODate);
        c.updated   = c.created;
        c.projectId = projectId;
        state->lastModel = c.model;
        saveChatFile(c);
        ops->openChat(c);
    };

    auto newChatFlow = [=](const QString &projectId) {
        if (state->busy) return;
        QNetworkReply *r = net->get(QNetworkRequest(QUrl(TAGS_URL)));
        QObject::connect(r, &QNetworkReply::finished, [=]() {
            QStringList names;
            if (r->error() == QNetworkReply::NoError) {
                const QJsonArray arr = QJsonDocument::fromJson(r->readAll()).object().value("models").toArray();
                for (const QJsonValue &v : arr)
                    names << v.toObject().value("name").toString();
            }
            r->deleteLater();
            if (names.isEmpty()) {
                QMessageBox::warning(win, "Ollama",
                    "Die Modellliste konnte nicht geladen werden oder es ist noch kein Modell installiert.\n"
                    "Läuft Ollama? Unter ☰ → KI-Modelle verwalten kannst du Modelle herunterladen.");
                return;
            }
            showNewChatDialog(names, projectId);
        });
    };

    // =====================  KI-Modelle verwalten (Download / Loeschen)  =====================
    auto showModelManager = [=]() {
        auto dl = std::make_shared<ModelDl>();
        std::function<void()> reload;               // vor dem Dialog deklariert -> wird nach ihm zerstoert

        QDialog dlg(win);
        QDialog *dp = &dlg;
        dlg.setWindowTitle("KI-Modelle verwalten");
        dlg.setMinimumSize(700, 640);

        auto *v = new QVBoxLayout(&dlg);
        v->setContentsMargins(26, 24, 26, 22);
        v->setSpacing(8);

        auto *intro = new QLabel("Wähle ein Modell aus der Liste oder tippe unten einen beliebigen Namen "
                                 "aus der Ollama-Bibliothek ein (z. B. mistral:7b).");
        intro->setObjectName("dlgLabel");
        intro->setWordWrap(true);

        auto *tree = new QTreeWidget;
        tree->setColumnCount(3);
        tree->setHeaderLabels(QStringList{"Modell", "Info", "Status"});
        tree->setRootIsDecorated(false);
        tree->setIndentation(12);
        tree->setUniformRowHeights(true);
        tree->header()->setStretchLastSection(true);
        tree->setColumnWidth(0, 230);
        tree->setColumnWidth(1, 250);

        auto *nameEdit = new QLineEdit;
        nameEdit->setPlaceholderText("Modellname, z. B. llama3.2:3b");

        auto *progress = new QProgressBar;
        progress->setRange(0, 1000);
        progress->setValue(0);
        progress->hide();
        auto *statusLabel = new QLabel;
        statusLabel->setObjectName("dlgLabel");
        statusLabel->setWordWrap(true);

        auto *hint = new QLabel(
            "Tipp: Mit 6 GB Grafikspeicher laufen Modelle bis ca. 8B gut. Größere werden langsamer, "
            "weil sie teilweise im normalen Arbeitsspeicher laufen.\n"
            "Hinweis: Claude (Anthropic) läuft nur in der Cloud und lässt sich nicht herunterladen.");
        hint->setObjectName("dlgLabel");
        hint->setWordWrap(true);

        auto *pullBtn = new QPushButton("Herunterladen");
        auto *cancelBtn = new QPushButton("Abbrechen");
        cancelBtn->setObjectName("ghostBtn");
        cancelBtn->hide();
        auto *delBtn = new QPushButton("Löschen");
        delBtn->setObjectName("ghostBtn");
        auto *closeBtn = new QPushButton("Schließen");
        closeBtn->setObjectName("ghostBtn");

        auto *nameRow = new QHBoxLayout;
        nameRow->setSpacing(8);
        nameRow->addWidget(nameEdit, 1);
        nameRow->addWidget(pullBtn);
        nameRow->addWidget(cancelBtn);

        auto *bottom = new QHBoxLayout;
        bottom->addWidget(delBtn);
        bottom->addStretch(1);
        bottom->addWidget(closeBtn);

        v->addWidget(intro);
        v->addWidget(tree, 1);
        v->addSpacing(4);
        v->addLayout(nameRow);
        v->addWidget(progress);
        v->addWidget(statusLabel);
        v->addWidget(hint);
        v->addSpacing(6);
        v->addLayout(bottom);

        // Familien-Zeile beschriften (mit Pfeil fuer auf-/zugeklappt)
        auto famText = [](QTreeWidgetItem *it) {
            it->setText(0, QString(it->isExpanded() ? "▾  " : "▸  ") +
                           it->data(0, Qt::UserRole + 1).toString() +
                           "  (" + QString::number(it->childCount()) + ")");
        };

        auto updateButtons = [=]() {
            const QString tag = nameEdit->text().trimmed();
            pullBtn->setEnabled(!dl->running && !tag.isEmpty());
            delBtn->setEnabled(!dl->running && dl->installed.contains(tag));
        };

        // Liste neu aufbauen: installierte Modelle von Ollama holen
        reload = [=]() {
            QNetworkReply *r = net->get(QNetworkRequest(QUrl(TAGS_URL)));
            QObject::connect(r, &QNetworkReply::finished, dp, [=]() {
                QMap<QString, qint64> inst;
                const bool ok = (r->error() == QNetworkReply::NoError);
                if (ok) {
                    const QJsonArray arr = QJsonDocument::fromJson(r->readAll()).object().value("models").toArray();
                    for (const QJsonValue &mv : arr) {
                        const QJsonObject o = mv.toObject();
                        inst[o.value("name").toString()] = qint64(o.value("size").toDouble());
                    }
                }
                r->deleteLater();
                if (!ok && !dl->running)
                    statusLabel->setText("Ollama ist nicht erreichbar - läuft es?");

                dl->installed.clear();
                for (auto it = inst.begin(); it != inst.end(); ++it) dl->installed.insert(it.key());

                // aufgeklappte Familien merken
                QSet<QString> open;
                for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                    QTreeWidgetItem *t = tree->topLevelItem(i);
                    if (t->isExpanded()) open.insert(t->data(0, Qt::UserRole + 1).toString());
                }

                QStringList order;
                QMap<QString, QList<ModelRow>> rows;
                QSet<QString> catTags;
                for (const CatFamily &f : catalog()) {
                    const QString fam = QString(f.name);
                    order << fam;
                    for (const CatModel &m : f.models) {
                        const QString tag = QString(m.tag);
                        catTags.insert(tag);
                        rows[fam].append({tag, QString(m.info), inst.contains(tag), inst.value(tag)});
                    }
                }
                QStringList extraFamilies;
                for (auto it = inst.begin(); it != inst.end(); ++it) {
                    if (catTags.contains(it.key())) continue;
                    const QString fam = familyOf(it.key());
                    if (!order.contains(fam) && !extraFamilies.contains(fam)) extraFamilies << fam;
                    rows[fam].append({it.key(), "installiert", true, it.value()});
                }
                extraFamilies.sort();
                order << extraFamilies;

                const QString keepTag = nameEdit->text().trimmed();
                tree->clear();
                for (const QString &fam : order) {
                    if (rows[fam].isEmpty()) continue;
                    auto *topItem = new QTreeWidgetItem(tree);
                    topItem->setData(0, Qt::UserRole + 1, fam);
                    QFont bf = topItem->font(0);
                    bf.setBold(true);
                    topItem->setFont(0, bf);
                    bool hasInstalled = false;
                    for (const ModelRow &row : rows[fam]) {
                        auto *child = new QTreeWidgetItem(topItem);
                        child->setText(0, row.tag);
                        child->setText(1, row.info);
                        child->setData(0, Qt::UserRole, row.tag);
                        if (row.installed) {
                            hasInstalled = true;
                            child->setText(2, "✓ installiert  (" + fmtBytes(double(row.size)) + ")");
                            child->setForeground(2, QColor("#98c379"));
                        }
                        child->setForeground(1, QColor("#a8a69e"));
                    }
                    topItem->setExpanded(dl->first ? hasInstalled : open.contains(fam));
                    famText(topItem);
                }
                dl->first = false;
                Q_UNUSED(keepTag);
                updateButtons();
            });
        };

        // Download starten
        auto startPull = [=, &reload](const QString &name) {
            if (dl->running || name.isEmpty()) return;
            dl->running = true;
            dl->err.clear();
            dl->success = false;
            dl->buf.clear();
            progress->setRange(0, 0);
            progress->show();
            cancelBtn->show();
            statusLabel->setText("Starte Download: " + name + " ...");
            updateButtons();

            QNetworkRequest req{QUrl(PULL_URL)};
            req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            const QJsonObject body{{"model", name}, {"stream", true}};
            QNetworkReply *r = net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
            dl->reply = r;

            QObject::connect(r, &QNetworkReply::readyRead, dp, [=]() {
                dl->buf += r->readAll();
                int nl;
                while ((nl = dl->buf.indexOf('\n')) != -1) {
                    const QByteArray line = dl->buf.left(nl);
                    dl->buf.remove(0, nl + 1);
                    const QJsonObject o = QJsonDocument::fromJson(line).object();
                    if (o.contains("error")) { dl->err = o.value("error").toString(); continue; }
                    const QString st = o.value("status").toString();
                    if (st == "success") dl->success = true;
                    const double total = o.value("total").toDouble();
                    const double done  = o.value("completed").toDouble();
                    if (total > 0) {
                        progress->setRange(0, 1000);
                        progress->setValue(int(done * 1000.0 / total));
                        statusLabel->setText(name + ":  " + fmtBytes(done) + " von " + fmtBytes(total) +
                                             "  (aktueller Teil)");
                    } else if (!st.isEmpty()) {
                        progress->setRange(0, 0);
                        statusLabel->setText(name + ":  " + st);
                    }
                }
            });

            QObject::connect(r, &QNetworkReply::finished, dp, [=, &reload]() {
                const bool cancelled = (r->error() == QNetworkReply::OperationCanceledError);
                const bool netErr    = (r->error() != QNetworkReply::NoError) && !cancelled;
                if (!dl->buf.trimmed().isEmpty()) {
                    const QJsonObject o = QJsonDocument::fromJson(dl->buf).object();
                    if (o.contains("error")) dl->err = o.value("error").toString();
                    dl->buf.clear();
                }
                r->deleteLater();
                dl->reply = nullptr;
                dl->running = false;
                progress->hide();
                cancelBtn->hide();
                if (cancelled)                 statusLabel->setText("Download abgebrochen.");
                else if (!dl->err.isEmpty())   statusLabel->setText("Fehler: " + dl->err);
                else if (netErr)               statusLabel->setText("Fehler: Ollama nicht erreichbar oder Verbindung abgebrochen.");
                else if (dl->success)          statusLabel->setText("✓ " + name + " ist installiert und einsatzbereit.");
                else                           statusLabel->setText("Download beendet.");
                updateButtons();
                if (!dl->closing) reload();
            });
        };

        // Eingaben / Knoepfe
        QObject::connect(nameEdit, &QLineEdit::textChanged, dp, [=](const QString &) { updateButtons(); });
        QObject::connect(nameEdit, &QLineEdit::returnPressed, dp, [=, &startPull]() {
            startPull(nameEdit->text().trimmed());
        });
        QObject::connect(pullBtn, &QPushButton::clicked, dp, [=, &startPull]() {
            startPull(nameEdit->text().trimmed());
        });
        QObject::connect(cancelBtn, &QPushButton::clicked, dp, [=]() {
            if (dl->reply) dl->reply->abort();
        });

        QObject::connect(tree, &QTreeWidget::itemSelectionChanged, dp, [=]() {
            QTreeWidgetItem *it = tree->currentItem();
            if (!it) return;
            const QString tag = it->data(0, Qt::UserRole).toString();
            if (!tag.isEmpty()) nameEdit->setText(tag);
        });
        QObject::connect(tree, &QTreeWidget::itemClicked, dp, [=](QTreeWidgetItem *it, int) {
            if (it->childCount() > 0) it->setExpanded(!it->isExpanded());
        });
        QObject::connect(tree, &QTreeWidget::itemExpanded,  dp, [=](QTreeWidgetItem *it) { famText(it); });
        QObject::connect(tree, &QTreeWidget::itemCollapsed, dp, [=](QTreeWidgetItem *it) { famText(it); });

        QObject::connect(delBtn, &QPushButton::clicked, dp, [=, &reload]() {
            const QString name = nameEdit->text().trimmed();
            if (dl->running || !dl->installed.contains(name)) return;
            const auto answer = QMessageBox::question(dp, "Modell löschen",
                    QString("Modell \"%1\" von der Festplatte löschen?").arg(name));
            if (answer != QMessageBox::Yes) return;

            QNetworkRequest req{QUrl(DELETE_URL)};
            req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            const QJsonObject body{{"model", name}, {"name", name}};
            QNetworkReply *r = net->sendCustomRequest(req, "DELETE",
                                                      QJsonDocument(body).toJson(QJsonDocument::Compact));
            QObject::connect(r, &QNetworkReply::finished, dp, [=, &reload]() {
                const bool ok = (r->error() == QNetworkReply::NoError);
                r->deleteLater();
                statusLabel->setText(ok ? "✓ " + name + " wurde gelöscht."
                                        : "Löschen fehlgeschlagen: " + name);
                reload();
            });
        });

        QObject::connect(closeBtn, &QPushButton::clicked, dp, &QDialog::accept);
        QObject::connect(dp, &QDialog::finished, dp, [=]() {
            dl->closing = true;
            if (dl->reply) dl->reply->abort();     // laufenden Download beim Schliessen stoppen
        });

        updateButtons();
        reload();
        dlg.exec();
    };

    // =====================  Einstellungen  =====================
    // Wechselt den Speicherordner; fragt, ob vorhandene Chats mitkommen sollen
    auto applyChatDir = [=](const QString &newDirIn, QWidget *parent) -> bool {
        const QString newDir = QDir::cleanPath(newDirIn);
        const QString oldDir = QDir::cleanPath(chatDir());
        if (newDir == oldDir) return false;
        QDir().mkpath(newDir);

        const QStringList oldFiles = QDir(oldDir).entryList(QStringList("*.json"), QDir::Files);
        if (!oldFiles.isEmpty()) {
            const auto answer = QMessageBox::question(parent, "Chats verschieben",
                "Sollen die vorhandenen Chats und Projektmappen in den neuen Ordner verschoben werden?");
            if (answer == QMessageBox::Yes) {
                for (const QString &f : oldFiles) {
                    const QString src = oldDir + "/" + f;
                    const QString dst = newDir + "/" + f;
                    if (QFile::exists(dst)) continue;
                    if (!QFile::rename(src, dst)) {            // z. B. anderes Laufwerk
                        if (QFile::copy(src, dst)) QFile::remove(src);
                    }
                }
            }
        }
        const bool isDefault = (newDir == QDir::cleanPath(defaultChatDir()));
        saveChatDirSetting(isDefault ? QString() : newDir);
        return true;
    };

    auto showSettings = [=]() {
        QDialog dlg(win);
        QDialog *dp = &dlg;
        dlg.setWindowTitle("Einstellungen");
        dlg.setMinimumWidth(600);

        auto *v = new QVBoxLayout(&dlg);
        v->setContentsMargins(26, 24, 26, 22);
        v->setSpacing(8);

        auto *l1 = new QLabel("Speicherort der Chats");
        l1->setObjectName("dlgLabel");
        auto *pathEdit = new QLineEdit(QDir::toNativeSeparators(chatDir()));
        pathEdit->setReadOnly(true);
        auto *changeBtn = new QPushButton("Ändern ...");
        changeBtn->setObjectName("ghostBtn");
        auto *resetBtn = new QPushButton("Standard");
        resetBtn->setObjectName("ghostBtn");
        auto *pathRow = new QHBoxLayout;
        pathRow->setSpacing(8);
        pathRow->addWidget(pathEdit, 1);
        pathRow->addWidget(changeBtn);
        pathRow->addWidget(resetBtn);

        auto *hint = new QLabel("Hier werden alle Chats und Projektmappen gespeichert. "
                                "Beim Wechsel kannst du das Vorhandene mitnehmen.");
        hint->setObjectName("dlgLabel");
        hint->setWordWrap(true);

        auto *closeRow = new QHBoxLayout;
        closeRow->addStretch(1);
        auto *closeBtn = new QPushButton("Schließen");
        closeRow->addWidget(closeBtn);

        v->addWidget(l1);
        v->addLayout(pathRow);
        v->addWidget(hint);
        v->addSpacing(14);
        v->addLayout(closeRow);

        QObject::connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        QObject::connect(changeBtn, &QPushButton::clicked, [=]() {
            const QString dir = QFileDialog::getExistingDirectory(dp, "Ordner für die Chats wählen", chatDir());
            if (dir.isEmpty()) return;
            if (applyChatDir(dir, dp)) {
                pathEdit->setText(QDir::toNativeSeparators(chatDir()));
                ops->refreshHome();
            }
        });
        QObject::connect(resetBtn, &QPushButton::clicked, [=]() {
            if (applyChatDir(defaultChatDir(), dp)) {
                pathEdit->setText(QDir::toNativeSeparators(chatDir()));
                ops->refreshHome();
            }
        });
        dlg.exec();
    };

    // =====================  Nachricht senden  =====================
    auto sendMessage = [=]() {
        const QString text = input->text().trimmed();
        if ((text.isEmpty() && state->attachments.empty()) || state->busy) return;

        // Anhaenge verarbeiten
        QString extra;
        QStringList fileNames, images;
        int budget = MAX_ATTACH_CHARS;
        for (const Attachment &a : state->attachments) {
            if (a.kind == KImage) {
                const QString b64 = imageToBase64(a.path);
                if (b64.isEmpty()) {
                    QMessageBox::warning(win, "Anhang", "Das Bild konnte nicht gelesen werden: " + a.name);
                    return;
                }
                images << b64;
                continue;
            }
            QString content;
            if (a.kind == KPdf) {
                bool ok = false;
                content = pdfText(a.path, ok);
                if (!ok) {
                    QMessageBox::warning(win, "Anhang", "Die PDF-Datei konnte nicht gelesen werden: " + a.name);
                    return;
                }
                if (content.isEmpty()) content = "[In dieser PDF wurde kein Text gefunden - vielleicht ein Scan.]";
            } else {
                content = readTextFile(a.path);
            }
            if (content.size() > budget) content = content.left(qMax(0, budget)) + "\n[... gekürzt ...]";
            budget = qMax(0, budget - content.size());
            fileNames << a.name;
            extra += "\n\n[Anhang: " + a.name + "]\n" + content + "\n[Ende Anhang]";
        }

        QString apiText = text;
        if (apiText.isEmpty())
            apiText = images.isEmpty() ? "Bitte sieh dir den Anhang an und fasse ihn kurz zusammen."
                                       : "Bitte beschreibe, was du auf dem Bild siehst.";
        apiText += extra;

        input->clear();
        state->attachments.clear();
        ops->refreshChips();

        state->busy = true;
        send->setEnabled(false);
        menuBtn->setEnabled(false);
        chatGear->setEnabled(false);
        state->reply.clear();
        state->buffer.clear();
        state->errorText.clear();

        QJsonObject um{{"role", "user"}, {"content", apiText}, {"display", text}};
        if (!fileNames.isEmpty()) um["files"]  = QJsonArray::fromStringList(fileNames);
        if (!images.isEmpty())    um["images"] = QJsonArray::fromStringList(images);
        state->chat.messages.append(um);
        const bool hadImages = !images.isEmpty();

        addUser(text, fileNames, images);
        state->aiMsg = addAi();

        // Nur die Felder schicken, die Ollama kennt
        QJsonArray apiMessages;
        for (const QJsonValue &mv : state->chat.messages) {
            const QJsonObject m = mv.toObject();
            QJsonObject o{{"role", m.value("role")}, {"content", m.value("content")}};
            if (!m.value("images").toArray().isEmpty()) o["images"] = m.value("images");
            apiMessages.append(o);
        }

        QNetworkRequest req{QUrl(CHAT_URL)};
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setTransferTimeout(300000);   // Modell laden kann dauern

        QJsonObject body{{"model", state->chat.model}, {"stream", true}, {"messages", apiMessages},
                         {"options", QJsonObject{{"num_ctx", NUM_CTX}}}};
        QNetworkReply *reply = net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));

        QObject::connect(reply, &QNetworkReply::readyRead, [=]() {
            state->buffer += reply->readAll();
            int nl;
            bool changed = false;
            while ((nl = state->buffer.indexOf('\n')) != -1) {
                QByteArray line = state->buffer.left(nl);
                state->buffer.remove(0, nl + 1);
                const QJsonObject o = QJsonDocument::fromJson(line).object();
                if (o.contains("error")) state->errorText = o.value("error").toString();
                QString piece = o.value("message").toObject().value("content").toString();
                if (piece.isEmpty()) continue;
                state->reply += piece;
                changed = true;
            }
            if (changed) updateMessage(*state->aiMsg, state->reply);
        });

        QObject::connect(reply, &QNetworkReply::finished, [=]() {
            // Reste im Puffer (letzte Zeile ohne Zeilenumbruch, z. B. Fehlertext)
            if (!state->buffer.trimmed().isEmpty()) {
                const QJsonObject o = QJsonDocument::fromJson(state->buffer).object();
                if (o.contains("error")) state->errorText = o.value("error").toString();
                state->buffer.clear();
            }
            if (state->reply.isEmpty() && (reply->error() != QNetworkReply::NoError || !state->errorText.isEmpty())) {
                QString msg;
                if (!state->errorText.isEmpty()) {
                    msg = "*[Fehler von Ollama: " + state->errorText + "]*";
                    if (hadImages) msg += "\n\n*Unterstützt dieses Modell Bilder? (z. B. llava, gemma3, llama3.2-vision)*";
                } else {
                    msg = "*[Fehler: Ollama nicht erreichbar - laeuft es?]*";
                }
                updateMessage(*state->aiMsg, msg);
                state->chat.messages.removeLast();   // Frage wieder entfernen
            } else {
                state->chat.messages.append(QJsonObject{{"role", "assistant"}, {"content", state->reply}});
            }
            saveChatFile(state->chat);               // ins Archiv speichern
            state->busy = false;
            send->setEnabled(true);
            menuBtn->setEnabled(true);
            chatGear->setEnabled(true);
            input->setFocus();
            reply->deleteLater();
        });
    };

    // =====================  Verbindungen  =====================
    QObject::connect(send,  &QPushButton::clicked,     sendMessage);
    QObject::connect(input, &QLineEdit::returnPressed, sendMessage);

    QObject::connect(homeNew,        &QPushButton::clicked, [=]() { newChatFlow(QString()); });
    QObject::connect(homeNewProject, &QPushButton::clicked, createProject);
    QObject::connect(projNew,        &QPushButton::clicked, [=]() { newChatFlow(state->currentProject); });
    QObject::connect(projBack,       &QPushButton::clicked, [=]() { ops->showHome(); });
    QObject::connect(projGear,       &QPushButton::clicked, [=]() {
        for (const Project &p : loadProjects())
            if (p.id == state->currentProject)
                ops->showProjectMenu(p, projGear, [=]() { ops->refreshProject(); });
    });
    QObject::connect(chatGear, &QPushButton::clicked, [=]() {
        if (state->busy) return;
        ops->showChatMenu(state->chat, chatGear, [=]() { updateChatHeader(); });
    });

    QObject::connect(menuBtn, &QPushButton::clicked, [=]() {
        mainMenu->exec(menuBtn->mapToGlobal(QPoint(0, menuBtn->height() + 4)));
    });
    QObject::connect(mainMenu, &QMenu::aboutToShow, [=]() {
        actProject->setEnabled(stack->currentIndex() == 1 && !state->chat.projectId.isEmpty());
    });
    QObject::connect(actNew,      &QAction::triggered, [=]() { newChatFlow(QString()); });
    QObject::connect(actHome,     &QAction::triggered, [=]() { if (!state->busy) ops->showHome(); });
    QObject::connect(actProject,  &QAction::triggered, [=]() {
        if (!state->busy && !state->chat.projectId.isEmpty()) ops->showProject(state->chat.projectId);
    });
    QObject::connect(actModels,   &QAction::triggered, [=]() { if (!state->busy) showModelManager(); });
    QObject::connect(actSettings, &QAction::triggered, showSettings);
    QObject::connect(actFolder,   &QAction::triggered, [=]() {
        QDir().mkpath(chatDir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(chatDir()));
    });

    ops->showHome();
    window.show();
    return app.exec();
}
