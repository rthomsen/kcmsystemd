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

#ifndef CAPABILITIES_H
#define CAPABILITIES_H
 
#include "ui_capabilities.h"
 
class CapabilitiesDialog : public KDialog
{
  Q_OBJECT

  public:
    explicit CapabilitiesDialog(QWidget *parent=0,
                                QVariantMap capabilities = QVariantMap(),
                                bool capActive = false);
    bool getChanged();
    QVariantMap getCapabilities();
    bool getCapActive();

  private slots:
    virtual void slotButtonClicked(int button);
    void slotChanged();
    void slotChkCapabilitiesChanged();
 
  private:
    bool changed;
    Ui::CapabilitiesDialog ui;
};
 
#endif