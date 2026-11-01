/*****************************************************************************/
/*  newjob.h - "New Job" survey starter plugin for LibreCAD                  */
/*                                                                           */
/*  Pulls the subject parcel and adjoiners from NC OneMap, FEMA flood        */
/*  zones from the NFHL, and draws a vicinity map from Census TIGERweb       */
/*  roads. Coordinates are requested in NAD83 NC State Plane, US feet.       */
/*                                                                           */
/*  This library is free software, licensed under the terms of the GNU       */
/*  General Public License as published by the Free Software Foundation,     */
/*  either version 2 of the License, or (at your option) any later version.  */
/*****************************************************************************/

#ifndef NEWJOB_H
#define NEWJOB_H

#include <QDialog>
#include <QJsonObject>
#include <QStringList>
#include "qc_plugininterface.h"
#include "document_interface.h"
#include "newjob_geom.h"

class QComboBox;
class QLineEdit;
class QCheckBox;

class NewJob : public QObject, QC_PluginInterface
{
    Q_OBJECT
    Q_INTERFACES(QC_PluginInterface)
    Q_PLUGIN_METADATA(IID LC_DocumentInterface_iid FILE "newjob.json")

public:
    PluginCapabilities getCapabilities() const override;
    QString name() const override;
    void execComm(Document_Interface *doc, QWidget *parent, QString cmd) override;
};

/* Everything the user picks in the dialog, plus service URLs and layer
 * names that live in the settings file (editable without a rebuild). */
struct NewJobOptions {
    int searchBy = 0;           // 0 = PIN/PID, 1 = address
    QString searchText;
    QString county;
    double platScale = 50.0;    // 1" = X'
    double textHeight = 0.08;   // inches on paper
    double adjBuffer = 10.0;    // ft, how close a parcel must be to count as an adjoiner
    bool drawAdjoiners = true;
    bool labelParcels = true;
    bool drawFlood = true;
    bool drawVicinity = true;
    double vicScale = 2000.0;   // 1" = Y'
    double vicSize = 4.0;       // frame width on paper, inches

    QString parcelUrl, floodUrl, panelUrl, roadsUrl;
    QString laySubject, layAdjoiner, layParcelText, layFlood, layFloodText,
            layVicFrame, layVicMajor, layVicRoads, layVicText, layVicSite;

    void load();
    void save() const;
};

class NewJobDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NewJobDialog(NewJobOptions &opt, QWidget *parent = nullptr);
public slots:
    void checkAccept();
private:
    NewJobOptions &o;
    QComboBox *searchby;
    QLineEdit *searchtext, *county, *platscale, *textheight, *adjbuffer,
              *vicscale, *vicsize;
    QCheckBox *adjoiners, *labels, *flood, *vicinity;
};

struct Parcel {
    NJ::Poly geom;
    QString owner, pin, pid, book, page, address;
    double acres = 0;
};

/* Does the work: fetch, then draw. fetch() is virtual so tests can feed
 * canned responses. */
class JobBuilder
{
public:
    JobBuilder(Document_Interface *doc, QWidget *parent, const NewJobOptions &opt);
    virtual ~JobBuilder() = default;
    bool run();                       // returns false if nothing was drawn
    QStringList warnings;
    QString summary;

protected:
    virtual QByteArray fetch(const QString &baseUrl, const QList<QPair<QString, QString>> &params,
                             QString *err);
    virtual bool chooseParcel(const QList<Parcel> &list, int *index);
    virtual bool pickPoint(QPointF *pt, const QString &msg);

private:
    QJsonObject query(const QString &url, const QList<QPair<QString, QString>> &params, QString *err);
    QList<Parcel> parseParcels(const QJsonObject &o) const;
    bool findSubject(Parcel *subject);
    QList<Parcel> findAdjoiners(const Parcel &subject);
    void drawFlood(const QRectF &site, const Parcel &subject);
    void drawVicinity(const QRectF &site, const Parcel &subject);

    void layer(const QString &name, int color, DPI::LineWidth w = DPI::WidthByLayer,
               DPI::LineType t = DPI::SolidLine);
    void polygon(const NJ::Poly &p);
    void path(const NJ::Path &p);
    void text(const QString &s, const QPointF &at, double h, double angleRad = 0.0,
              DPI::HAlign ha = DPI::HAlignCenter, DPI::VAlign va = DPI::VAlignMiddle);
    void stackedText(const QStringList &lines, const QPointF &center, double h);

    Document_Interface *d;
    QWidget *parentW;
    NewJobOptions o;
    double th = 4.0;                  // model text height
    QString curLayer;
    QRectF occupied;                 // drawing area used by parcels, for auto placement
};

#endif
