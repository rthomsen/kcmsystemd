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

#include <QDebug>

#include "confoption.h"

QStringList confOption::capabilities = QStringList() << "CAP_AUDIT_CONTROL" << "CAP_AUDIT_WRITE" << "CAP_BLOCK_SUSPEND" << "CAP_CHOWN"
                                           << "CAP_DAC_OVERRIDE" << "CAP_DAC_READ_SEARCH" << "CAP_FOWNER" << "CAP_FSETID" 
                                           << "CAP_IPC_LOCK" << "CAP_IPC_OWNER" << "CAP_KILL" << "CAP_LEASE" << "CAP_LINUX_IMMUTABLE"
                                           << "CAP_MAC_ADMIN" << "CAP_MAC_OVERRIDE" << "CAP_MKNOD" << "CAP_NET_ADMIN" << "CAP_NET_BIND_SERVICE"
                                           << "CAP_NET_BROADCAST" << "CAP_NET_RAW" << "CAP_SETGID" << "CAP_SETFCAP" << "CAP_SETPCAP"
                                           << "CAP_SETUID" << "CAP_SYS_ADMIN" << "CAP_SYS_BOOT" << "CAP_SYS_CHROOT" << "CAP_SYS_MODULE"
                                           << "CAP_SYS_NICE" << "CAP_SYS_PACCT" << "CAP_SYS_PTRACE" << "CAP_SYS_RAWIO" << "CAP_SYS_RESOURCE"
                                           << "CAP_SYS_TIME" << "CAP_SYS_TTY_CONFIG" << "CAP_SYSLOG" << "CAP_WAKE_ALARM";
QVariantMap confOption::resLimitsMap = QVariantMap();

bool confOption::operator==(const confOption& other) const
{
  if (name == other.name)
    return true;
  else
    return false;
}

confOption::confOption()
{
}

confOption::confOption(QString newName)
{
  name = newName;
}

confOption::confOption(confFile newFile, QString newName, settingType newType)
{
  file = newFile;
  name = newName;
  type = newType;
  defVal = "infinity";
  value = defVal;
}

confOption::confOption(confFile newFile, QString newName, settingType newType, QVariant newDefVal)
{
  active = false;
  file = newFile;
  name = newName;
  type = newType;
  defVal = newDefVal;
  value = defVal;
}

confOption::confOption(confFile newFile, QString newName, settingType newType, QVariant newDefVal, qlonglong newMinVal, qlonglong newMaxVal)
{
  file = newFile;
  name = newName;
  type = newType;
  defVal = newDefVal;
  minVal = newMinVal;
  maxVal = newMaxVal;
  value = defVal;
}

confOption::confOption(confFile newFile, QString newName, settingType newType, QVariant newDefVal, QStringList newPossibleVals)
{
  file = newFile;
  active = false;
  name = newName;
  type = newType;
  defVal = newDefVal;
  possibleVals = newPossibleVals;
  value = defVal;
}

confOption::confOption(confFile newFile, QString newName, settingType newType, QStringList newPossibleVals)
{
  file = newFile;
  active = false;
  name = newName;
  type = newType;
  possibleVals = newPossibleVals;  
  QVariantMap map;
  foreach (QString s, possibleVals)
    map[s] = false;
  defVal = map;
}

confOption::confOption(confFile newFile, QString newName, settingType newType, QVariant newDefVal, timeUnit newDefUnit, timeUnit newDefReadUnit, timeUnit newMinUnit, timeUnit newMaxUnit)
{
  file = newFile;
  name = newName;
  type = newType;
  defVal = newDefVal;
  defReadUnit = newDefReadUnit;
  defUnit = newDefUnit,
  minUnit = newMinUnit;
  maxUnit = newMaxUnit;
  value = defVal;
}

int confOption::setValueFromFile(QString line)
{
  QString rval = line.section("=",1).trimmed();
  
  if (type == BOOL)
  {
    if (rval == "true" || rval == "on" || rval == "yes")
    {
      active = true;
      value = true;
      return 0;
    }
    else if (rval == "false" || rval == "off" || rval == "no")
    {
      active = true;
      value = false;
      return 0;
    }
    qDebug() << rval << "is not a valid value for setting" << name << ". Ignoring...";
    return -1;
  }

  else if (type == INTEGER)
  {
    bool ok;
    qlonglong rvalToNmbr = rval.toLongLong(&ok);
    if (ok && rvalToNmbr >= minVal && rvalToNmbr <= maxVal)
    {
      active = true;
      value = rvalToNmbr;
      return 0;
    }
    qDebug() << rval << "is not a valid value for setting" << name << ". Ignoring...";
    return -1;
  }

  else if (type == STRING)
  {
    active = true;
    value = rval;
    return 0;
  }  
  
  else if (type == LIST)
  {
    if (possibleVals.contains(rval.toLower()))
    {
      active = true;
      value = rval.toLower();
      return 0;
    }
    qDebug() << rval << "is not a valid value for setting" << name << ". Ignoring...";
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
        qDebug() << rval << "is not a valid value for setting" << name << ". Ignoring...";
        return -1;
      }
    }
    
    for (int i = 0; i < possibleVals.size(); ++i)
    {
      if (readList.contains(possibleVals.at(i)))
      {
        map[possibleVals.at(i)] = true;
      }
      else
      {
        map[possibleVals.at(i)] = false;
      }
    }
    
    active = true;
    value = map;
    return 0;

  }  
  
  else if (type == TIME)
  {
    int pos = 0;
    bool found = false;
    
    QRegExp rxNanoseconds = QRegExp("\\b\\d*\\.?\\d+ns\\b");
    QRegExp rxMicroseconds = QRegExp("\\b\\d*\\.?\\d+us\\b");
    QRegExp rxMilliseconds = QRegExp("\\b\\d*\\.?\\d+ms\\b");
    QRegExp rxSeconds = QRegExp("\\b\\d*\\.?\\d+s\\b");
    QRegExp rxMinutes = QRegExp("(\\b\\d*\\.?\\d+min\\b)|(\\b\\d*\\.*\\d+m\\b)");
    QRegExp rxHours = QRegExp("\\b\\d*\\.?\\d+h\\b");
    QRegExp rxDays = QRegExp("(\\b\\d*\\.?\\d+day\\b)|(\\b\\d*\\.?\\d+d\\b)");
    QRegExp rxWeeks = QRegExp("(\\b\\d*\\.?\\d+week\\b)|(\\b\\d*\\.*\\d+w\\b)");
    QRegExp rxMonths = QRegExp("\\b\\d*\\.?\\d+month\\b");
    QRegExp rxYears = QRegExp("\\b\\d*\\.?\\d+year\\b");
    
    seconds secs(0);
    
    if (rval.contains(rxNanoseconds))
    {
      pos = rxNanoseconds.indexIn(rval);
      if (pos > -1)
      {
        nanoseconds ns(rxNanoseconds.cap(0).remove("ns").toDouble());
        secs += ns;
      }       
    }

    if (rval.contains(rxMicroseconds))
    {
      pos = rxMicroseconds.indexIn(rval);
      if (pos > -1)
      {
        microseconds us(rxMicroseconds.cap(0).remove("us").toDouble());
        secs += us;
      }
    }    

    if (rval.contains(rxMilliseconds))
    {
      pos = rxMilliseconds.indexIn(rval);
      if (pos > -1)
      {
        milliseconds ms(rxMilliseconds.cap(0).remove("ms").toDouble());
        secs += ms;
      }
    }
    
    if (rval.contains(rxSeconds))
    {
      pos = rxSeconds.indexIn(rval);
      if (pos > -1)
      {
        seconds s(rxSeconds.cap(0).remove("s").toDouble());
        secs += s;
      }
    }

    if (rval.contains(rxMinutes))
    {
      pos = rxMinutes.indexIn(rval);
      if (pos > -1)
      {
        minutes min(rxMinutes.cap(0).remove("min").remove("m").toDouble());
        secs += min;
      }
    }

    if (rval.contains(rxHours))
    {
      pos = rxHours.indexIn(rval);
      if (pos > -1)
      {
        hours hr(rxHours.cap(0).remove("h").toDouble());
        secs += hr;
      }
    }
    
    if (rval.contains(rxDays))
    {
      pos = rxDays.indexIn(rval);
      if (pos > -1)
      {
        days dy(rxDays.cap(0).remove("d").toDouble());
        secs += dy;
      }
    }
    
    if (rval.contains(rxWeeks))
    {
      pos = rxWeeks.indexIn(rval);
      if (pos > -1)
      {
        weeks wk(rxWeeks.cap(0).remove("week").remove("w").toDouble());
        secs += wk;
      }
    }
    
    if (rval.contains(rxMonths))
    {
      pos = rxMonths.indexIn(rval);
      if (pos > -1)
      {
        months mnth(rxMonths.cap(0).remove("month").toDouble());
        secs += mnth;
      }
    }
    
    if (rval.contains(rxYears))
    {
      pos = rxYears.indexIn(rval);
      if (pos > -1)
      {
        years yr(rxYears.cap(0).remove("year").toDouble());
        secs += yr;
      }
    }    
    
    
    if (secs != seconds(0))
    {
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
      found = true;
    }
    else
    {
      bool ok;
      double rvalDbl = rval.trimmed().toDouble(&ok);
      if (ok)
      {  
        if (defReadUnit == ns)
          value = nanoseconds(rvalDbl).count();
        else if (defReadUnit == us)
          value = microseconds(rvalDbl).count();
        else if (defReadUnit == ms)
          value = milliseconds(rvalDbl).count();
        else if (defReadUnit == s)
          value = seconds(rvalDbl).count();
        else if (defReadUnit == min)
          value = minutes(rvalDbl).count();
        else if (defReadUnit == h)
          value = hours(rvalDbl).count();
        else if (defReadUnit == d)
          value = days(rvalDbl).count();
        else if (defReadUnit == w)
          value = weeks(rvalDbl).count();
        else if (defReadUnit == month)
          value = months(rvalDbl).count();
        else if (defReadUnit == year)
          value = years(rvalDbl).count();
        found = true;
      }
    }
    
    if (found)
    {
      active = true;
      return 0;
    }
    qDebug() << rval << "is not a valid value for setting" << name << ". Ignoring...";
    return -1;
    
  }
  
  else if (type == RESLIMIT)
  {
    bool ok;
    int nmbr = rval.toUInt(&ok);
    if (ok)
    {
      active = true;
      value = nmbr;
      resLimitsMap[name] = value;
      return 0;
    }
    else if (rval.toLower().trimmed() == "infinity" || rval.trimmed().isEmpty())
    {
      active = true;
      value = "infinity";
      resLimitsMap[name] = value;
      return 0;
    }
    qDebug() << rval << "is not a valid value for setting" << name << ". Ignoring...";
    return -1;
    
  }
  
  else if (type == SIZE)
  {
    // RegExp to detect size units
    QRegExp rxSize = QRegExp("(\\b\\d+\\b)|(\\b\\d+K\\b)|(\\b\\d+M\\b)|(\\b\\d+G\\b)|(\\b\\d+T\\b)|(\\b\\d+P\\b)|(\\b\\d+E\\b)");
    int pos = 0;
    
    pos = rxSize.indexIn(rval);
    if (pos > -1)
    {
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
      
      value = value.toULongLong();
      
    }
    else
    {
      qDebug() << rval << "is not a valid value for setting" << name << ". Ignoring...";
      return -1;
    }
   
  }
  return -1;
}

int confOption::setValue(QVariant variant)
{
  value = variant;
  return 0;
}

QVariant confOption::getValue() const
{
  return value;
}

QString confOption::getLineForFile() const
{
  // Used for saving to conf files
  if (type == MULTILIST)
  {
    if (active && getValue() != defVal)
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
        return QString(name + "=" + ret.trimmed() + "\n");
      }
    }
    else
      return QString("#" + name + "=\n");
  }
  else if (type == TIME)
  {
    if (active && getValue() != defVal)
    {
      if (value.toULongLong() == 0)
        return QString(name + "=" + value.toString() + "\n");
      else
      {
        if (defUnit == ns)
          return QString(name + "=" + value.toString() + "ns\n");
        else if (defUnit == us)
          return QString(name + "=" + value.toString() + "us\n");
        else if (defUnit == ms)
          return QString(name + "=" + value.toString() + "ms\n");
        else if (defUnit == s)
          return QString(name + "=" + value.toString() + "s\n");
        else if (defUnit == min)
          return QString(name + "=" + value.toString() + "min\n");
        else if (defUnit == h)
          return QString(name + "=" + value.toString() + "h\n");
        else if (defUnit == d)
          return QString(name + "=" + value.toString() + "d\n");
        else if (defUnit == w)
          return QString(name + "=" + value.toString() + "w\n");
        else if (defUnit == month)
          return QString(name + "=" + value.toString() + "month\n");
        else if (defUnit == year)
          return QString(name + "=" + value.toString() + "year\n");
       }
    }
    else
      return QString("#" + name + "=\n");
  }
  else if (type == SIZE)
  {
    if (active && getValue() != defVal)
      return QString(name + "=" + value.toString() + "M\n");
    else
      return QString("#" + name + "=\n");
      
  }
  
  if (getValue() != defVal)
    return QString(name + "=" + value.toString() + "\n");
  
  return QString("#" + name + "=\n");
}

void confOption::setResLimitsMap(QVariantMap map)
{
  resLimitsMap.clear();
  resLimitsMap = map;
}

bool confOption::isDefault() const
{
  if (value == defVal)
    return true;
  else
    return false;
}
