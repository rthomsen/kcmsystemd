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

#include <KCModule>
#include <QProcess>
#include <QtDBus>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include "ui_kcmsystemd.h"

// struct for storing units retrieved from systemd via DBus
struct SystemdUnit
{
  QString id, description, load_state, active_state, sub_state, following;
  QDBusObjectPath unit_path;
  unsigned int job_id;
  QString job_type;
  QDBusObjectPath job_path;
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
    void initializeInterface();
    void readSystemConf();
    void readJournaldConf();
    void readLogindConf();
    void readUnits();
    void authServiceAction(QString, QString, QString, QString, QList<QVariant>);
    bool eventFilter(QObject *, QEvent*);
    QProcess *pkgConfigVer;
    static QVariantMap resLimits;
    QVariantMap unitpaths;
    static QList<QPair<QString, QString> > environ;
    static bool resLimitsChanged;
    static bool environChanged;
    QSortFilterProxyModel *proxyModel, *proxyModel2;
    QStandardItemModel *unitsModel;
    QList<SystemdUnit> unitslist;
    QString selectedUnit, etcDir;
    QMenu *contextMenuUnits;
    QAction *actEnableUnit, *actDisableUnit;
    float perDiskUsageValue, perDiskFreeValue, perSizeFilesValue, volDiskUsageValue, volDiskFreeValue, volSizeFilesValue;
    int timesLoad, lastRowChecked;
    
  private:
    Ui::kcmsystemd ui;
    long long unsigned partPersSizeMB, partVolaSizeMB;
    bool isPersistent;
    bool ToBoolDefOff(QString);
    bool ToBoolDefOn(QString);
    void updateSizeLimits(QComboBox*, QSpinBox*);

  private slots:
    void slotVersion();
    void slotDefaultChanged();
    void slotCPUAffinityChanged();
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
    void slotOpenResourceLimits();
    void slotOpenEnviron();
    //void slotTblServicesClicked(const QItemSelection &, const QItemSelection &);
    void slotTblRowChanged(const QModelIndex &, const QModelIndex &);
    void slotBtnStartUnit();
    void slotBtnStopUnit();    
    void slotBtnRestartUnit();
    void slotBtnReloadUnit(); 
    void slotChkInactiveUnits();
    void slotCmbUnitTypes();
    void slotDisplayMenu(const QPoint &);
    void refreshUnitsList();
};

#endif // kcmsystemd_H
