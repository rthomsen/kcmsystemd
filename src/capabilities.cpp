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

#include <KMessageBox>
#include "capabilities.h"
#include "kcmsystemd.h"

CapabilitiesDialog::CapabilitiesDialog (QWidget* parent, QVariantMap capabilitiesPassed, bool capActive) : KDialog ( parent)
{
  // Setup the dialog window
  QWidget *widget = new QWidget(this);
  ui.setupUi(widget);
  setMainWidget( widget );
  setWindowTitle(i18n("Set default resource limits"));
  
  changed = false;
  
  if (capActive)
    ui.chkCapabilities->setChecked(true);
  slotChkCapabilitiesChanged();
  
  for(QVariantMap::const_iterator iter = capabilitiesPassed.begin(); iter != capabilitiesPassed.end(); ++iter)
  {
    QCheckBox *chk = this->findChild<QCheckBox *>("chk" + iter.key());
    if (chk && iter.value().toBool() == true)
    {
      chk->setChecked(1);
    }
  }
     
  QList<QCheckBox *> listChk = this->findChildren<QCheckBox *>(QRegExp("chk\\w+"));
  foreach(QCheckBox *chk, listChk)
    connect(chk, SIGNAL(stateChanged(int)), this, SLOT(slotChanged()));
  
  connect(ui.chkCapabilities, SIGNAL(stateChanged(int)), this, SLOT(slotChkCapabilitiesChanged()));

}

void CapabilitiesDialog::slotButtonClicked(int button)
{
  if (button == KDialog::Ok)
  {
    bool noCaps = true;
    
    if (ui.chkCapabilities->isChecked() == false)
      noCaps = false;
  
    QList<QCheckBox *> list = this->findChildren<QCheckBox *>(QRegExp("chkCAP\\w+"));
    foreach(QCheckBox *i, list)
    {
      if (i->isChecked() == true)
        noCaps = false;
    }
    
    if (noCaps)
    {
      int reply = KMessageBox::warningContinueCancel(this, i18n("Warning: If you enable this feature with all capabilities disabled, systemd will be unable to boot!"), "Warning");
      if (reply == KMessageBox::Cancel)
        return;
    }
  }
  KDialog::slotButtonClicked(button);
}

void CapabilitiesDialog::slotChanged()
{
  // If the user changes any spinbox, set the capabilitiesChanged variable to true
  changed = true;
}

bool CapabilitiesDialog::getChanged()
{
  return changed;
}

bool CapabilitiesDialog::getCapActive()
{
  return ui.chkCapabilities->isChecked();
}

QVariantMap CapabilitiesDialog::getCapabilities()
{
  QVariantMap newCapabilities;
  
  QList<QCheckBox *> listChk = this->findChildren<QCheckBox *>(QRegExp("chkCAP\\w+"));
  foreach(QCheckBox *chk, listChk)
  {
    if (chk->isChecked())
      newCapabilities[chk->objectName().remove("chk")] = true;
    else
      newCapabilities[chk->objectName().remove("chk")] = false;
  }

  // qDebug() << newCapabilities;
  return newCapabilities;
}

void CapabilitiesDialog::slotChkCapabilitiesChanged()
{
  if (ui.chkCapabilities->isChecked())
    ui.frmCapabilities->setEnabled(true);
  else
    ui.frmCapabilities->setEnabled(false);
}

