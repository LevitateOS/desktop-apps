// The read-only viewer of a recipe's file: the .rhai Recipe will really run for an entry,
// with Rhai syntax highlighting, and where it comes from. Inspection only: there is no
// editing here and nothing is run.
#pragma once

#include <QDialog>
#include <QStringList>

class QLabel;
class QPlainTextEdit;

class SourceViewer : public QDialog {
public:
    // `from`: the source or folder it is from, in words (may be empty). `path`: the file, or
    // empty when Recipe did not say; then `problem` is why, and is shown instead of the code.
    SourceViewer(const QString &title, const QString &from, const QString &path, const QString &problem,
                 QWidget *parent = nullptr);

    // For a check.
    QString code() const;
    QString pathWords() const;
    QString fromWords() const;
    QString problemWords() const;
    bool readOnly() const;
    // The pieces of the code that carry a kind of highlighting: "keyword", "string", "comment",
    // "number" or "call".
    QStringList highlighted(const QString &kind) const;

private:
    QPlainTextEdit *code_;
    QLabel *path_, *from_, *problem_;
};
