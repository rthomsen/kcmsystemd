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

#include <QDebug>

#include "confoption.h"

// Initialize two static class members
QStringList confOption::capabilities = QStringList() << "CAP_AUDIT_CONTROL" << "CAP_AUDIT_WRITE" << "CAP_BLOCK_SUSPEND" << "CAP_CHOWN"
                                           << "CAP_DAC_OVERRIDE" << "CAP_DAC_READ_SEARCH" << "CAP_FOWNER" << "CAP_FSETID" 
                                           << "CAP_IPC_LOCK" << "CAP_IPC_OWNER" << "CAP_KILL" << "CAP_LEASE" << "CAP_LINUX_IMMUTABLE"
                                           << "CAP_MAC_ADMIN" << "CAP_MAC_OVERRIDE" << "CAP_MKNOD" << "CAP_NET_ADMIN" << "CAP_NET_BIND_SERVICE"
                                           << "CAP_NET_BROADCAST" << "CAP_NET_RAW" << "CAP_SETGID" << "CAP_SETFCAP" << "CAP_SETPCAP"
                                           << "CAP_SETUID" << "CAP_SYS_ADMIN" << "CAP_SYS_BOOT" << "CAP_SYS_CHROOT" << "CAP_SYS_MODULE"
                                           << "CAP_SYS_NICE" << "CAP_SYS_PACCT" << "CAP_SYS_PTRACE" << "CAP_SYS_RAWIO" << "CAP_SYS_RESOURCE"
                                           << "CAP_SYS_TIME" << "CAP_SYS_TTY_CONFIG" << "CAP_SYSLOG" << "CAP_WAKE_ALARM";

bool confOption::operator==(const confOption& other) const
{
  // Required for indexOf()
  if (uniqueName == other.uniqueName)
    return true;
  else
    return false;
}

confOption::confOption()
{
}

confOption::confOption(QString newName)
{
  // Create an instance necessary for indexOf() or ==
  uniqueName = newName;
}

confOption::confOption(QVariantMap map)
{
  file = static_cast<confFile>(map["file"].toInt());
  realName = map["name"].toString();
  uniqueName = QString(map["name"].toString() + "_" + QString::number(file));
  type = static_cast<settingType>(map["type"].toInt());
  defVal = map["defVal"];
  possibleVals = map["possibleVals"].toStringList();
  if (map.contains("defUnit"))
    defUnit = static_cast<timeUnit>(map["defUnit"].toInt());
  if (map.contains("defReadUnit"))
    defReadUnit = static_cast<timeUnit>(map["defReadUnit"].toInt());
  // These two are not used for anything currently
  // minUnit = static_cast<timeUnit>(map["minUnit"].toInt());
  // maxUnit = static_cast<timeUnit>(map["maxUnit"].toInt());
  if (map.contains("minVal"))
    minVal = map["minVal"].toLongLong();
  if (map.contains("maxVal"))
    maxVal = map["maxVal"].toLongLong();
  toolTip = map["toolTip"].toString();

  hasNsec = map["hasNsec"].toBool();

  if (type == MULTILIST)
  {
    // Create a map where all possibleVals are set to false
    QVariantMap defMap;
    foreach (QString s, possibleVals)
      defMap[s] = false;
    defVal = defMap;
  }
  if (type == RESLIMIT)
    defVal = -1;

  value = defVal;
}

int confOption::setValue(QVariant newVal)
{
  qDebug() << "Setting " << realName << " to " << newVal;
  value = newVal;
  return 0;
}

int confOption::setValueFromFile(QString line)
{
  // Used to set values in confOptions from a file line
  
  QString rval = line.section("=",1).trimmed();

  qDebug() << "setting " << realName << " to " << rval << " (from file)";
    
  if (type == BOOL)
  {
    if (rval == "true" || rval == "on" || rval == "yes")
    {
      value = true;
      return 0;
    }
    else if (rval == "false" || rval == "off" || rval == "no")
    {
      value = false;
      return 0;
    }
    qDebug() << rval << "is not a valid value for setting" << realName << ". Ignoring...";
    return -1;
  }

  else if (type == INTEGER)
  {
    bool ok;
    qlonglong rvalToNmbr = rval.toLongLong(&ok);
    if (ok && rvalToNmbr >= minVal && rvalToNmbr <= maxVal)
    {
      value = rvalToNmbr;
      return 0;
    }
    qDebug() << rval << "is not a valid value for setting" << realName << ". Ignoring...";
    return -1;
  }

  else if (type == STRING)
  {
    value = rval;
    return 0;
  }  
  
  else if (type == LIST)
  {
    if (realName == "ShowStatus") // ShowStatus needs special treatment
    {
      if (rval.toLower() == "true" || rval.toLower() == "on")
        rval = "yes";
      else if (rval.toLower() == "false" || rval.toLower() == "off")
        rval = "no";
    }
    if (possibleVals.contains(rval))
    {
      value = rval.toLower();
      return 0;
    }
    qDebug() << rval << "is not a valid value for setting" << realName << ". Ignoring...";
    value = defVal;
    return -1;
  }

  else if (type == MULTILIST)
  {
    QVariantMap map;

    QStringList readList = rval.split(" ", QString::SkipEmptyParts);
    for (int i = 0; i < readList.size(); ++i)
    {
      if (!possibleVals.contains(readList.at(i)))
      {
        qDebug() << rval << "is not a valid value for setting" << realName << ". Ignoring...";
        return -1;
      }
    }
    
    for (int i = 0; i < possibleVals.size(); ++i)
    {
      if (readList.contains(possibleVals.at(i)))
        map[possibleVals.at(i)] = true;
      else
        map[possibleVals.at(i)] = false;
    }

    value = map;
    return 0;

  }  
  
  else if (type == TIME)
  {
    int pos = 0;
    QRegExp rxValid;

    // These regex check whether rval is a valid time interval
    if (hasNsec)
      rxValid = QRegExp("^(?:\\d*\\.?\\d *(ns|nsec|us|usec|ms|msec|s|sec|second|seconds|m|min|minute|minutes|h|hr|hour|hours|d|day|days|w|week|weeks|month|months|y|year|years)? *)+$");
    else
      rxValid = QRegExp("^(?:\\d*\\.?\\d *(us|usec|ms|msec|s|sec|second|seconds|m|min|minute|minutes|h|hr|hour|hours|d|day|days|w|week|weeks|month|months|y|year|years)? *)+$");

    pos = rxValid.indexIn(rval);

    if (pos > -1)
    {
      pos = 0;
      seconds secs(0);

      // This regex parses individual elements of the time interval
      QRegExp rxTimeParse = QRegExp("(\\d*\\.?\\d+) *([a-z]*)");

      while ((pos = rxTimeParse.indexIn(rval, pos)) != -1)
      {
        if (rxTimeParse.cap(2) == "ns" ||
            rxTimeParse.cap(2) == "nsec" )
        {
          nanoseconds ns(rxTimeParse.cap(1).trimmed().toDouble());
          secs += ns;
        }
        else if (rxTimeParse.cap(2) == "us" ||
            rxTimeParse.cap(2) == "usec" )
        {
          microseconds us(rxTimeParse.cap(1).trimmed().toDouble());
          secs += us;
        }
        else if (rxTimeParse.cap(2) == "ms" ||
            rxTimeParse.cap(2) == "msec" )
        {
          milliseconds ms(rxTimeParse.cap(1).trimmed().toDouble());
          secs += ms;
        }
        else if (rxTimeParse.cap(2) == "s" ||
                 rxTimeParse.cap(2) == "sec" ||
                 rxTimeParse.cap(2) == "second" ||
                 rxTimeParse.cap(2) == "seconds" )
        {
          seconds s(rxTimeParse.cap(1).trimmed().toDouble());
          secs += s;
        }
        else if (rxTimeParse.cap(2) == "m" ||
                 rxTimeParse.cap(2) == "min" ||
                 rxTimeParse.cap(2) == "minute" ||
                 rxTimeParse.cap(2) == "minutes" )
        {
          minutes min(rxTimeParse.cap(1).trimmed().toDouble());
          secs += min;
        }
        else if (rxTimeParse.cap(2) == "h" ||
                 rxTimeParse.cap(2) == "hr" ||
                 rxTimeParse.cap(2) == "hour" ||
                 rxTimeParse.cap(2) == "hours" )
        {
          hours hr(rxTimeParse.cap(1).trimmed().toDouble());
          secs += hr;
        }
        else if (rxTimeParse.cap(2) == "d" ||
                 rxTimeParse.cap(2) == "day" ||
                 rxTimeParse.cap(2) == "days")
        {
          days dy(rxTimeParse.cap(1).trimmed().toDouble());
          secs += dy;
        }
        else if (rxTimeParse.cap(2) == "w" ||
                 rxTimeParse.cap(2) == "week" ||
                 rxTimeParse.cap(2) == "weeks")
        {
          weeks w(rxTimeParse.cap(1).trimmed().toDouble());
          secs += w;
        }
        else if (rxTimeParse.cap(2) == "month" ||
                 rxTimeParse.cap(2) == "months")
        {
          months m(rxTimeParse.cap(1).trimmed().toDouble());
          secs += m;
        }
        else if (rxTimeParse.cap(2) == "y" ||
                 rxTimeParse.cap(2) == "year" ||
                 rxTimeParse.cap(2) == "years")
        {
          years y(rxTimeParse.cap(1).trimmed().toDouble());
          secs += y;
        }

        else if (rxTimeParse.cap(2).isEmpty())
        {
          // unitless number, convert it from defReadUnit to seconds
          seconds tmpSeconds(convertTimeUnit(rxTimeParse.cap(1).trimmed().toDouble(), defReadUnit, timeUnit::s).toDouble());
          secs += tmpSeconds;
        }

        pos += rxTimeParse.matchedLength();
      }

      // Convert the read value in seconds to defUnit
      if (defUnit == ns)
        value = nanoseconds(secs).count();
      else if (defUnit == us)
        value = microseconds(secs).count();
      else if (defUnit == ms)
        value = milliseconds(secs).count();
      else if (defUnit == s)
        value = secs.count();
      else if (defUnit == min)
        value = minutes(secs).count();
      else if (defUnit == h)
        value = hours(secs).count();
      else if (defUnit == d)
        value = days(secs).count();
      else if (defUnit == w)
        value = weeks(secs).count();
      else if (defUnit == month)
        value = months(secs).count();
      else if (defUnit == year)
        value = years(secs).count();

      value = value.toULongLong(); // Convert to ulonglong (we don't support float in ui)
      return 0;

    }
    else
    {
      qDebug() << rval << "is not a valid value for setting" << realName << ". Ignoring...";
      return -1;
    }
  }
  
  else if (type == RESLIMIT)
  {
    bool ok;
    int nmbr = rval.toUInt(&ok);
    if (ok)
    {
      value = nmbr;
      return 0;
    }
    else if (rval.toLower().trimmed() == "infinity" || rval.trimmed().isEmpty())
    {
      value = -1;
      return 0;
    }
    qDebug() << rval << "is not a valid value for setting" << realName << ". Ignoring...";
    return -1;
    
  }
  
  else if (type == SIZE)
  {
    // RegExp to match a number (possibly with decimals) followed by a size unit (or no unit for byte)
    QRegExp rxSize = QRegExp("(\\b\\d+\\.?\\d*(K|M|G|T|P|E)?\\b)");
    
    int pos = 0;
    pos = rxSize.indexIn(rval);
    if (pos > -1 && rxSize.cap(0) == rval.trimmed())
    {
      // convert the specified size unit to megabytes
      if (rxSize.cap(0).contains("K"))
        value = rxSize.cap(0).remove("K").toDouble() / 1024;
      else if (rxSize.cap(0).contains("M"))
        value = rxSize.cap(0).remove("M").toDouble();
      else if (rxSize.cap(0).contains("G"))
        value = rxSize.cap(0).remove("G").toDouble() * 1024;
      else if (rxSize.cap(0).contains("T"))
        value = rxSize.cap(0).remove("T").toDouble() * 1024 * 1024;
      else if (rxSize.cap(0).contains("P"))
        value = rxSize.cap(0).remove("P").toDouble() * 1024 * 1024 * 1024;
      else if (rxSize.cap(0).contains("E"))
        value = rxSize.cap(0).remove("E").toDouble() * 1024 * 1024 * 1024 * 1024;
      else
        value = rxSize.cap(0).toDouble() / 1024 / 1024;
      
      // Convert from double to ulonglong (we don't support float in ui)
      value = value.toULongLong();
      return 0;
    }
    else
    {
      qDebug() << rval << "is not a valid value for setting" << realName << ". Ignoring...";
      return -1;
    }
   
  }
  return -1;
}

bool confOption::isDefault() const
{
  if (value == defVal)
    return true;
  else
    return false;
}

void confOption::setToDefault()
{
  value = defVal;
}

QVariant confOption::getValue() const
{
  return value;
}

QString confOption::getValueAsString() const
{
  // Used when fetching data for the QTableView
  if (type == MULTILIST)
  {
    QVariantMap map = value.toMap();
    QString mapAsString;
    for(QVariantMap::const_iterator iter = map.begin(); iter != map.end(); ++iter)
    {
      if (iter.value() == true && mapAsString.isEmpty())
        mapAsString = QString(iter.key());
      else if (iter.value() == true)
        mapAsString = QString(mapAsString + " " + iter.key());
    }
    return mapAsString;
  }
  return value.toString();
}

QString confOption::getLineForFile() const
{
  // Used for saving to conf files

  if (value == defVal)
  {
    // value is set to default
    return QString("#" + realName + "=\n");
  }
  else
  {
    // value is not default
    if (type == BOOL)
    {
      if (value.toBool())
        return QString(realName + "=" + "yes\n");
      else
        return QString(realName + "=" + "no\n");
    }

    else if (type == MULTILIST)
    {
      if (!value.toMap().isEmpty())
      {
        QVariantMap map = value.toMap();
        QString ret;
        for(QVariantMap::const_iterator iter = map.begin(); iter != map.end(); ++iter)
        {
          if (iter.value().toBool())
            ret = QString(ret + iter.key() + " ");
        }
        return QString(realName + "=" + ret.trimmed() + "\n");
      }
    }

    else if (type == TIME)
    {
      if (value.toULongLong() == 0)
        return QString(realName + "=" + value.toString() + "\n");
      else
      {
        if (defUnit == ns)
          return QString(realName + "=" + value.toString() + "ns\n");
        else if (defUnit == us)
          return QString(realName + "=" + value.toString() + "us\n");
        else if (defUnit == ms)
          return QString(realName + "=" + value.toString() + "ms\n");
        else if (defUnit == s)
          return QString(realName + "=" + value.toString() + "s\n");
        else if (defUnit == min)
          return QString(realName + "=" + value.toString() + "min\n");
        else if (defUnit == h)
          return QString(realName + "=" + value.toString() + "h\n");
        else if (defUnit == d)
          return QString(realName + "=" + value.toString() + "d\n");
        else if (defUnit == w)
          return QString(realName + "=" + value.toString() + "w\n");
        else if (defUnit == month)
          return QString(realName + "=" + value.toString() + "month\n");
        else if (defUnit == year)
          return QString(realName + "=" + value.toString() + "year\n");
       }
    }

    else if (type == SIZE)
    {
      return QString(realName + "=" + value.toString() + "M\n");
    }

    return QString(realName + "=" + value.toString() + "\n");
  } // not default
}

QString confOption::getFilename() const
{
  if (file == SYSTEMD)
    return QString("system.conf");
  else if (file == JOURNALD)
    return QString("journald.conf");
  else if (file == LOGIND)
    return QString("logind.conf");
  else if (file == COREDUMP)
    return QString("coredump.conf");
  return QString("");
}

QString confOption::getTimeUnit() const
{
  QStringList timeUnitAsString = QStringList() << "ns" << "us" << "ms" << "s" <<
                                          "min" << "h" << "days" << "weeks" <<
                                          "month" << "year";
  return timeUnitAsString.at(defUnit);
}

QVariant confOption::convertTimeUnit(double value, timeUnit oldTime, timeUnit newTime)
{
  QVariant convertedVal;
  seconds tmpSecs(0);
  
  if (oldTime == ns)
  {
    nanoseconds val(value);
    tmpSecs = boost::chrono::duration_cast<seconds>(val);
  }
  else if (oldTime == us)
  {
    microseconds val(value);
    tmpSecs = boost::chrono::duration_cast<seconds>(val);
  }
  else if (oldTime == ms)
  {
    milliseconds val(value);
    tmpSecs = boost::chrono::duration_cast<seconds>(val);
  }
  else if (oldTime == s)
  {
    seconds val(value);
    tmpSecs = boost::chrono::duration_cast<seconds>(val);
  }
  else if (oldTime == min)
  {
    minutes val(value);
    tmpSecs = boost::chrono::duration_cast<seconds>(val);
  }
  else if (oldTime == h)
  {
    hours val(value);
    tmpSecs = boost::chrono::duration_cast<seconds>(val);
  }
  else if (oldTime == d)
  {
    days val(value);
    tmpSecs = boost::chrono::duration_cast<seconds>(val);
  }
  else if (oldTime == w)
  {
    weeks val(value);
    tmpSecs = boost::chrono::duration_cast<seconds>(val);
  }
  else if (oldTime == month)
  {
    months val(value);
    tmpSecs = boost::chrono::duration_cast<seconds>(val);
  }
  else if (oldTime == year)
  {
    years val(value);
    tmpSecs = boost::chrono::duration_cast<seconds>(val);
  }
    
  if (newTime == ns)
    convertedVal = boost::chrono::duration_cast<nanoseconds>(tmpSecs).count();
  else if (newTime == us)
    convertedVal = boost::chrono::duration_cast<microseconds>(tmpSecs).count();
  else if (newTime == ms)
    convertedVal = boost::chrono::duration_cast<milliseconds>(tmpSecs).count();
  else if (newTime == s)
    convertedVal = boost::chrono::duration_cast<seconds>(tmpSecs).count();
  else if (newTime == min)
    convertedVal = boost::chrono::duration_cast<minutes>(tmpSecs).count();
  else if (newTime == h)
    convertedVal = boost::chrono::duration_cast<hours>(tmpSecs).count();
  else if (newTime == d)
    convertedVal = boost::chrono::duration_cast<days>(tmpSecs).count();
  else if (newTime == w)
    convertedVal = boost::chrono::duration_cast<weeks>(tmpSecs).count();
  else if (newTime == month)
    convertedVal = boost::chrono::duration_cast<months>(tmpSecs).count();
  else if (newTime == year)
    convertedVal = boost::chrono::duration_cast<years>(tmpSecs).count();

  return convertedVal;
}
