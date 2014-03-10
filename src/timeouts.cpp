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

#include "timeouts.h"
#include "kcmsystemd.h"

TimeoutsDialog::TimeoutsDialog (QWidget* parent, Qt::WFlags flags) : KDialog ( parent)
{
  // Setup the dialog window
  QWidget *widget = new QWidget(this);
  ui.setupUi(widget);
  setMainWidget( widget );
  setWindowTitle(i18n("Set default timeouts for units"));
  
  // Initialize all the spinboxes from "timeoutSettings"
  for(QVariantMap::const_iterator iter = kcmsystemd::timeoutSettings.begin(); iter != kcmsystemd::timeoutSettings.end(); ++iter)
    {
      QSpinBox *a = this->findChild<QSpinBox *>("spn" + QString(iter.key()));
      if (a)
        a->setValue(iter.value().toInt());
    }
  
  connect(ui.spnDefaultTimeoutStartSec, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  connect(ui.spnDefaultTimeoutStopSec, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  connect(ui.spnDefaultRestartSec, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  connect(ui.spnDefaultStartLimitInterval, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  connect(ui.spnDefaultStartLimitBurst, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  
}

void TimeoutsDialog::slotButtonClicked(int button)
{
  if (button == KDialog::Ok)
  {
    // write integers from spinboxes to QVariantMap "timeoutSettings"
    QList<QSpinBox *> list = this->findChildren<QSpinBox *>();
    foreach(QSpinBox *Name, list)
      kcmsystemd::timeoutSettings[Name->objectName().remove("spn")] = Name->value();
  }
  KDialog::slotButtonClicked(button);
}

void TimeoutsDialog::slotChanged()
{
  // If the user changes any spinbox, set the timeoutsChanged variable to true
  kcmsystemd::timeoutsChanged = 1;
}
