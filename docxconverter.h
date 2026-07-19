#ifndef DOCXCONVERTER_H
#define DOCXCONVERTER_H

#include <QString>
#include <QByteArray>
#include <QHash>

class QTextDocument;

// Minimal .docx import/export scoped to MattWord's feature set:
// paragraphs, bold / italic / underline character formatting, and
// inline images. Anything else in an incoming .docx (tables, lists,
// footnotes, headers, ...) is skipped; its plain text is kept where
// that makes sense.
//
// The native format of MattWord remains HTML — .docx is strictly an
// import/export representation.
namespace DocxConverter {

// Export `doc` to a .docx file at `filePath`.
// Returns true on success; on failure returns false and sets *errorOut.
bool exportDocx(const QTextDocument *doc, const QString &filePath,
                QString *errorOut = nullptr);

// Import the .docx at `filePath`.
// On success returns true, fills `htmlOut` with HTML using
// src="myimage/..." references for images, and fills `imagesOut`
// with resourceName -> encoded image bytes to register on the
// document after setHtml().
bool importDocx(const QString &filePath, QString &htmlOut,
                QHash<QString, QByteArray> &imagesOut,
                QString *errorOut = nullptr);

} // namespace DocxConverter

#endif // DOCXCONVERTER_H
