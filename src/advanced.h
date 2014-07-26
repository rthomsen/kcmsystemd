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

#ifndef ADVANCED_H
#define ADVANCED_H
 
#include "ui_advanced.h"
 
class AdvancedDialog : public KDialog
{
  Q_OBJECT

  public:
    explicit AdvancedDialog(QWidget *parent = 0,
                            QVariantMap args = QVariantMap());
    
    bool getChanged();
    QString getJoinControllers();
    QString getDefaultControllers();
    qulonglong getTimerSlack();
    qulonglong getRuntimeWatchdog();
    qulonglong getShutdownWatchdog();
    QVariantMap getCPUAffinity();
    bool getCPUAffActive();
    QVariantMap getSystemCallArchitectures();
    bool getSysCallActive();
    QVariantMap getCapabilities();
    bool getCapActive();
  
  private slots:  
    virtual void slotButtonClicked(int button);
    void slotChanged();
    void slotChkCPUAffinityChanged();
    void slotChkSystemCallArchitecturesChanged();
    void slotOpenCapabilities();
 
  private:
    bool changed;
    QVariantMap tempCap;
    bool tempCapActive;
    Ui::AdvancedDialog ui;
};
 
#endif