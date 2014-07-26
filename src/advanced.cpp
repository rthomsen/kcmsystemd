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

#include "kcmsystemd.h"
#include "advanced.h"
#include "capabilities.h"

AdvancedDialog::AdvancedDialog (QWidget* parent, QVariantMap args) : KDialog (parent)
//AdvancedDialog::AdvancedDialog (QWidget* parent, QVariantMap args) : KDialog (parent)
{
  // Setup the dialog window
  QWidget *widget = new QWidget(this);
  ui.setupUi(widget);
  setMainWidget( widget );
  setWindowTitle(i18n("Set advanced settings for systemd"));
  
  changed = false;
  tempCap = args["CapabilityBoundingSet"].toMap();
  tempCapActive = args["CapabilityBoundingSetActive"].toBool();
  
  for (int i = 1; i <= QThread::idealThreadCount(); ++i)
  {
    QCheckBox *box = new QCheckBox(QString("cpu" + QString::number(i)), this);
    box->setObjectName("chkCPU" + QString::number(i));
    ui.horizCPUAffinity->addWidget(box);
  }

  bool anySysCall = false;
  QVariantMap sysCallMap = args["SystemCallArchitectures"].toMap();
  for(QVariantMap::const_iterator iter = sysCallMap.begin(); iter != sysCallMap.end(); ++iter)
  {
    if (iter.value().toBool())
      anySysCall = true;
  }
  
  if (args["SystemCallArchitecturesActive"].toBool() && anySysCall)
    ui.chkSystemCallArchitectures->setChecked(true);
  slotChkSystemCallArchitecturesChanged();
  
  if (args["SystemCallArchitectures"].toMap()["native"] == true)
    ui.chkNative->setChecked(true);
  if (args["SystemCallArchitectures"].toMap()["x86"] == true)
    ui.chkx86->setChecked(true);
  if (args["SystemCallArchitectures"].toMap()["x86-64"] == true)
    ui.chkx8664->setChecked(true);
  if (args["SystemCallArchitectures"].toMap()["x32"] == true)
    ui.chkx32->setChecked(true);
  if (args["SystemCallArchitectures"].toMap()["arm"] == true)
    ui.chkArm->setChecked(true);

  if (args["CPUAffinityActive"].toBool())
    ui.chkCPUAffinity->setChecked(true);
  slotChkCPUAffinityChanged();
  
  bool anyCPUAff = false;
  QVariantMap CPUAffMap = args["CPUAffinity"].toMap();
  for(QVariantMap::const_iterator iter = CPUAffMap.begin(); iter != CPUAffMap.end(); ++iter)
  {
    QCheckBox *chk = this->findChild<QCheckBox *>("chkCPU" + iter.key());
    if (chk && iter.value().toBool())
    {
      chk->setChecked(1);
      anyCPUAff = true;
    }
  }
  if (args["CPUAffinityActive"].toBool() && anyCPUAff)
    ui.chkCPUAffinity->setChecked(true);
  slotChkCPUAffinityChanged();
 
  ui.leJoinControllers->setText(args["JoinControllers"].toString());
  ui.leDefControllers->setText(args["DefaultControllers"].toString());
  ui.spnTimerSlack->setValue(args["TimerSlackNSec"].toULongLong());
  ui.spnRuntimeWatchdog->setValue(args["RuntimeWatchdogSec"].toULongLong());
  ui.spnShutdownWatchdog->setValue(args["ShutdownWatchdogSec"].toULongLong());
  
  if (args["systemdVersion"].toInt() > 207)
  {
    ui.lblDefControllers->setEnabled(0);
    ui.leDefControllers->setEnabled(0);
  }
    
  connect(ui.leJoinControllers, SIGNAL(textEdited(QString)), this, SLOT(slotChanged()));
  connect(ui.leDefControllers, SIGNAL(textEdited(QString)), this, SLOT(slotChanged()));
  connect(ui.spnRuntimeWatchdog, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  connect(ui.spnShutdownWatchdog, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  connect(ui.spnTimerSlack, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));  
  QList<QCheckBox *> listChk = this->findChildren<QCheckBox *>(QRegExp("chk\\w+"));
  foreach(QCheckBox *chk, listChk)
    connect(chk, SIGNAL(stateChanged(int)), this, SLOT(slotChanged()));
  
  connect(ui.chkCPUAffinity, SIGNAL(stateChanged(int)), this, SLOT(slotChkCPUAffinityChanged()));
  connect(ui.chkSystemCallArchitectures, SIGNAL(stateChanged(int)), this, SLOT(slotChkSystemCallArchitecturesChanged()));
  connect(ui.btnCapabilities, SIGNAL(clicked()), this, SLOT(slotOpenCapabilities()));
  
}

bool AdvancedDialog::getChanged()
{
  return changed;
}

QString AdvancedDialog::getJoinControllers()
{
  return ui.leJoinControllers->text();
}

QString AdvancedDialog::getDefaultControllers()
{
  return ui.leDefControllers->text();
}

qulonglong AdvancedDialog::getTimerSlack()
{
  return ui.spnTimerSlack->value();
}

qulonglong AdvancedDialog::getRuntimeWatchdog()
{
  return ui.spnRuntimeWatchdog->value();
}

qulonglong AdvancedDialog::getShutdownWatchdog()
{
  return ui.spnShutdownWatchdog->value();
}

QVariantMap AdvancedDialog::getCPUAffinity()
{
  QVariantMap newCPUAffinity;
  
  QList<QCheckBox *> listChk = this->findChildren<QCheckBox *>(QRegExp("chkCPU\\d+"));
  foreach(QCheckBox *chk, listChk)
  {
    if (chk->isChecked())
      newCPUAffinity[chk->objectName().remove("chkCPU")] = true;
    else
      newCPUAffinity[chk->objectName().remove("chkCPU")] = false;
  }

  // qDebug() << newCPUAffinity;
  return newCPUAffinity;
}

bool AdvancedDialog::getCPUAffActive()
{
  return ui.chkCPUAffinity->isChecked();
}

QVariantMap AdvancedDialog::getSystemCallArchitectures()
{
  QVariantMap newSystemCallArch;
  
  if (ui.chkNative->isChecked())
    newSystemCallArch["native"] = true;
  else
    newSystemCallArch["native"] = false;
  if (ui.chkx86->isChecked())
    newSystemCallArch["x86"] = true;
  else
    newSystemCallArch["x86"] = false;
  if (ui.chkx8664->isChecked())
    newSystemCallArch["x86-64"] = true;
  else
    newSystemCallArch["x86-64"] = false;
  if (ui.chkx32->isChecked())
    newSystemCallArch["x32"] = true;
  else
    newSystemCallArch["x32"] = false;
  if (ui.chkArm->isChecked())
    newSystemCallArch["arm"] = true;
  else
    newSystemCallArch["arm"] = false;
    
  // qDebug() << newSystemCallArch;
  return newSystemCallArch; 
}

bool AdvancedDialog::getSysCallActive()
{
  return ui.chkSystemCallArchitectures->isChecked();
}

QVariantMap AdvancedDialog::getCapabilities()
{
  return tempCap;
}

bool AdvancedDialog::getCapActive()
{
  return tempCapActive;
}


void AdvancedDialog::slotButtonClicked(int button)
{
  if (button == KDialog::Ok)
  {}
  KDialog::slotButtonClicked(button);
}

void AdvancedDialog::slotChanged()
{
  changed = true;
}

void AdvancedDialog::slotChkCPUAffinityChanged()
{
  if (ui.chkCPUAffinity->isChecked())
    ui.frmCPUAffinity->setEnabled(true);
  else
    ui.frmCPUAffinity->setEnabled(false);  
}

void AdvancedDialog::slotChkSystemCallArchitecturesChanged()
{
  if (ui.chkSystemCallArchitectures->isChecked())
    ui.frmSystemCallArchitectures->setEnabled(true);
  else
    ui.frmSystemCallArchitectures->setEnabled(false);
}

void AdvancedDialog::slotOpenCapabilities()
{
  QPointer<CapabilitiesDialog> capabilitiesDialog = new CapabilitiesDialog(this,
                                                                           tempCap,
                                                                           tempCapActive);
  if (capabilitiesDialog->exec() == QDialog::Accepted)
  {
    tempCap = capabilitiesDialog->getCapabilities();
    tempCapActive = capabilitiesDialog->getCapActive();
    
    if (capabilitiesDialog->getChanged())
      changed = true;
  }
  
  delete capabilitiesDialog;
}