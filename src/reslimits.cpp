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

#include "reslimits.h"
#include "kcmsystemd.h"

ResLimitsDialog::ResLimitsDialog (QWidget* parent, Qt::WFlags flags) : KDialog ( parent)
{
  // Setup the dialog window
  QWidget *widget = new QWidget(this);
  ui.setupUi(widget);
  setMainWidget( widget );
  setWindowTitle(i18n("Set default resource limits"));
  
  // Initialize all the spinboxes from "resLimits"
  for(QVariantMap::const_iterator iter = kcmsystemd::resLimits.begin(); iter != kcmsystemd::resLimits.end(); ++iter)
    {
      QSpinBox *a = this->findChild<QSpinBox *>("spn" + QString(iter.key()));
      if (a)
	a->setValue(iter.value().toInt());
    }
 
  // Connect all the spinboxes to the slotChanged slot
  connect(ui.spnDefaultLimitCPU, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  connect(ui.spnDefaultLimitFSIZE, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  connect(ui.spnDefaultLimitDATA, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  connect(ui.spnDefaultLimitSTACK, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  connect(ui.spnDefaultLimitCORE, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  connect(ui.spnDefaultLimitRSS, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  connect(ui.spnDefaultLimitNOFILE, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  connect(ui.spnDefaultLimitAS, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  connect(ui.spnDefaultLimitNPROC, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  connect(ui.spnDefaultLimitMEMLOCK, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  connect(ui.spnDefaultLimitLOCKS, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  connect(ui.spnDefaultLimitSIGPENDING, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  connect(ui.spnDefaultLimitMSGQUEUE, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  connect(ui.spnDefaultLimitNICE, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  connect(ui.spnDefaultLimitRTPRIO, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  connect(ui.spnDefaultLimitRTTIME, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
}

void ResLimitsDialog::slotButtonClicked(int button)
{
  if (button == KDialog::Ok)
  {
    // write integers from spinboxes to QVariantMap "resLimits"
    QList<QSpinBox *> list = this->findChildren<QSpinBox *>();
    foreach(QSpinBox *Name, list)
      kcmsystemd::resLimits[Name->objectName().remove("spn")] = Name->value();  
  }
  KDialog::slotButtonClicked(button);
}

void ResLimitsDialog::slotChanged()
{
  // If the user changes any spinbox, set the resLimitsChanged variable to true
  kcmsystemd::resLimitsChanged = 1;
}
