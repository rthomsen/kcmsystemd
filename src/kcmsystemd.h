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
#include "ui_kcmsystemd.h"

class kcmsystemd : public KCModule
{
  Q_OBJECT

  public:
    explicit kcmsystemd(QWidget *parent = 0, const QVariantList &list = QVariantList());
    virtual void defaults();
    virtual void load();
    virtual void save();
    virtual void initializeInterface();
    virtual void readSystemConf();
    virtual void readJournaldConf();
    virtual void readLogindConf();
    QProcess *pkgConfigVer;
    static QVariantMap resLimits;
    static QList<QPair<QString, QString> > environ;
    static bool resLimitsChanged;
    static bool environChanged;
    
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
};

#endif // kcmsystemd_H
