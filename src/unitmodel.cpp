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

#include <QtDBus/QtDBus>
#include <QColor>
#include <KLocalizedString>

#include <systemd/sd-journal.h>

#include "unitmodel.h"

UnitModel::UnitModel(QObject *parent)
 : QAbstractTableModel(parent)
{
}

UnitModel::UnitModel(QObject *parent, const QList<SystemdUnit> *list, QString userBusPath)
 : QAbstractTableModel(parent)
{
  unitList = list;
  userBus = userBusPath;
}

int UnitModel::rowCount(const QModelIndex &) const
{
  return unitList->size();
}

int UnitModel::columnCount(const QModelIndex &) const
{
  return 4;
}

QVariant UnitModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section == 0)
    return QString("Load state");
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section == 1)
    return QString("Active state");
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section == 2)
    return QString("Unit state");
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section == 3)
    return QString("Unit");
  return QVariant();
}

QVariant UnitModel::data(const QModelIndex & index, int role) const
{

  if (!index.isValid())
    return QVariant();

  if (role == Qt::DisplayRole)
  {
    if (index.column() == 0)
      return unitList->at(index.row()).load_state;
    else if (index.column() == 1)
      return unitList->at(index.row()).active_state;
    else if (index.column() == 2)
      return unitList->at(index.row()).sub_state;
    else if (index.column() == 3)
      return unitList->at(index.row()).id;
  }

  else if (role == Qt::ForegroundRole)
  {
    // Update the text color in model
    QColor newcolor;

    if (unitList->at(index.row()).active_state == "active")
      newcolor = Qt::darkGreen;
    else if (unitList->at(index.row()).active_state == "failed")
      newcolor = Qt::darkRed;
    else if (unitList->at(index.row()).active_state == "-")
      newcolor = Qt::darkGray;
    else
      newcolor = Qt::black;

    return QVariant(newcolor);
  }

  else if (role == Qt::ToolTipRole)
  {
    QString selUnit = unitList->at(index.row()).id;
    QString selUnitPath = unitList->at(index.row()).unit_path.path();
    QString selUnitFile = unitList->at(index.row()).unit_file;

    QString toolTipText;
    toolTipText.append("<FONT COLOR=white>");
    toolTipText.append("<b>" + selUnit + "</b><hr>");

    // Create a DBus interface
    QDBusConnection bus("");
    if (!userBus.isEmpty())
      bus = QDBusConnection::connectToBus(userBus, "org.freedesktop.systemd1");
    else
      bus = QDBusConnection::systemBus();
    QDBusInterface *iface;

    // Use the DBus interface to get unit properties
    if (!selUnitPath.isEmpty())
    {
      // Unit has a valid path

      iface = new QDBusInterface ("org.freedesktop.systemd1",
                                  selUnitPath,
                                  "org.freedesktop.systemd1.Unit",
                                  bus);
      if (iface->isValid())
      {
        // Unit has a valid unit DBus object
        toolTipText.append(i18n("<b>Description: </b>"));
        toolTipText.append(iface->property("Description").toString());
        toolTipText.append(i18n("<br><b>Unit file: </b>"));
        toolTipText.append(iface->property("FragmentPath").toString());
        toolTipText.append(i18n("<br><b>Unit file state: </b>"));
        toolTipText.append(iface->property("UnitFileState").toString());

        qulonglong ActiveEnterTimestamp = iface->property("ActiveEnterTimestamp").toULongLong();
        toolTipText.append(i18n("<br><b>Activated: </b>"));
        if (ActiveEnterTimestamp == 0)
          toolTipText.append("n/a");
        else
        {
          QDateTime timeActivated;
          timeActivated.setMSecsSinceEpoch(ActiveEnterTimestamp/1000);
          toolTipText.append(timeActivated.toString());
        }

        qulonglong InactiveEnterTimestamp = iface->property("InactiveEnterTimestamp").toULongLong();
        toolTipText.append(i18n("<br><b>Deactivated: </b>"));
        if (InactiveEnterTimestamp == 0)
          toolTipText.append("n/a");
        else
        {
          QDateTime timeDeactivated;
          timeDeactivated.setMSecsSinceEpoch(InactiveEnterTimestamp/1000);
          toolTipText.append(timeDeactivated.toString());
        }
      }
      delete iface;

    }
    else
    {
      // Unit does not have a valid unit DBus object
      // Retrieve UnitFileState from Manager object

      iface = new QDBusInterface ("org.freedesktop.systemd1",
                                  "/org/freedesktop/systemd1",
                                  "org.freedesktop.systemd1.Manager",
                                  bus);
      QList<QVariant> args;
      args << selUnit;

      toolTipText.append(i18n("<b>Unit file: </b>"));
      if (!selUnitFile.isEmpty())
        toolTipText.append(selUnitFile);

      toolTipText.append(i18n("<br><b>Unit file state: </b>"));
      if (!selUnitFile.isEmpty())
        toolTipText.append(iface->callWithArgumentList(QDBus::AutoDetect, "GetUnitFileState", args).arguments().at(0).toString());

      delete iface;
    }

    // Journal entries for units
    toolTipText.append(i18n("<hr><b>Last log entries:</b>"));
    QStringList log = getLastJrnlEntries(selUnit);
    if (log.isEmpty())
      toolTipText.append(i18n("<br><i>No log entries found for this unit.</i>"));
    else
    {
      for(int i = log.count()-1; i >= 0; --i)
      {
        if (!log.at(i).isEmpty())
          toolTipText.append(QString("<br>" + log.at(i)));
      }
    }

    toolTipText.append("</FONT");

    return toolTipText;
  }

  return QVariant();
}

QStringList UnitModel::getLastJrnlEntries(QString unit) const
{
  QString match1, match2;
  int r, jflags;
  QStringList reply;
  const void *data;
  size_t length;
  uint64_t time;
  sd_journal *journal;

  if (!userBus.isEmpty())
  {
    match1 = QString("USER_UNIT=" + unit);
    jflags = (SD_JOURNAL_LOCAL_ONLY | SD_JOURNAL_CURRENT_USER);
  }
  else
  {
    match1 = QString("_SYSTEMD_UNIT=" + unit);
    match2 = QString("UNIT=" + unit);
    jflags = (SD_JOURNAL_LOCAL_ONLY | SD_JOURNAL_SYSTEM);
  }

  r = sd_journal_open(&journal, jflags);
  if (r != 0)
  {
    qDebug() << "Failed to open journal";
    return reply;
  }

  sd_journal_flush_matches(journal);

  r = sd_journal_add_match(journal, match1.toLatin1(), 0);
  if (r != 0)
    return reply;

  if (!match2.isEmpty())
  {
    sd_journal_add_disjunction(journal);
    r = sd_journal_add_match(journal, match2.toLatin1(), 0);
    if (r != 0)
      return reply;
  }


  r = sd_journal_seek_tail(journal);
  if (r != 0)
    return reply;

  // Fetch the last 5 entries
  for (int i = 0; i < 5; ++i)
  {
    r = sd_journal_previous(journal);
    if (r == 1)
    {
      QString line;

      // Get the date and time
      r = sd_journal_get_realtime_usec(journal, &time);
      if (r == 0)
      {
        QDateTime date;
        date.setMSecsSinceEpoch(time/1000);
        line.append(date.toString("yyyy.MM.dd hh:mm"));
      }

      // Color messages according to priority
      r = sd_journal_get_data(journal, "PRIORITY", &data, &length);
      if (r == 0)
      {
        int prio = QString::fromLatin1((const char *)data, length).section("=",1).toInt();
        if (prio <= 3)
          line.append("<span style='color:tomato;'>");
        else if (prio == 4)
          line.append("<span style='color:khaki;'>");
        else
          line.append("<span style='color:palegreen;'>");
      }

      // Get the message itself
      r = sd_journal_get_data(journal, "MESSAGE", &data, &length);
      if (r == 0)
      {
        line.append(": " + QString::fromLatin1((const char *)data, length).section("=",1) + "</span>");
        if (line.length() > 195)
          line = QString(line.left(195) + "..." + "</span>");
        reply << line;
      }
    }
    else // previous failed, no more entries
      return reply;
  }

  sd_journal_close(journal);

  return reply;
}
