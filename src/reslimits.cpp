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
#include "reslimits.h"

ResLimitsDialog::ResLimitsDialog (QWidget* parent, QVariantMap resLimitsMap) : KDialog ( parent)
{
  // Setup the dialog window
  QWidget *widget = new QWidget(this);
  ui.setupUi(widget);
  setMainWidget( widget );
  setWindowTitle(i18n("Set default resource limits"));
  
  changed = false;
  
  QList<QCheckBox *> listChk = this->findChildren<QCheckBox *>();
  foreach(QCheckBox *chk, listChk)
    connect(chk, SIGNAL(stateChanged(int)), this, SLOT(slotChkChanged()));
  
  // Initialize all the spinboxes from "resLimitsMap"
  for(QVariantMap::const_iterator iter = resLimitsMap.begin(); iter != resLimitsMap.end(); ++iter)
    {
      // qDebug() << iter.key() << " = " << iter.value();
      
      if (iter.value().toString() == "infinity")
        this->findChild<QCheckBox *>("chk" + iter.key())->setChecked(false);
      else
      {
        this->findChild<QCheckBox *>("chk" + iter.key())->setChecked(true);
    
        QSpinBox *spn = this->findChild<QSpinBox *>("spn" + QString(iter.key()));
        if(spn)
        {
          spn->setValue(iter.value().toUInt());
        }

        
      }
    }
 
  // Connect all the spin- and checkboxes to the slotChanged slot  
  foreach(QSpinBox *spn, this->findChildren<QSpinBox *>())
    connect(spn, SIGNAL(valueChanged(int)), this, SLOT(slotChanged()));
  foreach(QCheckBox *chk, this->findChildren<QCheckBox *>())
    connect(chk, SIGNAL(stateChanged(int)), this, SLOT(slotChanged()));
}

void ResLimitsDialog::slotButtonClicked(int button)
{
  if (button == KDialog::Ok)
  {

  }
  KDialog::slotButtonClicked(button);
}

void ResLimitsDialog::slotChanged()
{
  // If the user changes any spinbox, set the resLimitsChanged variable to true
  changed = true;
}

bool ResLimitsDialog::getChanged()
{
  return changed;
}

QVariantMap ResLimitsDialog::getResLimits()
{
  QVariantMap newResLimitsMap;
  // write integers from spinboxes to a QVariantMap
  QList<QSpinBox *> list = this->findChildren<QSpinBox *>();
  foreach(QSpinBox *spin, list)
  {
    QCheckBox *chk = this->findChild<QCheckBox *>("chk" + spin->objectName().remove("spn"));
    if (chk->isChecked())
      newResLimitsMap[spin->objectName().remove("spn")] = spin->value();
    else
      newResLimitsMap[spin->objectName().remove("spn")] = "infinity";
  }
  
  return newResLimitsMap;
}

void ResLimitsDialog::slotChkChanged()
{
  QString name = QObject::sender()->objectName().remove("chk");
  
  QCheckBox *chk = this->findChild<QCheckBox *>("chk" + name);
  QSpinBox *spin = this->findChild<QSpinBox *>("spn" + name);
  
  if (chk->isChecked())
    spin->setEnabled(true);
  else
    spin->setEnabled(false);

}
