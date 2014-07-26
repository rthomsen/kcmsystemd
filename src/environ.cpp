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
#include "environ.h"

EnvironDialog::EnvironDialog (QWidget* parent,
                              QString rvalPassed) : KDialog (parent)
{
  // Initialize dialog window
  QWidget *widget = new QWidget(this);
  ui.setupUi(widget);
  setMainWidget( widget );
  setWindowTitle(i18n("Set environment variables"));
  
  addedVars = 0;
  changed = false;
  
  while (!rvalPassed.trimmed().isEmpty())
  {
    // If first character is quotation sign
    if (rvalPassed.trimmed().left(1) == "\"") {
      
      // read variable name and value
      QPair<QString,QString> i;
      i.first = rvalPassed.section('\"',1,1).section('=',0,0);
      i.second = rvalPassed.section('\"',1,1).section('=',-1);
      environ.append(i);
              
      // remove the read variable from rvalPassed
      rvalPassed = rvalPassed.remove("\"" + rvalPassed.section('\"',1,1) + "\"");
      
    } else {  // if first character is not quotation sign
      QPair<QString,QString> i;
      
      i.first = rvalPassed.section('=',0,0).trimmed();
      i.second = rvalPassed.section('=',1,1).trimmed().section(QRegExp("\\s+"),0,0);
      environ.append(i);
      
      // remove the read variable from the string
      rvalPassed = rvalPassed.remove(rvalPassed.section(QRegExp("\\s+"),0,0));
    }
    // Trim any extra whitespace
    rvalPassed = rvalPassed.trimmed();
  }
  
  // create line edits for variables read from system.conf
  QListIterator<QPair<QString,QString> > i(environ);
  while (i.hasNext())
  {
    addedVars++;
    addNewVariable(addedVars, i.peekNext().first, i.peekNext().second);
    i.next();
  }
  
  // button for adding a new variable
  connect(ui.btnNewVariable, SIGNAL(clicked()), this, SLOT(slotNewVariable()));
}

void EnvironDialog::addNewVariable(int index, QString name, QString value)
{
  QSignalMapper* signalMapper = new QSignalMapper (this);
  
  QLineEdit* leName = new QLineEdit (this);
  QLabel* lblEquals = new QLabel (this);
  QLineEdit* leValue = new QLineEdit (this);
  QPushButton* btnRm = new QPushButton ("Remove", this);
  ui.veLayoutVarNames->addWidget(leName);
  ui.veLayoutEquals->addWidget(lblEquals);
  ui.veLayoutVarValues->addWidget(leValue);
  ui.veLayoutRemoval->addWidget(btnRm);
  leName->setObjectName(QString::number(index)+"Name");
  leName->show();
  lblEquals->setObjectName(QString::number(index)+"Equals");
  lblEquals->show();
  leValue->setObjectName(QString::number(index)+"Value");
  leValue->show();
  btnRm->setObjectName(QString::number(index)+"Btn");
  btnRm->show();

  leName->setText(name);
  leValue->setText(value);
  lblEquals->setText("=");
  
  connect(btnRm, SIGNAL(clicked()), signalMapper, SLOT(map()));
  signalMapper->setMapping (btnRm, index);
  connect(signalMapper, SIGNAL(mapped(const int &)), this, SLOT(slotRemoveVariable(const int &)));
}

bool EnvironDialog::getChanged()
{
  return changed;
}

QString EnvironDialog::getEnviron()
{
  QString newDefEnviron;
  
  // empty "environ"
  environ.clear();
  
  QPair<QString,QString> singleVar;
  
  // find all QLineEdits that contain "Name"
  QList<QLineEdit *> leList = this->findChildren<QLineEdit *>();
  foreach(QLineEdit *Name, leList)
  {
    if (Name->objectName().contains("Name"))
    {
      QLineEdit *Value = this->findChild<QLineEdit *>(Name->objectName().section("Name",0,0) + "Value");

      // If qlineedits not empty append them to "environ"
      if (!Name->text().trimmed().isEmpty() && !Value->text().trimmed().isEmpty())
      {
        singleVar.first = Name->text();
        singleVar.second = Value->text();
        environ.append(singleVar);
      }
    }
  }   
  
  QListIterator<QPair<QString,QString> > list(environ);
  while (list.hasNext())
  {
    if (list.peekNext().first.contains(" ") || list.peekNext().second.contains(" "))
    {
      newDefEnviron.append("\"" + list.peekNext().first + "=" + list.peekNext().second + "\" ");
      list.next();
    } else {
      newDefEnviron.append(list.peekNext().first + "=" + list.peekNext().second + " ");
      list.next();
    }
  }
  
  return newDefEnviron.trimmed();
}

void EnvironDialog::slotButtonClicked(int button)
{
  if (button == KDialog::Ok)
  {}
  KDialog::slotButtonClicked(button);
}

// slot for button to add new variable
void EnvironDialog::slotNewVariable()
{
  // each variable needs a numeric identifier so we can loop through them
  addedVars++;
  addNewVariable(addedVars,QString(""),QString(""));
  changed = true;
}

void EnvironDialog::slotRemoveVariable(const int &index)
{
  QLineEdit *varName = this->findChild<QLineEdit *>(QString::number(index)+"Name");
  if (varName)
    varName->hide();
    delete varName;
  QLabel *varEquals = this->findChild<QLabel *>(QString::number(index)+"Equals");
  if (varEquals)
    varEquals->hide();
    delete varEquals;
  QLineEdit *varValue = this->findChild<QLineEdit *>(QString::number(index)+"Value");
  if (varValue)
    varValue->hide();
    delete varValue;
  QPushButton *varBtn = this->findChild<QPushButton *>(QString::number(index)+"Btn");
  if (varBtn)
    varBtn->hide();
    delete varBtn;
  addedVars--;
  changed = true;
  layout()->setSizeConstraint(QLayout::SetFixedSize);
}