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

#include "sortfilterunitmodel.h"

SortFilterUnitModel::SortFilterUnitModel(QObject *parent)
     : QSortFilterProxyModel(parent)
{
}

void SortFilterUnitModel::initFilterMap(const QMap<filterType, QString> &map)
{
  filtersMap.clear();

  for(QMap<filterType, QString>::const_iterator iter = map.constBegin(); iter != map.constEnd(); ++iter)
  {
    filtersMap[iter.key()] = iter.value();
  }

}

void SortFilterUnitModel::addFilterRegExp(filterType type, const QString &pattern)
{
  if(!filtersMap.contains(type))
    return;

  filtersMap[type] = pattern;

  // qDebug() << "filtersMap changed: " << filtersMap;
}

bool SortFilterUnitModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
  if(filtersMap.isEmpty())
    return true;

  bool ret = false;

  for(QMap<filterType, QString>::const_iterator iter = filtersMap.constBegin(); iter != filtersMap.constEnd(); ++iter)
  {
    QModelIndex index1 = sourceModel()->index(sourceRow, 1, sourceParent);
    QModelIndex index3 = sourceModel()->index(sourceRow, 3, sourceParent);

    if (iter.key() == activeState)
      ret = (index1.data().toString().contains(QRegExp(iter.value())));
    else if (iter.key() == unitType)
      ret = (index3.data().toString().contains(QRegExp(iter.value())));
    else if (iter.key() == unitName)
      ret = (index3.data().toString().contains(QRegExp(iter.value(), Qt::CaseInsensitive)));

    if(!ret)
      return ret;
  }

  return ret;
}
 
