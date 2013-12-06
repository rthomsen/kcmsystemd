/*******************************************************************************
 * Copyright (C) 2013 Ragnar Thomsen <rthomsen6@gmail.com>                     *
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

class kcmsystemd : public KCModule
{
  Q_OBJECT

  public:
    explicit kcmsystemd(QWidget *parent = 0, const QVariantList &list = QVariantList());
    void defaults();
    void load();
    void save();
    static QList<QPair<QString, QString> > environ;
    static QVariantMap resLimits;
    static bool resLimitsChanged;
    static bool environChanged;
    
  private:
    Ui::kcmsystemd ui;
    void setupSignalSlots();
    void initializeInterface();
    void setupUnitslist();
    void readSystemConf();
    void readJournaldConf();
    void readLogindConf();
    void authServiceAction(QString, QString, QString, QString, QList<QVariant>);
    bool eventFilter(QObject *, QEvent*);
    void updateUnitProps(QString);
    void updateUnitCount();
    QProcess *pkgConfigVer;
    QVariantMap unitpaths;
    QSortFilterProxyModel *proxyModelUnitType, *proxyModelAct, *proxyModelUnitName;
    QStandardItemModel *unitsModel;
    QList<SystemdUnit> unitslist;
    QString selectedUnit, etcDir;
    QMenu *contextMenuUnits;
    QAction *actEnableUnit, *actDisableUnit;
    float perDiskUsageValue, perDiskFreeValue, perSizeFilesValue, volDiskUsageValue, volDiskFreeValue, volSizeFilesValue;
    int timesLoad, lastRowChecked, selectedRow, noActUnits;
    long long unsigned partPersSizeMB, partVolaSizeMB;
    bool isPersistent;
    bool ToBoolDefOff(QString);
    bool ToBoolDefOn(QString);
    void updateSizeLimits(QComboBox*, QSpinBox*);

  private slots:
    void slotVersion();
    void slotDefaultChanged();
    void slotTblRowChanged(const QModelIndex &, const QModelIndex &);
    void slotBtnStartUnit();
    void slotBtnStopUnit();    
    void slotBtnRestartUnit();
    void slotBtnReloadUnit(); 
    void slotChkInactiveUnits();
    void slotCmbUnitTypes();
    void slotDisplayMenu(const QPoint &);
    void slotRefreshUnitsList();
    void slotSystemdReloading(bool);
    void slotUnitLoaded(QString, QDBusObjectPath);
    void slotUnitUnloaded(QString, QDBusObjectPath);
    void slotUnitFilesChanged();
    void slotPropertiesChanged(QString, QVariantMap, QStringList);
    void slotLeSearchUnitChanged(QString);
    void slotCPUAffinityChanged();
    void slotOpenResourceLimits();
    void slotOpenEnviron();
    void slotStorageChanged();
    void slotFwdToSyslogChanged();
    void slotFwdToKmsgChanged();
    void slotFwdToConsoleChanged();
    void slotSpnDiskUsageChanged();
    void slotSpnDiskFreeChanged();
    void slotSpnSizeFilesChanged();
    void slotMaxRetentionChanged();
    void slotMaxFileChanged();
    void slotKillUserProcessesChanged();
};

#endif // kcmsystemd_H
