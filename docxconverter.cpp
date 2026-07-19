#include "docxconverter.h"

#include <QTextDocument>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QXmlStreamReader>
#include <QImage>
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QVariant>
#include <QUrl>
#include <QDebug>
#include <QRegularExpression>

#include "miniz.h"

namespace {

// Image size mathematics.
//
// PNG stores physical density in integer dots-per-meter; Qt writes 3780
// (the nearest integer to 96 dpi — exact 96 dpi is not representable).
// LibreOffice derives an image's "natural size" from that pHYs value, and
// only uses its fast 1:1 screen blit when the declared extent equals the
// natural size. If we declared extents at exactly 96 dpi (9525 EMU/px)
// they would differ from the 3780 dpm natural size by ~0.0125%, pushing
// Writer onto its banded scaling paint path (visible seam lines on
// screen). So both the PNGs we write and the extents we declare use the
// same 3780 dpm basis.
constexpr double DOTS_PER_METER = 3780.0;    // what Qt writes as pHYs
constexpr double EMU_PER_METER  = 36000000.0; // exact: 914400 EMU/in ÷ 0.0254

inline qint64 pxToEmu(int px) {
    return qRound64(px * EMU_PER_METER / DOTS_PER_METER);
}
inline int emuToPx(qint64 emu) {
    return static_cast<int>(qRound(emu * DOTS_PER_METER / EMU_PER_METER));
}

QString xmlEscape(const QString &s) {
    QString r = s;
    r.replace('&', "&amp;");
    r.replace('<', "&lt;");
    r.replace('>', "&gt;");
    r.replace('"', "&quot;");
    return r;
}

// ─── Static docx parts ─────────────────────────────────────────────────────

QByteArray contentTypesXml() {
    return QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/>"
        "<Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>"
        "<Default Extension=\"jpg\" ContentType=\"image/jpeg\"/>"
        "<Override PartName=\"/word/document.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "<Override PartName=\"/word/settings.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/>"
        "</Types>");
}

QByteArray rootRelsXml() {
    return QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
        "Target=\"word/document.xml\"/>"
        "</Relationships>");
}

// Declares compatibilityMode 15 (Word 2013+). Without a settings part,
// LibreOffice (and old Word versions) fall back to legacy layout
// compatibility, one symptom of which is tall inline images being sliced
// into text-line-height strips with visible seams.
QByteArray settingsXml() {
    return QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:settings xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:compat>"
        "<w:compatSetting w:name=\"compatibilityMode\" "
        "w:uri=\"http://schemas.microsoft.com/office/word\" w:val=\"15\"/>"
        "</w:compat>"
        "</w:settings>");
}

// ─── Zip helpers (miniz) ───────────────────────────────────────────────────

bool zipAdd(mz_zip_archive *zip, const char *name, const QByteArray &data) {
    return mz_zip_writer_add_mem(zip, name, data.constData(),
                                 static_cast<size_t>(data.size()),
                                 MZ_DEFAULT_COMPRESSION) != MZ_FALSE;
}

QByteArray zipRead(mz_zip_archive *zip, const QString &name, bool *found) {
    if (found) *found = false;
    int idx = mz_zip_reader_locate_file(zip, name.toUtf8().constData(),
                                        nullptr, 0);
    if (idx < 0) return {};
    size_t size = 0;
    void *p = mz_zip_reader_extract_to_heap(zip, static_cast<mz_uint>(idx),
                                            &size, 0);
    if (!p) return {};
    QByteArray out(static_cast<const char *>(p), static_cast<int>(size));
    mz_free(p);
    if (found) *found = true;
    return out;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// Export
// ═══════════════════════════════════════════════════════════════════════════

bool DocxConverter::exportDocx(const QTextDocument *doc,
                               const QString &filePath, QString *errorOut) {
    if (!doc) {
        if (errorOut) *errorOut = QStringLiteral("No document to export.");
        return false;
    }

    // Collected while walking the document; written into the zip afterwards
    struct MediaEntry {
        QString relId;       // "rId1", ...
        QString mediaName;   // "media/image1.png"
        QByteArray bytes;    // encoded image data
    };
    QList<MediaEntry> media;
    int nextRel = 1;

    QString body;

    for (QTextBlock block = doc->begin(); block.isValid();
         block = block.next()) {
        // Explicit spacing properties: without these (and with no styles.xml
        // in our minimal package) Word applies its built-in Normal style —
        // 8pt space-after and 1.08 line spacing — visually inflating the
        // gaps between paragraphs. MattWord's model is "a blank line is its
        // own paragraph", so spacing must be zero and line rule single.
        body += "<w:p><w:pPr>"
                "<w:spacing w:before=\"0\" w:after=\"0\" "
                "w:line=\"240\" w:lineRule=\"auto\"/>"
                "</w:pPr>";

        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            QTextFragment frag = it.fragment();
            if (!frag.isValid()) continue;

            QTextCharFormat cf = frag.charFormat();

            if (cf.isImageFormat()) {
                // ── Inline image run ─────────────────────────────────────
                QTextImageFormat imgFmt = cf.toImageFormat();
                QString resName = imgFmt.name();

                QVariant res = doc->resource(QTextDocument::ImageResource,
                                             QUrl(resName));
                QByteArray bytes;
                QImage img;
                if (res.typeId() == QMetaType::QByteArray) {
                    bytes = res.toByteArray();
                    img.loadFromData(bytes);
                } else if (res.canConvert<QImage>()) {
                    img = res.value<QImage>();
                    QBuffer buf(&bytes);
                    buf.open(QIODevice::WriteOnly);
                    img.save(&buf, "PNG");
                }
                if (bytes.isEmpty() || img.isNull()) {
                    qDebug() << "exportDocx: skipping unresolvable image"
                             << resName;
                    continue;
                }

                // Displayed size: honour the format's width; keep aspect.
                // Sizes are forced to whole pixels and the bitmap is
                // resampled to exactly the displayed size, so declared
                // extent == intrinsic size == natural size and consumers
                // can blit 1:1 (avoids seam artifacts in LO's scaler).
                const int wPx = imgFmt.width() > 0
                                    ? qRound(imgFmt.width())
                                    : img.width();
                const int hPx = imgFmt.height() > 0
                                    ? qRound(imgFmt.height())
                                    : (img.width() > 0
                                           ? qRound(qreal(wPx) *
                                                    img.height() /
                                                    img.width())
                                           : wPx);

                if (img.width() != wPx || img.height() != hPx) {
                    img = img.scaled(wPx, hPx, Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation);
                }
                // Deterministic pHYs: 3780 dpm (~96 dpi), matching the
                // basis of our EMU extents. Canvas/pasted images can
                // otherwise carry screen DPI, which makes Writer rescale.
                img.setDotsPerMeterX(qRound(DOTS_PER_METER));
                img.setDotsPerMeterY(qRound(DOTS_PER_METER));
                {
                    bytes.clear();
                    QBuffer buf(&bytes);
                    buf.open(QIODevice::WriteOnly);
                    img.save(&buf, "PNG");
                }

                const qint64 cx = pxToEmu(wPx);
                const qint64 cy = pxToEmu(hPx);

                MediaEntry m;
                m.relId = QStringLiteral("rId%1").arg(nextRel);
                m.mediaName =
                    QStringLiteral("media/image%1.png").arg(nextRel);
                m.bytes = bytes;
                media.append(m);
                const int imgId = nextRel;
                ++nextRel;

                body += QStringLiteral(
                    "<w:r><w:drawing>"
                    "<wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"
                    "<wp:extent cx=\"%1\" cy=\"%2\"/>"
                    "<wp:docPr id=\"%3\" name=\"Picture %3\"/>"
                    "<a:graphic xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">"
                    "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
                    "<pic:pic xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
                    "<pic:nvPicPr><pic:cNvPr id=\"%3\" name=\"Picture %3\"/><pic:cNvPicPr/></pic:nvPicPr>"
                    "<pic:blipFill><a:blip r:embed=\"%4\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
                    "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%1\" cy=\"%2\"/></a:xfrm>"
                    "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr>"
                    "</pic:pic></a:graphicData></a:graphic>"
                    "</wp:inline></w:drawing></w:r>")
                        .arg(cx)
                        .arg(cy)
                        .arg(imgId)
                        .arg(m.relId);
                continue;
            }

            // ── Text run ────────────────────────────────────────────────
            QString text = frag.text();
            // QTextDocument represents soft line breaks (Shift+Enter) as
            // U+2028 inside a fragment; map them to <w:br/>
            const QStringList parts =
                text.split(QChar(QChar::LineSeparator));

            QString rpr;
            if (cf.fontWeight() >= QFont::Bold) rpr += "<w:b/>";
            if (cf.fontItalic()) rpr += "<w:i/>";
            if (cf.fontUnderline() ||
                cf.underlineStyle() == QTextCharFormat::SingleUnderline)
                rpr += "<w:u w:val=\"single\"/>";
            const QString rprBlock =
                rpr.isEmpty() ? QString()
                              : QStringLiteral("<w:rPr>%1</w:rPr>").arg(rpr);

            for (int p = 0; p < parts.size(); ++p) {
                if (p > 0)
                    body += QStringLiteral("<w:r>%1<w:br/></w:r>")
                                .arg(rprBlock);
                if (parts[p].isEmpty()) continue;
                body += QStringLiteral(
                            "<w:r>%1<w:t xml:space=\"preserve\">%2</w:t></w:r>")
                            .arg(rprBlock, xmlEscape(parts[p]));
            }
        }

        body += "</w:p>";
    }

    QByteArray documentXml =
        QByteArrayLiteral(
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            "<w:document "
            "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
            "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\">"
            "<w:body>") +
        body.toUtf8() +
        QByteArrayLiteral("<w:sectPr/></w:body></w:document>");

    // word/_rels/document.xml.rels — image relationships
    QString rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        // Fixed non-numeric id so it can never collide with image rIds
        "<Relationship Id=\"rIdSettings\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings\" "
        "Target=\"settings.xml\"/>";
    for (const MediaEntry &m : media) {
        rels += QStringLiteral(
                    "<Relationship Id=\"%1\" "
                    "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" "
                    "Target=\"%2\"/>")
                    .arg(m.relId, m.mediaName);
    }
    rels += "</Relationships>";

    // ── Write the zip ──────────────────────────────────────────────────────
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    const QByteArray pathUtf8 = QFile::encodeName(filePath);
    if (!mz_zip_writer_init_file(&zip, pathUtf8.constData(), 0)) {
        if (errorOut)
            *errorOut =
                QStringLiteral("Cannot create file %1").arg(filePath);
        return false;
    }

    bool ok = zipAdd(&zip, "[Content_Types].xml", contentTypesXml()) &&
              zipAdd(&zip, "_rels/.rels", rootRelsXml()) &&
              zipAdd(&zip, "word/document.xml", documentXml) &&
              zipAdd(&zip, "word/settings.xml", settingsXml()) &&
              zipAdd(&zip, "word/_rels/document.xml.rels", rels.toUtf8());
    for (const MediaEntry &m : media) {
        if (!ok) break;
        ok = zipAdd(&zip, ("word/" + m.mediaName).toUtf8().constData(),
                    m.bytes);
    }

    ok = mz_zip_writer_finalize_archive(&zip) && ok;
    mz_zip_writer_end(&zip);

    if (!ok && errorOut)
        *errorOut = QStringLiteral("Failed writing docx archive.");
    return ok;
}

// ═══════════════════════════════════════════════════════════════════════════
// Import
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// Parse word/_rels/document.xml.rels → relId -> target ("media/image1.png")
QHash<QString, QString> parseRels(const QByteArray &xml) {
    QHash<QString, QString> map;
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement() &&
            r.name() == QLatin1String("Relationship")) {
            map.insert(r.attributes().value("Id").toString(),
                       r.attributes().value("Target").toString());
        }
    }
    return map;
}

struct RunProps {
    bool bold = false;
    bool italic = false;
    bool underline = false;
};

// OOXML boolean properties: <w:b/> means on; w:val="false"/"0"/"none" = off
bool ooxmlFlagOn(QXmlStreamReader &r) {
    const auto val = r.attributes().value("w:val");
    if (val.isEmpty()) return true;
    return !(val == QLatin1String("false") || val == QLatin1String("0") ||
             val == QLatin1String("none"));
}

} // anonymous namespace

bool DocxConverter::importDocx(const QString &filePath, QString &htmlOut,
                               QHash<QString, QByteArray> &imagesOut,
                               QString *errorOut) {
    htmlOut.clear();
    imagesOut.clear();

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    const QByteArray pathUtf8 = QFile::encodeName(filePath);
    if (!mz_zip_reader_init_file(&zip, pathUtf8.constData(), 0)) {
        if (errorOut)
            *errorOut = QStringLiteral(
                            "%1 is not a valid .docx (zip) file.")
                            .arg(QFileInfo(filePath).fileName());
        return false;
    }

    bool found = false;
    const QByteArray documentXml =
        zipRead(&zip, QStringLiteral("word/document.xml"), &found);
    if (!found) {
        mz_zip_reader_end(&zip);
        if (errorOut)
            *errorOut = QStringLiteral(
                "No word/document.xml inside the file — not a .docx?");
        return false;
    }

    bool relsFound = false;
    const QHash<QString, QString> rels = parseRels(zipRead(
        &zip, QStringLiteral("word/_rels/document.xml.rels"), &relsFound));

    // ── Walk document.xml ─────────────────────────────────────────────────
    QString html;
    html += "<html><body>";

    QXmlStreamReader r(documentXml);
    int imgCounter = 0;

    QString para;            // accumulated HTML for current paragraph
    bool paraHasContent = false;
    RunProps props;          // formatting of the run being parsed
    bool inRun = false;
    qint64 pendingExtentCx = 0; // wp:extent precedes a:blip in OOXML; hold
                                // the display width until the image arrives

    auto openTags = [](const RunProps &p) {
        QString s;
        if (p.bold) s += "<b>";
        if (p.italic) s += "<i>";
        if (p.underline) s += "<u>";
        return s;
    };
    auto closeTags = [](const RunProps &p) {
        QString s;
        if (p.underline) s += "</u>";
        if (p.italic) s += "</i>";
        if (p.bold) s += "</b>";
        return s;
    };

    while (!r.atEnd()) {
        r.readNext();

        if (r.isStartElement()) {
            const auto name = r.name();

            if (name == QLatin1String("p")) {
                para.clear();
                paraHasContent = false;
            } else if (name == QLatin1String("r")) {
                inRun = true;
                props = RunProps();
            } else if (inRun && name == QLatin1String("b")) {
                props.bold = ooxmlFlagOn(r);
            } else if (inRun && name == QLatin1String("i")) {
                props.italic = ooxmlFlagOn(r);
            } else if (inRun && name == QLatin1String("u")) {
                const auto v = r.attributes().value("w:val");
                props.underline = !(v == QLatin1String("none"));
            } else if (name == QLatin1String("t")) {
                const QString text = r.readElementText();
                if (!text.isEmpty()) {
                    para += openTags(props) + xmlEscape(text) +
                            closeTags(props);
                    paraHasContent = true;
                }
            } else if (name == QLatin1String("br") ||
                       name == QLatin1String("cr")) {
                para += "<br/>";
                paraHasContent = true;
            } else if (name == QLatin1String("tab")) {
                // Approximate a tab; QTextEdit HTML has no real tab stop
                para += "&nbsp;&nbsp;&nbsp;&nbsp;";
                paraHasContent = true;
            } else if (name == QLatin1String("extent")) {
                // wp:extent (displayed size in EMU) precedes a:blip inside
                // w:drawing — remember it for the upcoming image
                bool okCx = false;
                const qint64 cx =
                    r.attributes().value("cx").toLongLong(&okCx);
                if (okCx && cx > 0) pendingExtentCx = cx;
            } else if (name == QLatin1String("blip")) {
                // <a:blip r:embed="rIdN"/> inside w:drawing (or w:pict)
                const QString relId =
                    r.attributes().value("r:embed").toString();
                const QString target = rels.value(relId);
                if (!target.isEmpty()) {
                    bool mediaFound = false;
                    QByteArray bytes = zipRead(
                        &zip, QStringLiteral("word/") + target,
                        &mediaFound);
                    if (!mediaFound) // some producers use absolute-ish paths
                        bytes = zipRead(&zip, target, &mediaFound);
                    if (mediaFound && !bytes.isEmpty()) {
                        const QString resName =
                            QStringLiteral("myimage/docximg_%1.png")
                                .arg(imgCounter++);
                        // Re-encode to PNG so downstream save-as-HTML
                        // (which labels everything image/png) stays honest
                        QImage img;
                        img.loadFromData(bytes);
                        if (!img.isNull()) {
                            QByteArray png;
                            QBuffer buf(&png);
                            buf.open(QIODevice::WriteOnly);
                            img.save(&buf, "PNG");
                            imagesOut.insert(resName, png);

                            const int widthPx =
                                pendingExtentCx > 0
                                    ? emuToPx(pendingExtentCx)
                                    : img.width();
                            para += QStringLiteral(
                                        "<img src=\"%1\" width=\"%2\"/>")
                                        .arg(resName)
                                        .arg(widthPx);
                            paraHasContent = true;
                        }
                    }
                }
                pendingExtentCx = 0; // consumed
            }
        } else if (r.isEndElement()) {
            const auto name = r.name();
            if (name == QLatin1String("r")) {
                inRun = false;
            } else if (name == QLatin1String("p")) {
                // margin:0 — Qt's setHtml() otherwise gives every <p> a
                // default 12px top+bottom margin (uncollapsed), which shows
                // up as ~2 blank lines between paragraphs in the editor.
                html += "<p style=\"margin-top:0px; margin-bottom:0px;\">";
                html += paraHasContent ? para : QStringLiteral("<br/>");
                html += "</p>";
            }
        }
    }

    mz_zip_reader_end(&zip);

    if (r.hasError()) {
        if (errorOut)
            *errorOut = QStringLiteral("XML error in document.xml: %1")
                            .arg(r.errorString());
        return false;
    }

    html += "</body></html>";
    htmlOut = html;
    return true;
}
