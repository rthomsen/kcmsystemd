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

#ifndef CONFOPTION_H
#define CONFOPTION_H

#include <QStringList>
#include <QVariant>

#include <boost/chrono.hpp>

enum settingType
{
  BOOL, TIME, INTEGER, STRING, LIST, MULTILIST, RESLIMIT, SIZE
};

enum confFile
{
  SYSTEMD, JOURNALD, LOGIND, COREDUMP
};

class confOption {
  
  public:
    typedef boost::chrono::duration<double, boost::ratio<1, 1000000000> > nanoseconds;
    typedef boost::chrono::duration<double, boost::ratio<1, 1000000> > microseconds;
    typedef boost::chrono::duration<double, boost::ratio<1, 1000> > milliseconds;
    typedef boost::chrono::duration<double> seconds;
    typedef boost::chrono::duration<double, boost::ratio<60, 1> > minutes;
    typedef boost::chrono::duration<double, boost::ratio<3600, 1> > hours;
    typedef boost::chrono::duration<double, boost::ratio<86400, 1> > days;
    typedef boost::chrono::duration<double, boost::ratio<604800, 1> > weeks;
    typedef boost::chrono::duration<double, boost::ratio<2629800, 1> > months; // define a month as 30.4375days
    typedef boost::chrono::duration<double, boost::ratio<29030400, 1> > years;
    typedef enum timeUnit { ns, us, ms, s, min, h, d, w, month, year } timeUnit;
    
    confFile file;
    settingType type;
    QString uniqueName, realName, toolTip;
    qlonglong minVal = 0, maxVal = 999999999;
    QStringList possibleVals = QStringList();
    static QStringList capabilities;
    
    confOption();
    // Used for comparing
    confOption(QString newName);
    confOption(QVariantMap);
    
    bool operator==(const confOption& other) const;
    int setValue(QVariant);
    int setValueFromFile(QString);
    bool isDefault() const;
    void setToDefault();
    QVariant getValue() const;
    QString getValueAsString() const;
    QString getLineForFile() const;
    QString getFilename() const;
    QString getTimeUnit() const;

  private:
    bool hasNsec = false;
    QVariant value, defVal;
    timeUnit defUnit = timeUnit::s, defReadUnit = timeUnit::s, minUnit = timeUnit::ns, maxUnit = timeUnit::year;
    QVariant convertTimeUnit(double, timeUnit, timeUnit);
};

#endif
