#include "sourceview.h"

#include "parts.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QLabel>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextLayout>
#include <QVBoxLayout>

namespace {

constexpr int kind = QTextFormat::UserProperty;

// Rhai, as recipes are written in it. The colours are the desktop palette's own: a link colour
// for what the language means, the disabled text colour for comments.
class RhaiHighlighter : public QSyntaxHighlighter {
public:
    explicit RhaiHighlighter(QTextDocument *document, const QPalette &palette)
        : QSyntaxHighlighter(document)
    {
        auto make = [](const QColor &colour, const char *name, bool bold, bool italic) {
            QTextCharFormat format;
            format.setForeground(colour);
            format.setFontWeight(bold ? QFont::Bold : QFont::Normal);
            format.setFontItalic(italic);
            format.setProperty(kind, QString::fromLatin1(name));
            return format;
        };
        keyword_ = make(palette.color(QPalette::Link), "keyword", true, false);
        string_ = make(palette.color(QPalette::LinkVisited), "string", false, false);
        comment_ = make(palette.color(QPalette::Disabled, QPalette::Text), "comment", false, true);
        number_ = make(palette.color(QPalette::LinkVisited), "number", false, false);
        call_ = make(palette.color(QPalette::Text), "call", true, false);
    }

protected:
    // State 1: inside a block comment. State 2: inside a back-quoted string.
    void highlightBlock(const QString &text) override
    {
        static const QRegularExpression words(
            QStringLiteral("\\b(let|const|fn|if|else|while|for|in|loop|return|break|continue|switch|import|export|as|"
                           "private|true|false|this|throw|try|catch|do|until|global)\\b"));
        static const QRegularExpression number(QStringLiteral("\\b(?:0x[0-9a-fA-F_]+|0b[01_]+|\\d[\\d_]*(?:\\.\\d+)?(?:[eE][+-]?\\d+)?)\\b"));
        static const QRegularExpression call(QStringLiteral("\\b([A-Za-z_][A-Za-z0-9_]*)\\s*\\("));
        setCurrentBlockState(0);
        // What the plain patterns colour first; strings and comments paint over it.
        for (auto it = words.globalMatch(text); it.hasNext();) {
            const auto match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), keyword_);
        }
        for (auto it = number.globalMatch(text); it.hasNext();) {
            const auto match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), number_);
        }
        for (auto it = call.globalMatch(text); it.hasNext();) {
            const auto match = it.next();
            if (format(match.capturedStart(1)).property(kind).toString() != QLatin1String("keyword"))
                setFormat(match.capturedStart(1), match.capturedLength(1), call_);
        }
        // Strings and comments, read left to right.
        int at = 0;
        const int size = text.size();
        int state = previousBlockState();
        auto cleared = [&](int from, int length) {
            QTextCharFormat none;
            setFormat(from, length, none);
        };
        while (at < size) {
            if (state == 1) {
                const int end = text.indexOf(QStringLiteral("*/"), at);
                const int stop = end < 0 ? size : end + 2;
                cleared(at, stop - at);
                setFormat(at, stop - at, comment_);
                at = stop;
                state = end < 0 ? 1 : 0;
                continue;
            }
            if (state == 2) {
                const int end = text.indexOf(QLatin1Char('`'), at);
                const int stop = end < 0 ? size : end + 1;
                cleared(at, stop - at);
                setFormat(at, stop - at, string_);
                at = stop;
                state = end < 0 ? 2 : 0;
                continue;
            }
            const QChar c = text.at(at);
            if (c == QLatin1Char('/') && at + 1 < size && text.at(at + 1) == QLatin1Char('/')) {
                cleared(at, size - at);
                setFormat(at, size - at, comment_);
                at = size;
            } else if (c == QLatin1Char('/') && at + 1 < size && text.at(at + 1) == QLatin1Char('*')) {
                state = 1;
                // Painted by the state above, from the opening.
                const int end = text.indexOf(QStringLiteral("*/"), at + 2);
                const int stop = end < 0 ? size : end + 2;
                cleared(at, stop - at);
                setFormat(at, stop - at, comment_);
                at = stop;
                state = end < 0 ? 1 : 0;
            } else if (c == QLatin1Char('`')) {
                const int end = text.indexOf(QLatin1Char('`'), at + 1);
                const int stop = end < 0 ? size : end + 1;
                cleared(at, stop - at);
                setFormat(at, stop - at, string_);
                at = stop;
                state = end < 0 ? 2 : 0;
            } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                int stop = at + 1;
                while (stop < size && text.at(stop) != c) {
                    if (text.at(stop) == QLatin1Char('\\'))
                        ++stop;
                    ++stop;
                }
                stop = qMin(size, stop + 1);
                cleared(at, stop - at);
                setFormat(at, stop - at, string_);
                at = stop;
            } else {
                ++at;
            }
        }
        setCurrentBlockState(state);
    }

private:
    QTextCharFormat keyword_, string_, comment_, number_, call_;
};

} // namespace

SourceViewer::SourceViewer(const QString &title, const QString &from, const QString &path, const QString &problem,
                           QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("The recipe %1").arg(title));
    resize(860, 640);
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(20, 18, 20, 14);
    column->setSpacing(8);
    column->addWidget(heading(title));
    from_ = dim(text(from.isEmpty() ? QString() : tr("From %1").arg(from), true));
    from_->setVisible(!from.isEmpty());
    column->addWidget(from_);
    path_ = new QLabel(path);
    path_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    path_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    path_->setVisible(!path.isEmpty());
    column->addWidget(path_);
    column->addWidget(dim(text(tr("This is the file Recipe runs for it. You can read it here; it cannot be changed from this window."), true)));

    QString content, why = problem;
    if (path.isEmpty() && why.isEmpty())
        why = tr("Recipe did not say where this recipe's file is.");
    if (!path.isEmpty()) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
            content = QString::fromUtf8(file.readAll());
        else
            why = tr("The file could not be read: %1").arg(file.errorString());
    }
    problem_ = text(why, true);
    problem_->setVisible(!why.isEmpty());
    column->addWidget(problem_);

    code_ = new QPlainTextEdit;
    code_->setReadOnly(true);
    code_->setLineWrapMode(QPlainTextEdit::NoWrap);
    code_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    code_->setPlainText(content);
    code_->setVisible(why.isEmpty() || !content.isEmpty());
    new RhaiHighlighter(code_->document(), code_->palette());
    column->addWidget(code_, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);
}

QString SourceViewer::code() const
{
    return code_->toPlainText();
}

QString SourceViewer::pathWords() const
{
    return path_->text();
}

QString SourceViewer::fromWords() const
{
    return from_->isVisibleTo(this) ? from_->text() : QString();
}

QString SourceViewer::problemWords() const
{
    return problem_->isVisibleTo(this) ? problem_->text() : QString();
}

bool SourceViewer::readOnly() const
{
    return code_->isReadOnly();
}

QStringList SourceViewer::highlighted(const QString &wanted) const
{
    QStringList found;
    for (QTextBlock block = code_->document()->begin(); block.isValid(); block = block.next())
        for (const QTextLayout::FormatRange &range : block.layout()->formats())
            if (range.format.property(kind).toString() == wanted)
                found << block.text().mid(range.start, range.length);
    return found;
}
