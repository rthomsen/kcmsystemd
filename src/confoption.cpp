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
  // Used when searching in confOptList using indexOf() or ==
  realName = newName;
  name = newName;
}

// RESLIMIT
confOption::confOption(confFile newFile, QString newName, settingType newType)
{
  file = newFile;
  realName = newName;
  name = QString(newName + "_" + QString::number(file));
  type = newType;
  defVal = "infinity";
  value = defVal;
}

// BOOL and STRING
confOption::confOption(confFile newFile, QString newName, settingType newType, QVariant newDefVal)
{
  active = false;
  file = newFile;
  realName = newName;
  name = QString(newName + "_" + QString::number(file));
  type = newType;
  defVal = newDefVal;
  value = defVal;
}

// SIZE
confOption::confOption(confFile newFile, QString newName, settingType newType, QVariant newDefVal, qulonglong newMaxVal)
{
  active = false;
  file = newFile;
  realName = newName;
  name = QString(newName + "_" + QString::number(file));
  type = newType;
  defVal = newDefVal;
  value = defVal; 
  maxVal = newMaxVal;
}

// INTEGER
confOption::confOption(confFile newFile, QString newName, settingType newType, QVariant newDefVal, qlonglong newMinVal, qlonglong newMaxVal)
{
  file = newFile;
  realName = newName;
  name = QString(newName + "_" + QString::number(file));
  type = newType;
  defVal = newDefVal;
  minVal = newMinVal;
  maxVal = newMaxVal;
  value = defVal;
}

// LIST
confOption::confOption(confFile newFile, QString newName, settingType newType, QVariant newDefVal, QStringList newPossibleVals)
{
  file = newFile;
  active = false;
  realName = newName;
  name = QString(newName + "_" + QString::number(file));
  type = newType;
  defVal = newDefVal;
  possibleVals = newPossibleVals;
  value = defVal;
}

// MULTILIST
confOption::confOption(confFile newFile, QString newName, settingType newType, QStringList newPossibleVals)
{
  file = newFile;
  active = false;
  realName = newName;
  name = QString(newName + "_" + QString::number(file));
  type = newType;
  possibleVals = newPossibleVals;  
  QVariantMap map;
  foreach (QString s, possibleVals)
    map[s] = false;
  defVal = map;
}

// TIME
confOption::confOption(confFile newFile, QString newName, settingType newType, QVariant newDefVal, timeUnit newDefUnit, timeUnit newDefReadUnit, timeUnit newMinUnit, timeUnit newMaxUnit)
{
  file = newFile;
  realName = newName;
  name = QString(newName + "_" + QString::number(file));
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
  // Used to set values in confOptions from a file line
  
  QString rval = line.section("=",1).trimmed();
    
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
      active = true;
      value = rvalToNmbr;
      return 0;
    }
    qDebug() << rval << "is not a valid value for setting" << realName << ". Ignoring...";
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
    if (realName == "ShowStatus")
    {
      if (rval.toLower() == "true" || rval.toLower() == "on")
        rval = "yes";
      else if (rval.toLower() == "false" || rval.toLower() == "off")
        rval = "no";
    }
    if (possibleVals.contains(rval.toLower()))
    {
      active = true;
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
      // Some values with units were found
      // Convert secs to defUnit and set "value"
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
      // See if a unitless number was found
      // If yes, convert the value from defReadUnit to defUnit
      bool ok;
      double rvalDbl = rval.trimmed().toDouble(&ok);
      if (ok)
      {
        value = convertTimeUnit(rvalDbl, defReadUnit, defUnit);
        found = true;
      }
    }
    
    if (found)
      return 0;
    
    qDebug() << rval << "is not a valid value for setting" << realName << ". Ignoring...";
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
      
      // Convert from double to ulonglong
      value = value.toULongLong();
    }
    else
    {
      qDebug() << rval << "is not a valid value for setting" << realName << ". Ignoring...";
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
  if (type == BOOL)
  {
    if (getValue() != defVal)
    {
      if (getValue().toBool())
        return QString(realName + "=" + "yes\n");
      else
        return QString(realName + "=" + "no\n");
    }
    else
      return QString("#" + realName + "=\n");
  }  
  
  else if (type == MULTILIST)
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
        return QString(realName + "=" + ret.trimmed() + "\n");
      }
    }
    else
      return QString("#" + realName + "=\n");
  }
  
  else if (type == TIME)
  {
    if (getValue() != defVal)
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
    else
      return QString("#" + realName + "=\n");
  }
  
  else if (type == SIZE)
  {
    if (getValue() != defVal)
      return QString(realName + "=" + value.toString() + "M\n");
    else
      return QString("#" + realName + "=\n");
  }
  
  if (getValue() != defVal)
    return QString(realName + "=" + value.toString() + "\n");
  
  return QString("#" + realName + "=\n");
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

void confOption::setToDefault()
{
  value = defVal;
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