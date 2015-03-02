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
#include "confoption.h"
#include "confmodel.h"
#include "confdelegate.h"

// struct for storing units retrieved from systemd via DBus
struct SystemdUnit
{
  QString id, description, load_state, active_state, sub_state, following, job_type;
  QDBusObjectPath unit_path, job_path;
  unsigned int job_id;
  
  // The == operator must be provided to use contains() and indexOf()
  // on QLists of this struct
  bool operator==(const SystemdUnit& right) const
  {
    if (id == right.id)
      return true;
    else
      return false;
  }
};
Q_DECLARE_METATYPE(SystemdUnit)

// struct for storing sessions retrieved from logind via DBus
struct SystemdSession
{
  QString session_id, user_name, seat_id, session_state;
  QDBusObjectPath session_path;
  unsigned int user_id;

  // The == operator must be provided to use contains() and indexOf()
  // on QLists of this struct
  bool operator==(const SystemdSession& right) const
  {
    if (session_id == right.session_id)
      return true;
    else
      return false;
  }
};
Q_DECLARE_METATYPE(SystemdSession)

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
    void readConfFile(int);
    void authServiceAction(QString, QString, QString, QString, QList<QVariant>);
    bool eventFilter(QObject *, QEvent*);
    void updateUnitCount();
    void setupConfigParms();
    QStringList getLastJrnlEntries(QString);
    QProcess *kdeConfig;
    QVariantMap unitpaths;
    QSortFilterProxyModel *proxyModelUnitId, *proxyModelAct, *proxyModelConf;
    QStandardItemModel *unitsModel, *sessionModel;
    QList<SystemdUnit> unitslist;
    QList<SystemdSession> sessionlist;
    QList<unitfile> unitfileslist;
    QStringList listConfFiles;
    QString kdePrefix, selectedUnit, etcDir, filterUnitType, searchTerm;
    QMenu *contextMenuUnits;
    QAction *actEnableUnit, *actDisableUnit;
    int systemdVersion, timesLoad = 0, lastUnitRowChecked = -1, lastSessionRowChecked = -1, noActUnits;
    qulonglong partPersSizeMB, partVolaSizeMB;
    bool isPersistent, varLogDirExists;

  private slots:
    void slotKdeConfig();
    void slotChkShowUnits();
    void slotCmbUnitTypes();
    void slotUnitContextMenu(const QPoint &);
    void slotSessionContextMenu(const QPoint &);
    void slotRefreshUnitsList();
    void slotRefreshSessionList();
    void slotSystemdReloading(bool);
    // void slotUnitLoaded(QString, QDBusObjectPath);
    // void slotUnitUnloaded(QString, QDBusObjectPath);
    void slotUnitFilesChanged();
    void slotSystemdPropertiesChanged(QString, QVariantMap, QStringList);
    void slotLogindPropertiesChanged(QString, QVariantMap, QStringList);
    void slotLeSearchUnitChanged(QString);
    void slotConfChanged(const QModelIndex &, const QModelIndex &);
    void slotCmbConfFileChanged(int);
};

#endif // kcmsystemd_H
