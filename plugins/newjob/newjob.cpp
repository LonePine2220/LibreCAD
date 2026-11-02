/*****************************************************************************/
/*  newjob.cpp - "New Job" survey starter plugin for LibreCAD                */
/*                                                                           */
/*  This library is free software, licensed under the terms of the GNU       */
/*  General Public License as published by the Free Software Foundation,     */
/*  either version 2 of the License, or (at your option) any later version.  */
/*****************************************************************************/

#include <cmath>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QTimeZone>
#include <QDoubleValidator>
#include <QEventLoop>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "newjob.h"

using namespace NJ;

static const char *SR_NC_FT = "2264";   // NAD83 / North Carolina (ftUS)
static constexpr double PI = 3.14159265358979323846;

/* ------------------------------------------------------------------ */
PluginCapabilities NewJob::getCapabilities() const
{
    PluginCapabilities caps;
    caps.menuEntryPoints
        << PluginMenuLocation("plugins_menu", tr("New Job (GIS parcels, flood, vicinity map)"),
                            tr("Start a survey job from NC OneMap parcels, FEMA flood data and Census roads"));
    return caps;
}

QString NewJob::name() const { return tr("New Job"); }

void NewJob::execComm(Document_Interface *doc, QWidget *parent, [[maybe_unused]] QString cmd)
{
    NewJobOptions opt;
    opt.load();
    NewJobDialog dlg(opt, parent);
    if (dlg.exec() != QDialog::Accepted) return;
    opt.save();

    JobBuilder job(doc, parent, opt);
    bool ok = job.run();
    QString msg = job.summary;
    if (!job.warnings.isEmpty())
        msg += "\n\n" + QObject::tr("Notes:") + "\n- " + job.warnings.join("\n- ");
    if (ok) QMessageBox::information(parent, tr("New Job"), msg);
    else    QMessageBox::warning(parent, tr("New Job"), msg);
    doc->updateView();
}

/* ------------------------------------------------------------------ */
void NewJobOptions::load()
{
    QSettings s(QSettings::IniFormat, QSettings::UserScope, "LibreCAD", "newjob");
    searchBy     = s.value("searchby", 0).toInt();
    searchText   = s.value("searchtext").toString();
    county       = s.value("county").toString();
    platScale    = s.value("platscale", 50.0).toDouble();
    textHeight   = s.value("textheight", 0.08).toDouble();
    adjBuffer    = s.value("adjbuffer", 10.0).toDouble();
    drawAdjoiners= s.value("adjoiners", true).toBool();
    labelParcels = s.value("labels", true).toBool();
    drawFlood    = s.value("flood", true).toBool();
    drawVicinity = s.value("vicinity", true).toBool();
    vicScale     = s.value("vicscale", 2000.0).toDouble();
    vicSize      = s.value("vicsize", 4.0).toDouble();

    s.beginGroup("services");
    parcelUrl = s.value("parcels", "https://services.nconemap.gov/secure/rest/services/NC1Map_Parcels/FeatureServer/1").toString();
    // Layer 0 of that service is parcel points; polygons are layer 1
    if (parcelUrl.endsWith("NC1Map_Parcels/FeatureServer/0")) { parcelUrl.chop(1); parcelUrl += "1"; }
    floodUrl  = s.value("floodzones", "https://hazards.fema.gov/gis/nfhl/rest/services/public/NFHL/MapServer/28").toString();
    panelUrl  = s.value("firmpanels", "https://hazards.fema.gov/gis/nfhl/rest/services/public/NFHL/MapServer/3").toString();
    roadsUrl  = s.value("roads", "https://tigerweb.geo.census.gov/arcgis/rest/services/TIGERweb/Transportation/MapServer").toString();
    s.endGroup();

    s.beginGroup("layers");
    laySubject    = s.value("subject", "GIS-PARCEL-SUBJECT").toString();
    layAdjoiner   = s.value("adjoiner", "GIS-PARCEL-ADJ").toString();
    layParcelText = s.value("parceltext", "GIS-PARCEL-TEXT").toString();
    layFlood      = s.value("flood", "GIS-FLOOD").toString();
    layFloodText  = s.value("floodtext", "GIS-FLOOD-TEXT").toString();
    layVicFrame   = s.value("vicframe", "VMAP-FRAME").toString();
    layVicMajor   = s.value("vicmajor", "VMAP-ROAD-MAJOR").toString();
    layVicRoads   = s.value("vicroads", "VMAP-ROAD").toString();
    layVicText    = s.value("victext", "VMAP-TEXT").toString();
    layVicSite    = s.value("vicsite", "VMAP-SITE").toString();
    s.endGroup();
}

void NewJobOptions::save() const
{
    QSettings s(QSettings::IniFormat, QSettings::UserScope, "LibreCAD", "newjob");
    s.setValue("searchby", searchBy);
    s.setValue("searchtext", searchText);
    s.setValue("county", county);
    s.setValue("platscale", platScale);
    s.setValue("textheight", textHeight);
    s.setValue("adjbuffer", adjBuffer);
    s.setValue("adjoiners", drawAdjoiners);
    s.setValue("labels", labelParcels);
    s.setValue("flood", drawFlood);
    s.setValue("vicinity", drawVicinity);
    s.setValue("vicscale", vicScale);
    s.setValue("vicsize", vicSize);
    // Written out so they can be edited in the .ini without a rebuild
    s.beginGroup("services");
    s.setValue("parcels", parcelUrl);
    s.setValue("floodzones", floodUrl);
    s.setValue("firmpanels", panelUrl);
    s.setValue("roads", roadsUrl);
    s.endGroup();
    s.beginGroup("layers");
    s.setValue("subject", laySubject);
    s.setValue("adjoiner", layAdjoiner);
    s.setValue("parceltext", layParcelText);
    s.setValue("flood", layFlood);
    s.setValue("floodtext", layFloodText);
    s.setValue("vicframe", layVicFrame);
    s.setValue("vicmajor", layVicMajor);
    s.setValue("vicroads", layVicRoads);
    s.setValue("victext", layVicText);
    s.setValue("vicsite", layVicSite);
    s.endGroup();
}

/* ------------------------------------------------------------------ */
NewJobDialog::NewJobDialog(NewJobOptions &opt, QWidget *parent) : QDialog(parent), o(opt)
{
    setWindowTitle(tr("New Job"));
    auto num = [this](double v, double bottom) {
        auto *e = new QLineEdit(QString::number(v));
        auto *val = new QDoubleValidator(this);
        val->setBottom(bottom);
        e->setValidator(val);
        return e;
    };

    auto *main = new QVBoxLayout;

    auto *gbFind = new QGroupBox(tr("Find the parcel (NC OneMap)"));
    auto *fFind = new QFormLayout;
    searchby = new QComboBox;
    searchby->addItems(QStringList() << tr("PIN or parcel ID") << tr("Site address"));
    searchby->setCurrentIndex(o.searchBy);
    searchtext = new QLineEdit(o.searchText);
    searchtext->setPlaceholderText(tr("e.g. 2584123456 or 953 Hardin Dr"));
    county = new QLineEdit(o.county);
    county->setPlaceholderText(tr("optional, e.g. Cleveland"));
    fFind->addRow(tr("Search by:"), searchby);
    fFind->addRow(tr("PIN / address:"), searchtext);
    fFind->addRow(tr("County:"), county);
    gbFind->setLayout(fFind);
    main->addWidget(gbFind);

    auto *gbDraw = new QGroupBox(tr("Draw"));
    auto *fDraw = new QFormLayout;
    platscale = num(o.platScale, 1);
    textheight = num(o.textHeight, 0.01);
    adjbuffer = num(o.adjBuffer, 0);
    adjoiners = new QCheckBox(tr("Adjoining parcels"));
    adjoiners->setChecked(o.drawAdjoiners);
    labels = new QCheckBox(tr("Owner / PIN / deed labels"));
    labels->setChecked(o.labelParcels);
    flood = new QCheckBox(tr("FEMA flood zones and flood note"));
    flood->setChecked(o.drawFlood);
    fDraw->addRow(tr("Plat scale 1\" ="), platscale);
    fDraw->addRow(tr("Text height (in):"), textheight);
    fDraw->addRow(adjoiners);
    fDraw->addRow(tr("Adjoiner search (ft):"), adjbuffer);
    fDraw->addRow(labels);
    fDraw->addRow(flood);
    gbDraw->setLayout(fDraw);
    main->addWidget(gbDraw);

    auto *gbVic = new QGroupBox(tr("Vicinity map"));
    auto *fVic = new QFormLayout;
    vicinity = new QCheckBox(tr("Draw vicinity map (Census roads)"));
    vicinity->setChecked(o.drawVicinity);
    vicscale = num(o.vicScale, 1);
    vicsize = num(o.vicSize, 0.5);
    fVic->addRow(vicinity);
    fVic->addRow(tr("Vicinity scale 1\" ="), vicscale);
    fVic->addRow(tr("Frame size (in):"), vicsize);
    fVic->addRow(new QLabel(tr("You'll be asked to pick where it goes; press Esc to place it automatically.")));
    gbVic->setLayout(fVic);
    main->addWidget(gbVic);

    auto *buttons = new QHBoxLayout;
    auto *ok = new QPushButton(tr("Start job"));
    auto *cancel = new QPushButton(tr("Cancel"));
    ok->setDefault(true);
    buttons->addStretch();
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    main->addLayout(buttons);
    setLayout(main);

    connect(ok, &QPushButton::clicked, this, &NewJobDialog::checkAccept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

void NewJobDialog::checkAccept()
{
    if (searchtext->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("New Job"), tr("Enter a PIN, parcel ID or address."));
        return;
    }
    if (platscale->text().toDouble() <= 0 || textheight->text().toDouble() <= 0 ||
        vicscale->text().toDouble() <= 0 || vicsize->text().toDouble() <= 0) {
        QMessageBox::warning(this, tr("New Job"), tr("Scales, text height and frame size must be greater than zero."));
        return;
    }
    o.searchBy = searchby->currentIndex();
    o.searchText = searchtext->text().trimmed();
    o.county = county->text().trimmed();
    o.platScale = platscale->text().toDouble();
    o.textHeight = textheight->text().toDouble();
    o.adjBuffer = adjbuffer->text().toDouble();
    o.drawAdjoiners = adjoiners->isChecked();
    o.labelParcels = labels->isChecked();
    o.drawFlood = flood->isChecked();
    o.drawVicinity = vicinity->isChecked();
    o.vicScale = vicscale->text().toDouble();
    o.vicSize = vicsize->text().toDouble();
    accept();
}

/* ------------------------------------------------------------------ */
JobBuilder::JobBuilder(Document_Interface *doc, QWidget *parent, const NewJobOptions &opt)
    : d(doc), parentW(parent), o(opt)
{
    th = o.textHeight * o.platScale;
}

QByteArray JobBuilder::fetch(const QString &baseUrl, const QList<QPair<QString, QString>> &params,
                             QString *err)
{
    // Build the query string ourselves: QUrlQuery leaves '%' and '+' alone,
    // which breaks SQL LIKE wildcards in ArcGIS where clauses.
    QByteArray q;
    for (const auto &p : params) {
        if (!q.isEmpty()) q += '&';
        q += QUrl::toPercentEncoding(p.first) + '=' + QUrl::toPercentEncoding(p.second);
    }
    QNetworkAccessManager nam;
    QNetworkRequest req;
    req.setRawHeader("User-Agent", "LibreCAD-NewJob/1.0");
    req.setTransferTimeout(60000);
    QNetworkReply *reply = nullptr;
    if (q.size() > 1800) {   // long geometry queries go as POST
        req.setUrl(QUrl(baseUrl));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        reply = nam.post(req, q);
    } else {
        QByteArray full = baseUrl.toUtf8();
        full += '?';
        full += q;
        req.setUrl(QUrl::fromEncoded(full));
        reply = nam.get(req);
    }
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    QByteArray data;
    if (reply->error() != QNetworkReply::NoError) {
        if (err) *err = reply->errorString();
    } else {
        data = reply->readAll();
    }
    reply->deleteLater();
    return data;
}

QJsonObject JobBuilder::query(const QString &url, const QList<QPair<QString, QString>> &params, QString *err)
{
    QString e;
    QByteArray data = fetch(url, params, &e);
    if (data.isEmpty()) {
        if (err) *err = e.isEmpty() ? QObject::tr("no response") : e;
        return {};
    }
    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (!doc.isObject()) {
        if (err) *err = QObject::tr("bad response (%1)").arg(pe.errorString());
        return {};
    }
    QJsonObject obj = doc.object();
    if (obj.contains("error")) {
        QJsonObject eo = obj.value("error").toObject();
        if (err) *err = eo.value("message").toString(QObject::tr("server error"));
        return {};
    }
    return obj;
}

static QString attr(const QJsonObject &a, const QStringList &names)
{
    // Field names differ in case between services; match case-insensitively
    for (auto it = a.begin(); it != a.end(); ++it)
        for (const QString &n : names)
            if (it.key().compare(n, Qt::CaseInsensitive) == 0) {
                const QJsonValue v = it.value();
                if (v.isNull() || v.isUndefined()) return {};
                if (v.isDouble()) {
                    double x = v.toDouble();
                    return (x == std::floor(x) && std::fabs(x) < 1e15) ? QString::number(qint64(x))
                                                                       : QString::number(x);
                }
                return v.toVariant().toString().trimmed();
            }
    return {};
}

static Poly readRings(const QJsonObject &geom, const char *key = "rings")
{
    Poly p;
    const QJsonArray rings = geom.value(key).toArray();
    for (const QJsonValue &rv : rings) {
        Ring r;
        for (const QJsonValue &pv : rv.toArray()) {
            QJsonArray c = pv.toArray();
            if (c.size() >= 2) r.append(QPointF(c[0].toDouble(), c[1].toDouble()));
        }
        if (r.size() >= 2) p.append(r);
    }
    return p;
}

QList<Parcel> JobBuilder::parseParcels(const QJsonObject &o) const
{
    QList<Parcel> out;
    for (const QJsonValue &fv : o.value("features").toArray()) {
        QJsonObject f = fv.toObject();
        QJsonObject a = f.value("attributes").toObject();
        Parcel p;
        p.geom = readRings(f.value("geometry").toObject());
        if (p.geom.isEmpty()) continue;
        p.owner = attr(a, {"ownname", "owner"});
        p.pin = attr(a, {"parno", "pin"});
        p.pid = attr(a, {"altparno", "pid"});
        p.book = attr(a, {"deedbook"});
        p.page = attr(a, {"deedpage"});
        if (p.book.isEmpty() && p.page.isEmpty()) {
            // NC OneMap carries the deed (when the county provides it) in sourceref,
            // in county-specific formats: "1934/1970", "DB 1934 PG 1970", "1934-1970"...
            QString ref = attr(a, {"sourceref"});
            QRegularExpressionMatch m = QRegularExpression("^\\D*(\\d+)\\D+(\\d+)\\D*$").match(ref);
            if (m.hasMatch()) { p.book = m.captured(1); p.page = m.captured(2); }
            else if (!ref.isEmpty()) p.book = ref;   // shown as-is
        }
        p.address = attr(a, {"siteadd"});
        p.acres = attr(a, {"gisacres", "acres"}).toDouble();
        if (p.acres <= 0) {   // some counties leave GIS acres at 0; compute from the shape
            double sqft = 0;
            for (const Ring &r : p.geom) sqft += ringArea(r);   // esri: holes run opposite to outer rings
            p.acres = std::fabs(sqft) / 43560.0;
        }
        out.append(p);
    }
    return out;
}

static QString sqlEsc(QString s) { return s.replace('\'', "''"); }

bool JobBuilder::chooseParcel(const QList<Parcel> &list, int *index)
{
    QStringList items;
    for (const Parcel &p : list)
        items << QString("%1  |  %2  |  %3").arg(p.pin, p.address, p.owner);
    bool ok = false;
    QString pick = QInputDialog::getItem(parentW, QObject::tr("New Job"),
                                         QObject::tr("Several parcels matched. Pick one:"),
                                         items, 0, false, &ok);
    if (!ok) return false;
    *index = items.indexOf(pick);
    return *index >= 0;
}

bool JobBuilder::pickPoint(QPointF *pt, const QString &msg)
{
    return d->getPoint(pt, msg);
}

bool JobBuilder::findSubject(Parcel *subject)
{
    QString s = o.searchText.toUpper().trimmed();
    QString where;
    if (o.searchBy == 0) {
        QString v = sqlEsc(s);
        QString stripped = sqlEsc(QString(s).remove(QRegularExpression("[^0-9A-Z]")));
        where = QString("(parno='%1' OR altparno='%1' OR parno='%2' OR altparno='%2')").arg(v, stripped);
    } else {
        // "953 Hardin Dr" -> '%953%HARDIN%' (first two words, as stored upper case)
        QStringList w = s.split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts);
        QString pat = "%" + w.mid(0, 2).join("%") + "%";
        where = QString("siteadd LIKE '%1'").arg(sqlEsc(pat));
    }
    if (!o.county.isEmpty())
        where += QString(" AND UPPER(cntyname) LIKE '%%1%'").arg(sqlEsc(o.county.toUpper()));

    QString err;
    QJsonObject res = query(o.parcelUrl + "/query", {
        {"where", where}, {"outFields", "*"}, {"returnGeometry", "true"},
        {"outSR", SR_NC_FT}, {"f", "json"}}, &err);
    if (res.isEmpty()) {
        summary = QObject::tr("Couldn't reach the NC OneMap parcel service: %1").arg(err);
        return false;
    }
    QList<Parcel> list = parseParcels(res);
    if (list.isEmpty()) {
        summary = QObject::tr("No parcel found for \"%1\". Check the PIN or try the address (or leave County blank).").arg(o.searchText);
        return false;
    }
    int idx = 0;
    if (list.size() > 1 && !chooseParcel(list, &idx)) {
        summary = QObject::tr("Cancelled.");
        return false;
    }
    *subject = list.at(idx);
    return true;
}

QList<Parcel> JobBuilder::findAdjoiners(const Parcel &subject)
{
    QList<Parcel> out;
    QRectF b = bounds(subject.geom).adjusted(-o.adjBuffer - 1, -o.adjBuffer - 1, o.adjBuffer + 1, o.adjBuffer + 1);
    QString err;
    QJsonObject res = query(o.parcelUrl + "/query", {
        {"where", "1=1"},
        {"geometry", QString("%1,%2,%3,%4").arg(b.left(), 0, 'f', 2).arg(b.top(), 0, 'f', 2)
                                           .arg(b.right(), 0, 'f', 2).arg(b.bottom(), 0, 'f', 2)},
        {"geometryType", "esriGeometryEnvelope"}, {"inSR", SR_NC_FT},
        {"spatialRel", "esriSpatialRelIntersects"}, {"outFields", "*"},
        {"returnGeometry", "true"}, {"outSR", SR_NC_FT}, {"f", "json"}}, &err);
    if (res.isEmpty()) {
        warnings << QObject::tr("Adjoiners not drawn: %1").arg(err);
        return out;
    }
    if (res.value("exceededTransferLimit").toBool())
        warnings << QObject::tr("The parcel service limited the number of results; some adjoiners may be missing.");
    QPointF sc = centroid(subject.geom);
    for (const Parcel &p : parseParcels(res)) {
        if (!subject.pin.isEmpty() && p.pin == subject.pin) continue;
        if (dist(centroid(p.geom), sc) < 0.01 && std::fabs(bounds(p.geom).width() - bounds(subject.geom).width()) < 0.01)
            continue;   // same shape, different record (condo stacks etc.)
        if (polyDistance(p.geom, subject.geom) <= o.adjBuffer)
            out.append(p);
    }
    return out;
}

/* ------------------------------------------------------------------ */
void JobBuilder::layer(const QString &name, int color, DPI::LineWidth w, DPI::LineType t)
{
    bool isNew = !d->getAllLayer().contains(name);
    d->setLayer(name);
    if (isNew) d->setCurrentLayerProperties(color, w, t);   // don't override the user's own layer settings
    curLayer = name;
}

void JobBuilder::polygon(const Poly &p)
{
    for (const Ring &r : p) {
        std::vector<Plug_VertexData> v;
        Ring rr = r;
        if (rr.size() > 2 && dist(rr.first(), rr.last()) < 1e-6) rr.removeLast();
        for (const QPointF &q : rr) v.emplace_back(q, 0.0);
        if (v.size() >= 2) d->addPolyline(v, true);
    }
}

void JobBuilder::path(const Path &p)
{
    std::vector<Plug_VertexData> v;
    for (const QPointF &q : p) v.emplace_back(q, 0.0);
    if (v.size() >= 2) d->addPolyline(v, false);
}

void JobBuilder::text(const QString &s, const QPointF &at, double h, double angleRad,
                      DPI::HAlign ha, DPI::VAlign va)
{
    if (s.trimmed().isEmpty()) return;
    QPointF p = at;
    d->addText(s, "romans", &p, h, angleRad, ha, va);
}

void JobBuilder::stackedText(const QStringList &lines, const QPointF &c, double h)
{
    QStringList l;
    for (const QString &s : lines) if (!s.trimmed().isEmpty()) l << s;
    const double gap = h * 1.6;
    double y = c.y() + gap * (l.size() - 1) / 2.0;
    for (const QString &s : l) { text(s, QPointF(c.x(), y), h); y -= gap; }
}

static QStringList parcelLines(const Parcel &p, bool subject)
{
    QStringList l;
    l << p.owner;
    if (!p.pin.isEmpty()) l << "PARCEL " + p.pin;
    if (!p.book.isEmpty() && !p.page.isEmpty())
        l << QString("DB %1 PG %2").arg(p.book, p.page);
    else if (!p.book.isEmpty())
        l << "DEED REF " + p.book;
    if (subject && p.acres > 0) l << QString("%1 AC (GIS)").arg(p.acres, 0, 'f', 3);
    return l;
}

/* ------------------------------------------------------------------ */
bool JobBuilder::run()
{
    QProgressDialog prog(QObject::tr("Finding the parcel..."), QString(), 0, 4, parentW);
    prog.setWindowModality(Qt::WindowModal);
    prog.setMinimumDuration(0);
    prog.setValue(0);
    QApplication::processEvents();

    Parcel subject;
    if (!findSubject(&subject)) return false;

    QList<Parcel> adj;
    if (o.drawAdjoiners) {
        prog.setLabelText(QObject::tr("Finding adjoiners..."));
        prog.setValue(1);
        adj = findAdjoiners(subject);
    }

    const QString startLayer = d->getCurrentLayer();
    layer(o.laySubject, 0xFF0000, DPI::Width09);
    polygon(subject.geom);
    if (!adj.isEmpty()) {
        layer(o.layAdjoiner, 0x808080, DPI::Width03);
        for (const Parcel &p : adj) polygon(p.geom);
    }
    if (o.labelParcels) {
        layer(o.layParcelText, 0xFFFFFF);
        QPointF sc = centroid(subject.geom);
        if (!pointInPoly(sc, subject.geom)) sc = closestOnPoly(sc, subject.geom);
        stackedText(parcelLines(subject, true), sc, th);
        for (const Parcel &p : adj) {
            // Label just across the common line from the subject, inside the adjoiner
            QPointF near = closestOnPoly(sc, p.geom);
            QPointF dir = near - sc;
            double len = std::hypot(dir.x(), dir.y());
            if (len < 1e-6) dir = QPointF(0, 1); else dir /= len;
            QPointF at = near + dir * (th * 6);
            if (!pointInPoly(at, p.geom)) at = centroid(p.geom);
            stackedText(parcelLines(p, false), at, th * 0.9);
        }
    }

    QRectF site = bounds(subject.geom);
    occupied = site;
    for (const Parcel &p : adj) occupied = unite(occupied, bounds(p.geom));
    occupied = occupied.intersected(site.adjusted(-1500, -1500, 1500, 1500));
    if (o.drawFlood) {
        prog.setLabelText(QObject::tr("Getting FEMA flood data..."));
        prog.setValue(2);
        drawFlood(site, subject);
    }
    if (o.drawVicinity) {
        prog.setLabelText(QObject::tr("Drawing vicinity map..."));
        prog.setValue(3);
        prog.hide();   // get it out of the way for the pick
        drawVicinity(site, subject);
    }
    prog.setValue(4);
    d->setLayer(startLayer);

    summary = QObject::tr("Started job for %1\nPIN %2   %3")
                  .arg(subject.owner, subject.pin, subject.address);
    if (o.drawAdjoiners)
        summary += "\n" + QObject::tr("%1 adjoining parcels drawn.").arg(adj.size());
    summary += "\n" + QObject::tr("Coordinates: NAD83 NC State Plane, US survey feet. GIS lines are approximate, not survey data.");
    return true;
}

/* ------------------------------------------------------------------ */
void JobBuilder::drawFlood(const QRectF &site, const Parcel &subject)
{
    const double pad = std::max(500.0, std::max(site.width(), site.height()));
    QRectF b = site.adjusted(-pad, -pad, pad, pad);
    QString env = QString("%1,%2,%3,%4").arg(b.left(), 0, 'f', 1).arg(b.top(), 0, 'f', 1)
                      .arg(b.right(), 0, 'f', 1).arg(b.bottom(), 0, 'f', 1);
    QString err;
    QJsonObject res = query(o.floodUrl + "/query", {
        {"where", "1=1"}, {"geometry", env}, {"geometryType", "esriGeometryEnvelope"},
        {"inSR", SR_NC_FT}, {"spatialRel", "esriSpatialRelIntersects"},
        {"outFields", "FLD_ZONE,ZONE_SUBTY,STATIC_BFE"}, {"returnGeometry", "true"},
        {"outSR", SR_NC_FT}, {"maxAllowableOffset", "1"}, {"f", "json"}}, &err);
    if (res.isEmpty()) {
        warnings << QObject::tr("FEMA flood zones not drawn: %1").arg(err);
        return;
    }

    QSet<QString> subjectZones;
    bool drewAny = false;
    for (const QJsonValue &fv : res.value("features").toArray()) {
        QJsonObject f = fv.toObject();
        QJsonObject a = f.value("attributes").toObject();
        QString zone = attr(a, {"FLD_ZONE"});
        QString sub = attr(a, {"ZONE_SUBTY"}).toUpper();
        Poly g = readRings(f.value("geometry").toObject());
        if (g.isEmpty()) continue;
        bool minimal = (zone == "X" && !sub.contains("0.2"));
        QString label = (zone == "X" && sub.contains("0.2")) ? QString("SHADED ZONE X") : QString("ZONE " + zone);
        if (zone == "X" && minimal) label = "ZONE X";
        if (polysIntersect(g, subject.geom)) subjectZones.insert(label);
        if (minimal) continue;   // don't outline the "minimal hazard" background
        layer(o.layFlood, 0x00A0FF, DPI::Width05, DPI::DashLine);
        polygon(g);
        layer(o.layFloodText, 0x00A0FF);
        // label near the site: closest boundary point, nudged inside
        QPointF c = site.center();
        QPointF at = pointInPoly(c, g) ? c + QPointF(0, -th * 6) : closestOnPoly(c, g);
        if (!pointInPoly(c, g)) {
            QPointF dir = at - c;
            double len = std::hypot(dir.x(), dir.y());
            if (len > 1e-6) at += dir / len * (th * 3);
        }
        QString bfe = attr(a, {"STATIC_BFE"});
        QString lbl = label;
        if (!bfe.isEmpty() && bfe.toDouble() > -9000) lbl += QString(" (BFE %1)").arg(bfe);
        text(lbl, at, th);
        drewAny = true;
    }

    // Panel + effective date at the subject
    QPointF c = centroid(subject.geom);
    QString panel, eff;
    QJsonObject pres = query(o.panelUrl + "/query", {
        {"where", "1=1"}, {"geometry", QString("%1,%2").arg(c.x(), 0, 'f', 2).arg(c.y(), 0, 'f', 2)},
        {"geometryType", "esriGeometryPoint"}, {"inSR", SR_NC_FT},
        {"spatialRel", "esriSpatialRelIntersects"}, {"outFields", "FIRM_PAN,EFF_DATE"},
        {"returnGeometry", "false"}, {"f", "json"}}, &err);
    const QJsonArray pf = pres.value("features").toArray();
    if (!pf.isEmpty()) {
        QJsonObject a = pf.first().toObject().value("attributes").toObject();
        panel = attr(a, {"FIRM_PAN"});
        QJsonValue ev;
        for (auto it = a.begin(); it != a.end(); ++it)
            if (it.key().compare("EFF_DATE", Qt::CaseInsensitive) == 0) ev = it.value();
        if (ev.isDouble())
            eff = QDateTime::fromMSecsSinceEpoch(qint64(ev.toDouble())).toUTC().toString("MM/dd/yyyy");
        else eff = ev.toString();
    } else {
        warnings << QObject::tr("FIRM panel not found; fill it in the flood note.");
    }

    QStringList zones = subjectZones.values();
    zones.sort();
    if (zones.isEmpty()) zones << "ZONE X";
    QString note = QObject::tr("FLOOD NOTE: THIS PROPERTY LIES IN %1 PER FEMA FIRM PANEL %2, EFFECTIVE %3.")
                       .arg(zones.join(" AND "), panel.isEmpty() ? "________" : panel,
                            eff.isEmpty() ? "________" : eff);
    layer(o.layFloodText, 0x00A0FF);
    text(note, QPointF(site.left(), site.top() - th * 8), th, 0.0, DPI::HAlignLeft, DPI::VAlignMiddle);
    if (!drewAny && zones.join("") == "ZONE X")
        warnings << QObject::tr("No special flood hazard areas near the site.");
}

/* ------------------------------------------------------------------ */
void JobBuilder::drawVicinity(const QRectF &site, const Parcel &subject)
{
    // Ground area covered by the frame, and how it's shrunk into the drawing
    const double halfGround = o.vicSize / 2.0 * o.vicScale;     // ft
    const double k = o.platScale / o.vicScale;                   // drawing ft per ground ft
    const double halfDraw = halfGround * k;
    const QPointF siteC = centroid(subject.geom);
    const QRectF groundBox(siteC.x() - halfGround, siteC.y() - halfGround, 2 * halfGround, 2 * halfGround);

    // Find the road layers by name so service re-numbering doesn't break us
    QString err;
    QJsonObject svc = query(o.roadsUrl, {{"f", "json"}}, &err);
    // TIGERweb has each road class at several generalized scales
    // ("Secondary Roads 578k scale", ...). Use one detailed layer per class.
    QList<int> roadLayers;
    QSet<int> majorLayers;
    const QJsonArray svcLayers = svc.value("layers").toArray();
    for (const QString &cls : {QString("Primary Roads"), QString("Secondary Roads"), QString("Local Roads")}) {
        int best = -1, bestScore = -1;
        for (const QJsonValue &lv : svcLayers) {
            QJsonObject l = lv.toObject();
            QString nm = l.value("name").toString().trimmed();
            if (!nm.startsWith(cls, Qt::CaseInsensitive) || nm.contains("Label", Qt::CaseInsensitive)) continue;
            int score = 1;
            if (nm.compare(cls, Qt::CaseInsensitive) == 0) score = 4;      // full detail
            else if (nm.contains("72")) score = 3;                         // 1:72k, the most detailed generalized one
            else if (nm.contains("Interstate", Qt::CaseInsensitive)) score = 0;
            if (score > bestScore) { bestScore = score; best = l.value("id").toInt(); }
        }
        if (best >= 0) {
            roadLayers << best;
            if (!cls.startsWith("Local")) majorLayers.insert(best);
        }
    }
    if (roadLayers.isEmpty()) {
        warnings << QObject::tr("Vicinity map roads unavailable: %1")
                        .arg(err.isEmpty() ? QObject::tr("road layers not found") : err);
    }

    struct Road { QString name; bool major; Path path; };
    QList<Road> roads;
    const QRectF q = groundBox.adjusted(-halfGround * 0.1, -halfGround * 0.1, halfGround * 0.1, halfGround * 0.1);
    for (int id : roadLayers) {
        QJsonObject res = query(o.roadsUrl + QString("/%1/query").arg(id), {
            {"where", "1=1"},
            {"geometry", QString("%1,%2,%3,%4").arg(q.left(), 0, 'f', 1).arg(q.top(), 0, 'f', 1)
                                               .arg(q.right(), 0, 'f', 1).arg(q.bottom(), 0, 'f', 1)},
            {"geometryType", "esriGeometryEnvelope"}, {"inSR", SR_NC_FT},
            {"spatialRel", "esriSpatialRelIntersects"}, {"outFields", "NAME,BASENAME,MTFCC"},
            {"returnGeometry", "true"}, {"outSR", SR_NC_FT},
            {"maxAllowableOffset", QString::number(halfGround / 400.0, 'f', 1)}, {"f", "json"}}, &err);
        for (const QJsonValue &fv : res.value("features").toArray()) {
            QJsonObject f = fv.toObject();
            QJsonObject a = f.value("attributes").toObject();
            QString nm = attr(a, {"NAME"});
            if (nm.isEmpty()) nm = attr(a, {"BASENAME"});
            QString mtfcc = attr(a, {"MTFCC"});
            bool major = majorLayers.contains(id) || mtfcc == "S1100" || mtfcc == "S1200";
            for (const Ring &pth : readRings(f.value("geometry").toObject(), "paths"))
                for (const Path &piece : clipPath(pth, groundBox))
                    roads.append({nm.toUpper(), major, piece});
        }
    }

    // Where to put it
    QPointF center;
    if (!pickPoint(&center, QObject::tr("Pick the center of the vicinity map (Esc = automatic)"))) {
        center = QPointF(occupied.right() + halfDraw + th * 12, site.center().y());
    }
    auto xf = [&](const QPointF &g) { return center + (g - siteC) * k; };

    const double vth = th * 0.75;
    layer(o.layVicFrame, 0xFFFFFF, DPI::Width07);
    polygon({Ring{center + QPointF(-halfDraw, -halfDraw), center + QPointF(halfDraw, -halfDraw),
                  center + QPointF(halfDraw, halfDraw), center + QPointF(-halfDraw, halfDraw)}});

    QMap<QString, Path> longest;   // one label per road name, on its longest piece
    for (const Road &r : roads) {
        layer(r.major ? o.layVicMajor : o.layVicRoads, r.major ? 0xFFFF00 : 0xC0C0C0,
              r.major ? DPI::Width07 : DPI::Width03);
        Path dp;
        for (const QPointF &g : r.path) dp.append(xf(g));
        path(dp);
        if (!r.name.isEmpty() && pathLength(dp) > pathLength(longest.value(r.name)))
            longest[r.name] = dp;
    }

    layer(o.layVicText, 0xFFFFFF);
    for (auto it = longest.begin(); it != longest.end(); ++it) {
        const Path &p = it.value();
        if (pathLength(p) < vth * it.key().size() * 0.8) continue;   // too short to fit the name
        // label at the middle of the piece, along the segment there
        double half = pathLength(p) / 2.0, run = 0;
        for (int i = 0; i + 1 < p.size(); ++i) {
            double sl = dist(p[i], p[i + 1]);
            if (run + sl >= half) {
                double t = sl > 0 ? (half - run) / sl : 0;
                QPointF m = p[i] + (p[i + 1] - p[i]) * t;
                double ang = std::atan2(p[i + 1].y() - p[i].y(), p[i + 1].x() - p[i].x());
                if (ang > PI / 2) ang -= PI;
                if (ang <= -PI / 2) ang += PI;
                QPointF off(-std::sin(ang) * vth * 0.9, std::cos(ang) * vth * 0.9);
                text(it.key(), m + off, vth, ang, DPI::HAlignCenter, DPI::VAlignBottom);
                break;
            }
            run += sl;
        }
    }

    // Site marker and titles
    layer(o.layVicSite, 0xFF0000, DPI::Width09);
    QPointF sp = xf(siteC);
    QPointF spc = sp;
    d->addCircle(&spc, std::max(vth * 1.2, std::max(site.width(), site.height()) * k * 0.6));
    text("SITE", sp + QPointF(vth * 2.2, vth * 1.6), vth * 1.2, 0.0, DPI::HAlignLeft, DPI::VAlignBottom);

    layer(o.layVicText, 0xFFFFFF);
    text("VICINITY MAP", center + QPointF(0, halfDraw + th * 3.2), th * 1.4, 0.0, DPI::HAlignCenter, DPI::VAlignBottom);
    text("NOT TO SCALE", center + QPointF(0, halfDraw + th * 1.2), th, 0.0, DPI::HAlignCenter, DPI::VAlignBottom);

    // North arrow, top-right inside the frame
    QPointF na = center + QPointF(halfDraw - th * 3, halfDraw - th * 7);
    const double L = th * 4;
    layer(o.layVicFrame, 0xFFFFFF, DPI::Width07);
    path({na, na + QPointF(0, L)});
    path({na + QPointF(-L * 0.25, L * 0.65), na + QPointF(0, L), na + QPointF(L * 0.25, L * 0.65)});
    layer(o.layVicText, 0xFFFFFF);
    text("N", na + QPointF(0, L + th * 0.6), th * 1.2, 0.0, DPI::HAlignCenter, DPI::VAlignBottom);

    if (roads.isEmpty() && !roadLayers.isEmpty())
        warnings << QObject::tr("No roads were returned for the vicinity map.");
}
