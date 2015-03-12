/*******************************************************************************
 * Copyright (C) 2013-2015 Ragnar Thomsen <rthomsen6@gmail.com>                *
 *                                                                             *
 * This program is free software: you can redistribute it and/or modify it     *
 * under the terms of the GNU General Public License as published by the Free  *
 * Software Foundation, either version 3 of the License, or (at your option)   *
 * any later version.                                                          *
 *                                                                             *
 * This program is distributed in the hope that it will be useful, but WITHOUT *
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or       *
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for    *
 * more details.                                                               *
 *                                                                             *
 * You should have received a copy of the GNU General Public License along     *
 * with this program. If not, see <http://www.gnu.org/licenses/>.              *
 *******************************************************************************/

#ifndef KCMSYSTEMD_H
#define KCMSYSTEMD_H

#include <QtDBus/QtDBus>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QDialog>

#include <KCModule>
#include <KLocalizedString>

#include "ui_kcmsystemd.h"
#include "systemdunit.h"
#include "unitmodel.h"
#include "sortfilterunitmodel.h"
#include "confoption.h"
#include "confmodel.h"
#include "confdelegate.h"

struct unitfile
{
  QString name, status;
  
  bool operator==(const unitfile& right) const
  {
    if (name.section('/',-1) == right.name)
      return true;
    else
      return false;
  }
};

enum dbusConn
{
  systemd, logind
};

enum dbusIface
{
  sysdMgr, sysdUnit, sysdTimer, logdMgr, logdSession
};

class kcmsystemd : public KCModule
{
  Q_OBJECT

  public:
    explicit kcmsystemd(QWidget *parent,  const QVariantList &args);
    ~kcmsystemd();
    void defaults();
    void load();
    void save();
    static ConfModel *confModel;
    static QList<confOption> confOptList;

  private:
    Ui::kcmsystemd ui;
    void setupSignalSlots();
    void setupUnitslist();
    void setupConf();
    void setupSessionlist();
    void setupTimerlist();
    void readConfFile(int);
    void authServiceAction(QString, QString, QString, QString, QList<QVariant>);
    bool eventFilter(QObject *, QEvent*);
    void updateUnitCount();
    void setupConfigParms();
    QList<SystemdUnit> getUnitsFromDbus(dbusBus bus);
    QVariant getDbusProperty(QString prop, dbusIface ifaceName, QDBusObjectPath path = QDBusObjectPath("/org/freedesktop/systemd1"), dbusBus bus = sys);
    QDBusMessage callDbusMethod(QString method, dbusIface ifaceName, dbusBus bus = sys, const QList<QVariant> &args = QList<QVariant> ());
    QList<QStandardItem *> buildTimerListRow(const SystemdUnit &unit, const QList<SystemdUnit> &list, dbusBus bus);
    QProcess *kdeConfig;
    QSortFilterProxyModel *proxyModelConf;
    SortFilterUnitModel *systemUnitFilterModel, *userUnitFilterModel;
    QStandardItemModel *sessionModel, *timerModel;
    UnitModel *systemUnitModel, *userUnitModel;
    QList<SystemdUnit> unitslist, userUnitslist;
    QList<SystemdSession> sessionlist;
    QStringList listConfFiles;
    QString kdePrefix, etcDir, userBusPath;
    QMenu *contextMenuUnits;
    QAction *actEnableUnit, *actDisableUnit;
    int systemdVersion, timesLoad = 0, lastUnitRowChecked = -1, lastSessionRowChecked = -1, noActSystemUnits, noActUserUnits;
    qulonglong partPersSizeMB, partVolaSizeMB;
    bool enableUserUnits = true;
    QTimer *timer;
    const QStringList unitTypeSufx = QStringList() << "" << ".target" << ".service" << ".device" << ".mount"
                                                   << ".automount" << ".swap" << ".socket" << ".path"
                                                   << ".timer" << ".snapshot" << ".slice" << ".scope";
    const QString connSystemd = "org.freedesktop.systemd1";
    const QString connLogind = "org.freedesktop.login1";
    const QString pathSysdMgr = "/org/freedesktop/systemd1";
    const QString pathLogdMgr = "/org/freedesktop/login1";
    const QString ifaceMgr = "org.freedesktop.systemd1.Manager";
    const QString ifaceLogdMgr = "org.freedesktop.login1.Manager";
    const QString ifaceUnit = "org.freedesktop.systemd1.Unit";
    const QString ifaceTimer = "org.freedesktop.systemd1.Timer";
    const QString ifaceSession = "org.freedesktop.login1.Session";
    const QString ifaceDbusProp = "org.freedesktop.DBus.Properties";
    QDBusConnection systembus = QDBusConnection::systemBus();

  private slots:
    void slotKdeConfig();
    void slotChkShowUnits(int);
    void slotCmbUnitTypes(int);
    void slotUnitContextMenu(const QPoint &);
    void slotSessionContextMenu(const QPoint &);
    void slotRefreshUnitsList(bool, dbusBus);
    void slotRefreshSessionList();
    void slotRefreshTimerList();
    void slotSystemSystemdReloading(bool);
    void slotUserSystemdReloading(bool);
    void slotSystemUnitsChanged();
    void slotUserUnitsChanged();
    // void slotUnitLoaded(QString, QDBusObjectPath);
    // void slotUnitUnloaded(QString, QDBusObjectPath);
    void slotLogindPropertiesChanged(QString, QVariantMap, QStringList);
    void slotLeSearchUnitChanged(QString);
    void slotConfChanged(const QModelIndex &, const QModelIndex &);
    void slotCmbConfFileChanged(int);
    void slotUpdateTimers();
};

#endif // kcmsystemd_H
