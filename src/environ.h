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

#ifndef ENVIRON_H
#define ENVIRON_H
 
#include "ui_environ.h"
 
class EnvironDialog : public KDialog
{
  Q_OBJECT

  public:
    explicit EnvironDialog(QWidget *parent=0,
                           QString environ = "");
    void addNewVariable(int, QString, QString);    
    bool getChanged();
    QString getEnviron();
  
  private:
    Ui::EnvironDialog ui;
    int addedVars;
    bool changed;
    QList<QPair<QString, QString> > environ;
    
  private slots:
    void slotButtonClicked(int button);
    void slotNewVariable();
    void slotRemoveVariable(const int &index);
};
 
#endif