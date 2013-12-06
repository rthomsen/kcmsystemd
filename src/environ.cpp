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

#include "environ.h"
#include "kcmsystemd.h"

int addedVars = 0;

EnvironDialog::EnvironDialog (QWidget* parent, Qt::WFlags flags) : KDialog ( parent)
{
  // Initialize dialog window
  addedVars = 0;
  QWidget *widget = new QWidget(this);
  ui.setupUi(widget);
  setMainWidget( widget );
  setWindowTitle(i18n("Set environment variables"));

  // create line edits for variables read from system.conf
  QListIterator<QPair<QString,QString> > i(kcmsystemd::environ);
  while (i.hasNext())
  {
    addedVars++;
    addNewVariable(addedVars, i.peekNext().first, i.peekNext().second);
    i.next();
  }
  
  // button for adding a new variable
  connect(ui.btnNewVariable, SIGNAL(clicked()), this, SLOT(slotNewVariable()));
}

void EnvironDialog::slotButtonClicked(int button)
{
  if (button == KDialog::Ok)
  {
    // empty "environ"
    kcmsystemd::environ.clear();
    
    QPair<QString,QString> i;
    
    // find all QLineEdits that contain "Name"
    QList<QLineEdit *> list = this->findChildren<QLineEdit *>();
    foreach(QLineEdit *Name, list)
    {
      if (Name->objectName().contains("Name"))
      {
	QLineEdit *Value = this->findChild<QLineEdit *>(Name->objectName().section("Name",0,0) + "Value");

	// If qlineedits not empty append them to "environ"
	if (!Name->text().trimmed().isEmpty() && !Value->text().trimmed().isEmpty())
	{
	  i.first = Name->text();
	  i.second = Value->text();
	  kcmsystemd::environ.append(i);
	}
      }
    }    
  }
  KDialog::slotButtonClicked(button);
}

// slot for button to add new variable
void EnvironDialog::slotNewVariable()
{
  // each variable needs a numeric identifier so we can loop through them
  addedVars++;
  addNewVariable(addedVars,QString(""),QString(""));
  kcmsystemd::environChanged = 1;
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
  kcmsystemd::environChanged = 1;
  layout()->setSizeConstraint(QLayout::SetFixedSize);
}