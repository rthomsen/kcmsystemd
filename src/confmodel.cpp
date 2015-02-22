/*******************************************************************************
 * Copyright (C) 2013-2015 Ragnar Thomsen <rthomsen6@gmail.com>                *
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

#include "confmodel.h"
#include "kcmsystemd.h"

ConfModel::ConfModel(QObject *parent)
 : QAbstractTableModel(parent)
{
}

int ConfModel::rowCount(const QModelIndex &) const
{
  return kcmsystemd::confOptList.size();
}

int ConfModel::columnCount(const QModelIndex &) const
{
  return 3;
}

QVariant ConfModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section == 0)
    return QString("Item");
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section == 1)
    return QString("Value");
  return QVariant();
}

QVariant ConfModel::data(const QModelIndex & index, int role) const
{
  if (!index.isValid())
    return QVariant();

  if (role == Qt::DisplayRole)
  {
    if (index.column() == 0)
    {
      if (kcmsystemd::confOptList.at(index.row()).type == SIZE)
        return QString(kcmsystemd::confOptList.at(index.row()).realName + " (MB)");
      else if (kcmsystemd::confOptList.at(index.row()).type == TIME)
        return QString(kcmsystemd::confOptList.at(index.row()).realName + " (" +
                       kcmsystemd::confOptList.at(index.row()).getTimeUnit() + ")");
      else
        return kcmsystemd::confOptList.at(index.row()).realName;
    }
    else if (index.column() == 1)
      return kcmsystemd::confOptList.at(index.row()).getValueAsString();
    else if (index.column() == 2)
      return kcmsystemd::confOptList.at(index.row()).getFilename();
  }
  else if (role == Qt::UserRole && index.column() == 1)
  {
    // Holds the type, used in the delegate to set different
    // types of editor widgets
    return kcmsystemd::confOptList.at(index.row()).type;
  }
  else if (role == Qt::UserRole+1 && index.column() == 1)
  {
    // Holds the uniqueName, used in createEditor in the delegate,
    // to set a pointer to the correct confOption.
    return kcmsystemd::confOptList.at(index.row()).uniqueName;
  }
  else if (role == Qt::UserRole+2 && index.column() == 1)
  {
    // Holds a QVariantMap, used in the delegate to save/retrieve selected
    // values in comboboxes for type MULTILIST
    return QVariant(kcmsystemd::confOptList.at(index.row()).getValue().toMap());
  }
  else if (role == Qt::FontRole && !kcmsystemd::confOptList.at(index.row()).isDefault())
  {
    // Set font to bold if value is not default
    QFont newfont;
    newfont.setBold(true);
    return newfont;
  }
  else if (role == Qt::ToolTipRole)
  {
    return kcmsystemd::confOptList.at(index.row()).toolTip;
  }

  return QVariant();
}

bool ConfModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
  if (!value.isValid())
    return false;

  if (role == Qt::DisplayRole && index.column() == 1)
  {
    kcmsystemd::confOptList[index.row()].setValue(value);
  }
  else if (role == Qt::UserRole+2 && index.column() == 1)
  {
    kcmsystemd::confOptList[index.row()].setValue(value);
  }

  emit dataChanged(index, index);
  return true;
}

Qt::ItemFlags ConfModel::flags ( const QModelIndex & index ) const
{
  Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
  if (index.column() == 1) {
    return flags | Qt::ItemIsEditable;
  }
  return flags;
}
