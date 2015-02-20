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

#include <QSpinBox>

#include "confdelegate.h"
#include "confoption.h"
#include "kcmsystemd.h"

ConfDelegate::ConfDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QWidget *ConfDelegate::createEditor(QWidget *parent,
    const QStyleOptionViewItem &/* option */,
    const QModelIndex &index) const
{
  // Creates the editor component for an item in qtableview

  // qDebug() << "Creating editor";

  QString name = index.data(Qt::UserRole+3).toString();
  int confIndex = kcmsystemd::confOptList.indexOf(confOption(name));
  confOption const *opt = &kcmsystemd::confOptList.at(confIndex);

  if (index.data(Qt::UserRole+1) == BOOL)
  {
    QComboBox *editor = new QComboBox(parent);
    QStringList list = (QStringList() << "true" << "false");
    editor->addItems(list);
    return editor;
  }
  else if (index.data(Qt::UserRole+1) == TIME ||
           index.data(Qt::UserRole+1) == INTEGER ||
           index.data(Qt::UserRole+1) == RESLIMIT ||
           index.data(Qt::UserRole+1) == SIZE)
  {
    QSpinBox *editor = new QSpinBox(parent);
    if (index.data(Qt::UserRole+1) == RESLIMIT)
      editor->setMinimum(-1);
    else
      editor->setMinimum(0);
    if (index.data(Qt::UserRole+1) == SIZE)
      editor->setMaximum(opt->maxVal);
    else
      editor->setMaximum(999999999);
    return editor;
  }
  else if (index.data(Qt::UserRole+1) == LIST)
  {
    QComboBox *editor = new QComboBox(parent);

    // Populate combobox
    if (confIndex > -1)
      editor->addItems(opt->possibleVals);

    // editor->setFrame(false);
    return editor;
  }
  else if (index.data(Qt::UserRole+1) == MULTILIST)
  {
    QComboBox *editor = new QComboBox(parent);

    // Populate combobox
    QStandardItemModel *multiListModel = new QStandardItemModel();
    editor->setModel(multiListModel);
    for (int i = 0; i < opt->possibleVals.count(); ++i)
    {
      QStandardItem* item;
      item = new QStandardItem(opt->possibleVals.at(i));
      item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
      item->setData(Qt::Checked, Qt::CheckStateRole);

      multiListModel->appendRow(item);
    }
    // editor->setFrame(false);
    return editor;
  }
  else
  {
    QLineEdit *editor  = new QLineEdit(parent);
    editor->setFrame(false);
    return editor;
  }
}

void ConfDelegate::setEditorData(QWidget *editor,
                                    const QModelIndex &index) const
{
  // Set the value in the editor (spinbox, lineedit, etc)

  // qDebug() << "Setting editor data";

  if (index.data(Qt::UserRole+1) == BOOL)
  {
    QString value = index.model()->data(index, Qt::EditRole).toString().toLower();
    if (value == "true" || value == "on" || value == "yes")
      value = "true";
    else if (value == "false" || value == "off" || value == "no")
      value = "false";
    QComboBox *cmb = static_cast<QComboBox*>(editor);
    cmb->setCurrentIndex(cmb->findText(value));
  }
  else if (index.data(Qt::UserRole+1) == TIME ||
           index.data(Qt::UserRole+1) == INTEGER ||
           index.data(Qt::UserRole+1) == RESLIMIT ||
           index.data(Qt::UserRole+1) == SIZE)
  {
    QSpinBox *spinBox = static_cast<QSpinBox*>(editor);
    int value = index.model()->data(index, Qt::EditRole).toInt();
    spinBox->setValue(value);
  }
  else if (index.data(Qt::UserRole+1) == LIST)
  {
    QString value = index.model()->data(index, Qt::EditRole).toString();
    QComboBox *cmb = static_cast<QComboBox*>(editor);
    cmb->setCurrentIndex(cmb->findText(value));
  }
  else if (index.data(Qt::UserRole+1) == MULTILIST)
  {
    QComboBox *cmb = static_cast<QComboBox*>(editor);
    QVariantMap map = index.data(Qt::UserRole+2).toMap();

    for(QVariantMap::const_iterator iter = map.begin(); iter != map.end(); ++iter)
    {
      // qDebug() << iter.key() << " = " << iter.value();
      if (iter.value() == true)
        cmb->setItemData(cmb->findText(iter.key()), Qt::Checked, Qt::CheckStateRole);
      else
        cmb->setItemData(cmb->findText(iter.key()), Qt::Unchecked, Qt::CheckStateRole);
    }

  }
  else
  {
    QString value = index.model()->data(index, Qt::EditRole).toString();
    QLineEdit *le = static_cast<QLineEdit*>(editor);
    le->setText(value);
  }
}

void ConfDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                   const QModelIndex &index) const
{
  // Set data in model from the editor

  // qDebug() << "Setting model data";

  QString name = index.data(Qt::UserRole+3).toString();
  int confIndex = kcmsystemd::confOptList.indexOf(confOption(name));
  confOption const *opt = &kcmsystemd::confOptList.at(confIndex);
  
  QVariant value;
  
  if (index.data(Qt::UserRole+1) == BOOL)
  {
    QComboBox *cmb = static_cast<QComboBox*>(editor);
    value = cmb->currentText();
  }
  else if (index.data(Qt::UserRole+1) == TIME ||
           index.data(Qt::UserRole+1) == INTEGER ||
           index.data(Qt::UserRole+1) == RESLIMIT ||
           index.data(Qt::UserRole+1) == SIZE)
  {
    QSpinBox *spinBox = static_cast<QSpinBox*>(editor);
    spinBox->interpretText();
    value = spinBox->value();
  }
  else if (index.data(Qt::UserRole+1) == LIST)
  {
    QComboBox *cmb = static_cast<QComboBox*>(editor);
    value = cmb->currentText();
  }
  else if (index.data(Qt::UserRole+1) == MULTILIST)
  {
    QComboBox *cmb = static_cast<QComboBox*>(editor);

    QVariantMap map;
    for(int i = 0; i < cmb->count(); i++)
    {
      if (cmb->itemData(i, Qt::CheckStateRole) == Qt::Checked)
      {
        map[cmb->itemText(i)] = true;
      }
      else
      {
        map[cmb->itemText(i)] = false;
      }
      // qDebug() << "Setting " << cmb->itemText(i) << "to " << map[cmb->itemText(i)];
    }
    model->setData(index, QVariant(map), Qt::UserRole+2);

    QString mapAsString;
    for(QVariantMap::const_iterator iter = map.begin(); iter != map.end(); ++iter)
    {
      if (iter.value() == true && mapAsString.isEmpty())
        mapAsString = QString(iter.key());
      else if (iter.value() == true)
        mapAsString = QString(mapAsString + " " + iter.key());
    }
    value = mapAsString;
  }
  else
  {
    QLineEdit *le = static_cast<QLineEdit*>(editor);
    value = le->text();
  }
  model->setData(index, value, Qt::EditRole);

  QFont reg, bold;
  reg = kcmsystemd::confModel->item(0,0)->font();
  bold = reg;
  bold.setBold(true);
  if (!opt->isDefault())
  {
    kcmsystemd::confModel->item(index.row(),0)->setFont(bold);
    kcmsystemd::confModel->item(index.row(),1)->setFont(bold);
  }
  else
  {
    kcmsystemd::confModel->item(index.row(),0)->setFont(reg);
    kcmsystemd::confModel->item(index.row(),1)->setFont(reg);
  }
}

void ConfDelegate::updateEditorGeometry(QWidget *editor,
    const QStyleOptionViewItem &option, const QModelIndex &/* index */) const
{
    editor->setGeometry(option.rect);
}

