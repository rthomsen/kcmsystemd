/*******************************************************************************
 * Copyright (C) 2013-2014 Ragnar Thomsen <rthomsen6@gmail.com>                *
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

#include <QtDBus>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>

#include <KCModule>
#include "ui_kcmsystemd.h"
#include "confoption.h"

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
    explicit kcmsystemd(QWidget *parent = 0, const QVariantList &list = QVariantList());
    void defaults();
    void load();
    void save();
        
  private:
    Ui::kcmsystemd ui;
    void setupSignalSlots();
    void initializeInterface();
    void setupUnitslist();
    void readConfFile(QString);
    void applyToInterface();
    void authServiceAction(QString, QString, QString, QString, QList<QVariant>);    
    bool eventFilter(QObject *, QEvent*);
    void updateUnitProps(QString);
    void updateUnitCount();
    void setupConfigParms();
    QProcess *kdeConfig;
    QVariantMap unitpaths;
    QSortFilterProxyModel *proxyModelUnitId, *proxyModelAct;
    QStandardItemModel *unitsModel;
    QList<SystemdUnit> unitslist;
    QList<unitfile> unitfileslist;
    QList<confOption> confOptList;
    QString kdePrefix, selectedUnit, etcDir, filterUnitType, searchTerm;
    QMenu *contextMenuUnits;
    QAction *actEnableUnit, *actDisableUnit;
    int systemdVersion, timesLoad, lastRowChecked, selectedRow, noActUnits;
    qulonglong partPersSizeMB, partVolaSizeMB;
    bool isPersistent, varLogDirExists;

  private slots:
    void slotKdeConfig();
    void slotTblRowChanged(const QModelIndex &, const QModelIndex &);
    void slotBtnStartUnit();
    void slotBtnStopUnit();    
    void slotBtnRestartUnit();
    void slotBtnReloadUnit(); 
    void slotChkShowUnits();
    void slotCmbUnitTypes();
    void slotDisplayMenu(const QPoint &);
    void slotRefreshUnitsList();
    void slotSystemdReloading(bool);
    // void slotUnitLoaded(QString, QDBusObjectPath);
    // void slotUnitUnloaded(QString, QDBusObjectPath);
    void slotUnitFilesChanged();
    void slotPropertiesChanged(QString, QVariantMap, QStringList);
    void slotLeSearchUnitChanged(QString);
    void slotOpenResourceLimits();
    void slotOpenEnviron();
    void slotOpenAdvanced();
    void slotJrnlStorageChanged(int);
    void slotFwdToSyslogChanged();
    void slotFwdToKmsgChanged();
    void slotFwdToConsoleChanged();
    void slotFwdToWallChanged();
    void slotJrnlStorageChkBoxes(int);
    void slotSpnMaxUseChanged();
    void slotSpnKeepFreeChanged();
    void slotSpnMaxFileSizeChanged();
    void slotKillUserProcessesChanged();
    void slotUpdateConfOption();
    void slotCoreStorageChanged(int);
};

#endif // kcmsystemd_H
