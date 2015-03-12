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

#include "kcmsystemd.h"
#include <config.h>

#include <QMouseEvent>
#include <QMenu>
#include <QThread>

#include <KAboutData>
#include <KPluginFactory>
#include <KMessageBox>
#include <KMimeTypeTrader>
#include <KAuth>
using namespace KAuth;

#include <boost/filesystem.hpp>

// Static members
ConfModel *kcmsystemd::confModel = new ConfModel();
QList<confOption> kcmsystemd::confOptList;

K_PLUGIN_FACTORY(kcmsystemdFactory, registerPlugin<kcmsystemd>();)

kcmsystemd::kcmsystemd(QWidget *parent, const QVariantList &args) : KCModule(parent, args)
{
  KAboutData *about = new KAboutData("kcmsystemd",
                                     "kcmsystemd",
                                     KCM_SYSTEMD_VERSION,
                                     "KDE Systemd Control Module", 
                                     KAboutLicense::GPL_V3,
                                     "Copyright (C) 2013 Ragnar Thomsen", QString(),
                                     "https://github.com/rthomsen/kcmsystemd",
                                     "https://github.com/rthomsen/kcmsystemd/issues");
  about->addAuthor("Ragnar Thomsen", "Main Developer", "rthomsen6@gmail.com");
  setAboutData(about);
  ui.setupUi(this);
  setButtons(kcmsystemd::Default | kcmsystemd::Apply);
  setNeedsAuthorization(true);
  ui.leSearchUnit->setFocus();

  // See if systemd is reachable via dbus
  if (getDbusProperty("Version", sysdMgr) != "invalidIface")
  {
    systemdVersion = getDbusProperty("Version", sysdMgr).toString().remove("systemd ").toInt();
    qDebug() << "Detected systemd" << systemdVersion;
  }
  else
  {
    qDebug() << "Unable to contact systemd daemon!";
    ui.stackedWidget->setCurrentIndex(1);
  }

  // Search for user bus
  if (QFile("/run/user/" + QString::number(getuid()) + "/bus").exists())
    userBusPath = "unix:path=/run/user/" + QString::number(getuid()) + "/bus";
  else if (QFile("/run/user/" + QString::number(getuid()) + "/dbus/user_bus_socket").exists())
    userBusPath = "unix:path=/run/user/" + QString::number(getuid()) + "/dbus/user_bus_socket";
  else
  {
    qDebug() << "User bus not found. Support for user units disabled.";
    ui.tabWidget->setTabEnabled(1,false);
    enableUserUnits = false;
  }

  // Use kf5-config to get kde prefix
  kdeConfig = new QProcess(this);
  connect(kdeConfig, SIGNAL(readyReadStandardOutput()), this, SLOT(slotKdeConfig()));
  kdeConfig->start("kf5-config", QStringList() << "--prefix");
  
  // Find the configuration directory
  if (QDir("/etc/systemd").exists()) {
    etcDir = "/etc/systemd";
  } else if (QDir("/usr/etc/systemd").exists()) {
    etcDir = "/usr/etc/systemd";
  } else {
    // Failed to find systemd config directory
    ui.stackedWidget->setCurrentIndex(1);    
    ui.lblFailMessage->setText(i18n("Unable to find directory with systemd configuration files."));
    return;
  }
  listConfFiles  << "system.conf"
                 << "journald.conf"
                 << "logind.conf";
  if (systemdVersion >= 215)
    listConfFiles << "coredump.conf";
  
  // Use boost to find persistent partition size
  boost::filesystem::path pp ("/var/log");
  boost::filesystem::space_info logPpart = boost::filesystem::space(pp);
  partPersSizeMB = logPpart.capacity / 1024 / 1024;
  
  // Use boost to find volatile partition size
  boost::filesystem::path pv ("/run/log");
  boost::filesystem::space_info logVpart = boost::filesystem::space(pv);
  partVolaSizeMB = logVpart.capacity / 1024 / 1024;
  qDebug() << "Persistent partition size found to: " << partPersSizeMB << "MB";
  qDebug() << "Volatile partition size found to: " << partVolaSizeMB << "MB";
  
  setupConfigParms();
  setupSignalSlots();
  
  // Subscribe to dbus signals from systemd system daemon and connect them to slots
  callDbusMethod("Subscribe", sysdMgr);
  systembus.connect(connSystemd, pathSysdMgr, ifaceMgr, "Reloading", this, SLOT(slotSystemSystemdReloading(bool)));
  // systembus.connect(connSystemd, pathSysdMgr, ifaceMgr, "UnitNew", this, SLOT(slotUnitLoaded(QString, QDBusObjectPath)));
  // systembus.connect(connSystemd,pathSysdMgr, ifaceMgr, "UnitRemoved", this, SLOT(slotUnitUnloaded(QString, QDBusObjectPath)));
  systembus.connect(connSystemd, pathSysdMgr, ifaceMgr, "UnitFilesChanged", this, SLOT(slotSystemUnitsChanged()));
  systembus.connect(connSystemd, "", ifaceDbusProp, "PropertiesChanged", this, SLOT(slotSystemUnitsChanged()));
  // We need to use the JobRemoved signal, because stopping units does not emit PropertiesChanged signal
  systembus.connect(connSystemd, pathSysdMgr, ifaceMgr, "JobRemoved", this, SLOT(slotSystemUnitsChanged()));

  // Subscribe to dbus signals from systemd user daemon and connect them to slots
  callDbusMethod("Subscribe", sysdMgr, user);
  QDBusConnection userbus = QDBusConnection::connectToBus(userBusPath, connSystemd);
  userbus.connect(connSystemd, pathSysdMgr, ifaceMgr, "Reloading", this, SLOT(slotUserSystemdReloading(bool)));
  userbus.connect(connSystemd, pathSysdMgr, ifaceMgr, "UnitFilesChanged", this, SLOT(slotUserUnitsChanged()));
  userbus.connect(connSystemd, "", ifaceDbusProp, "PropertiesChanged", this, SLOT(slotUserUnitsChanged()));
  userbus.connect(connSystemd, pathSysdMgr, ifaceMgr, "JobRemoved", this, SLOT(slotUserUnitsChanged()));

  // logind
  systembus.connect(connLogind, "", ifaceDbusProp, "PropertiesChanged", this, SLOT(slotLogindPropertiesChanged(QString, QVariantMap, QStringList)));
  
  // Get list of units
  slotRefreshUnitsList(true, sys);
  slotRefreshUnitsList(true, user);

  setupUnitslist();
  setupConf();
  setupSessionlist();
  setupTimerlist();
}

kcmsystemd::~kcmsystemd()
{
}

QDBusArgument &operator<<(QDBusArgument &argument, const SystemdUnit &unit)
{
  argument.beginStructure();
  argument << unit.id
     << unit.description
     << unit.load_state
     << unit.active_state
     << unit.sub_state
     << unit.following
     << unit.unit_path
     << unit.job_id
     << unit.job_type
     << unit.job_path;
  argument.endStructure();
  return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, SystemdUnit &unit)
{
     argument.beginStructure();
     argument >> unit.id
        >> unit.description
        >> unit.load_state
        >> unit.active_state
        >> unit.sub_state
        >> unit.following
        >> unit.unit_path
        >> unit.job_id
        >> unit.job_type
        >> unit.job_path;
     argument.endStructure();
     return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const SystemdSession &session)
{
  argument.beginStructure();
  argument << session.session_id
     << session.user_id
     << session.user_name
     << session.seat_id
     << session.session_path;
  argument.endStructure();
  return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, SystemdSession &session)
{
     argument.beginStructure();
     argument >> session.session_id
        >> session.user_id
        >> session.user_name
        >> session.seat_id
        >> session.session_path;
     argument.endStructure();
     return argument;
}

void kcmsystemd::setupSignalSlots()
{
  // Connect signals for unit tabs
  connect(ui.chkInactiveUnits, SIGNAL(stateChanged(int)), this, SLOT(slotChkShowUnits(int)));
  connect(ui.chkUnloadedUnits, SIGNAL(stateChanged(int)), this, SLOT(slotChkShowUnits(int)));
  connect(ui.chkInactiveUserUnits, SIGNAL(stateChanged(int)), this, SLOT(slotChkShowUnits(int)));
  connect(ui.chkUnloadedUserUnits, SIGNAL(stateChanged(int)), this, SLOT(slotChkShowUnits(int)));
  connect(ui.cmbUnitTypes, SIGNAL(currentIndexChanged(int)), this, SLOT(slotCmbUnitTypes(int)));
  connect(ui.cmbUserUnitTypes, SIGNAL(currentIndexChanged(int)), this, SLOT(slotCmbUnitTypes(int)));
  connect(ui.tblUnits, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(slotUnitContextMenu(QPoint)));
  connect(ui.tblUserUnits, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(slotUnitContextMenu(QPoint)));
  connect(ui.leSearchUnit, SIGNAL(textChanged(QString)), this, SLOT(slotLeSearchUnitChanged(QString)));
  connect(ui.leSearchUserUnit, SIGNAL(textChanged(QString)), this, SLOT(slotLeSearchUnitChanged(QString)));

  // Connect signals for sessions tab
  connect(ui.tblSessions, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(slotSessionContextMenu(QPoint)));

  // Connect signals for conf tab
  connect(ui.cmbConfFile, SIGNAL(currentIndexChanged(int)), this, SLOT(slotCmbConfFileChanged(int)));
}

void kcmsystemd::load()
{
  // Only populate comboboxes once
  if (timesLoad == 0)
  {
    QStringList allowUnitTypes = QStringList() << i18n("All") << i18n("Targets") << i18n("Services")
                                               << i18n("Devices") << i18n("Mounts") << i18n("Automounts") << i18n("Swaps")
                                               << i18n("Sockets") << i18n("Paths") << i18n("Timers") << i18n("Snapshots")
                                               << i18n("Slices") << i18n("Scopes");
    ui.cmbUnitTypes->addItems(allowUnitTypes);
    ui.cmbUserUnitTypes->addItems(allowUnitTypes);
    ui.cmbConfFile->addItems(listConfFiles);
  }
  timesLoad = timesLoad + 1;
  
  // Set all confOptions to default
  // This is needed to clear user changes when resetting the KCM
  for (int i = 0; i < confOptList.size(); ++i)
  {
    confOptList[i].setToDefault();
  }

  // Read the four configuration files
  for (int i = 0; i < listConfFiles.size(); ++i)
  {
    readConfFile(i);
  }

  // Connect signals to slots, which need to be after initializeInterface()
  connect(confModel, SIGNAL(dataChanged(const QModelIndex &, const QModelIndex &)), this, SLOT(slotConfChanged(const QModelIndex &, const QModelIndex &)));
}

void kcmsystemd::setupConfigParms()
{
  // Creates an instance of confOption for each option in the systemd configuration
  // files and adds them to confOptList. Arguments are passed as a QVariantMap.

  QVariantMap map;

  // system.conf

  map.clear();
  map["name"] = "LogLevel";
  map["file"] = SYSTEMD;
  map["type"] = LIST;
  map["defVal"] = "info";
  map["possibleVals"] = QStringList() << "emerg" << "alert" << "crit" << "err" << "warning" << "notice" << "info" << "debug";
  map["toolTip"] = i18n("<p>Set the level of log entries in systemd.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "LogTarget";
  map["file"] = SYSTEMD;
  map["type"] = LIST;
  map["defVal"] = "journal-or-kmsg";
  map["possibleVals"] = QStringList() <<  "console" << "journal" << "syslog" << "kmsg" << "journal-or-kmsg" << "syslog-or-kmsg" << "null";
  map["toolTip"] = i18n("<p>Set target for logs.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "LogColor";
  map["file"] = SYSTEMD;
  map["type"] = BOOL;
  map["defVal"] = true;
  map["toolTip"] = i18n("<p>Use color to highlight important log messages.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "LogLocation";
  map["file"] = SYSTEMD;
  map["type"] = BOOL;
  map["defVal"] = true;
  map["toolTip"] = i18n("<p>Include code location in log messages.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "DumpCore";
  map["file"] = SYSTEMD;
  map["type"] = BOOL;
  map["defVal"] = true;
  map["toolTip"] = i18n("<p>Dump core on systemd crash.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "CrashShell";
  map["file"] = SYSTEMD;
  map["type"] = BOOL;
  map["defVal"] = false;
  map["toolTip"] = i18n("<p>Spawn a shell when systemd crashes. Note: The shell is not password-protected.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "ShowStatus";
  map["file"] = SYSTEMD;
  map["type"] = LIST;
  map["defVal"] = "yes";
  map["possibleVals"] = QStringList() << "yes" << "no" << "auto";
  map["toolTip"] = i18n("<p>Show terse service status information while booting.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "CrashChVT";
  map["file"] = SYSTEMD;
  map["type"] = LIST;
  map["defVal"] = "-1";
  map["possibleVals"] = QStringList() << "-1" << "1" << "2" << "3" << "4" << "5" << "6" << "7" << "8";
  map["toolTip"] = i18n("<p>Activate the specified virtual terminal when systemd crashes (-1 to deactivate).</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "CPUAffinity";
  map["file"] = SYSTEMD;
  map["type"] = MULTILIST;
  QStringList CPUAffinityList;
  for (int i = 1; i <= QThread::idealThreadCount(); ++i)
    CPUAffinityList << QString::number(i);
  map["possibleVals"] = CPUAffinityList;
  map["toolTip"] = i18n("<p>The initial CPU affinity for the systemd init process.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "JoinControllers";
  map["file"] = SYSTEMD;
  map["type"] = STRING;
  map["defVal"] = "cpu,cpuacct net_cls,net_prio";
  map["toolTip"] = i18n("<p>Controllers that shall be mounted in a single hierarchy. Takes a space-separated list of comma-separated controller names, in order to allow multiple joined hierarchies. Pass an empty string to ensure that systemd mounts all controllers in separate hierarchies.</p>");
  confOptList.append(confOption(map));

  if (systemdVersion < 208)
  {
    map.clear();
    map["name"] = "DefaultControllers";
    map["file"] = SYSTEMD;
    map["type"] = STRING;
    map["defVal"] = "cpu";
    map["toolTip"] = i18n("<p>Configures in which control group hierarchies to create per-service cgroups automatically, in addition to the name=systemd named hierarchy. Takes a space-separated list of controller names. Pass the empty string to ensure that systemd does not touch any hierarchies but its own.</p>");
    confOptList.append(confOption(map));
  }

  map.clear();
  map["name"] = "RuntimeWatchdogSec";
  map["file"] = SYSTEMD;
  map["type"] = TIME;
  map["defVal"] = 0;
  map["toolTip"] = i18n("<p>The watchdog hardware (/dev/watchdog) will be programmed to automatically reboot the system if it is not contacted within the specified timeout interval. The system manager will ensure to contact it at least once in half the specified timeout interval. This feature requires a hardware watchdog device.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "ShutdownWatchdogSec";
  map["file"] = SYSTEMD;
  map["type"] = TIME;
  map["defVal"] = 10;
  map["defUnit"] = confOption::min;
  map["toolTip"] = i18n("<p>This setting may be used to configure the hardware watchdog when the system is asked to reboot. It works as a safety net to ensure that the reboot takes place even if a clean reboot attempt times out. This feature requires a hardware watchdog device.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "CapabilityBoundingSet";
  map["file"] = SYSTEMD;
  map["type"] = MULTILIST;
  map["possibleVals"] = confOption::capabilities;
  map["toolTip"] = i18n("<p>Capabilities for the systemd daemon and its children.</p>");
  confOptList.append(confOption(map));

  if (systemdVersion >= 209)
  {
    map.clear();
    map["name"] = "SystemCallArchitectures";
    map["file"] = SYSTEMD;
    map["type"] = MULTILIST;
    map["possibleVals"] = QStringList() << "native" << "x86" << "x86-64" << "x32" << "arm";
    map["toolTip"] = i18n("<p>Architectures of which system calls may be invoked on the system.</p>");
    confOptList.append(confOption(map));
  }

  map.clear();
  map["name"] = "TimerSlackNSec";
  map["file"] = SYSTEMD;
  map["type"] = TIME;
  map["defVal"] = 0;
  map["defUnit"] = confOption::ns;
  map["defReadUnit"] = confOption::ns;
  map["hasNsec"] = true;
  map["toolTip"] = i18n("<p>Sets the timer slack for PID 1 which is then inherited to all executed processes, unless overridden individually. The timer slack controls the accuracy of wake-ups triggered by timers.</p>");
  confOptList.append(confOption(map));

  if (systemdVersion >= 212)
  {
    map.clear();
    map["name"] = "DefaultTimerAccuracySec";
    map["file"] = SYSTEMD;
    map["type"] = TIME;
    map["defVal"] = 60;
    map["toolTip"] = i18n("<p>The default accuracy of timer units. Note that the accuracy of timer units is also affected by the configured timer slack for PID 1.</p>");
    confOptList.append(confOption(map));
  }

  map.clear();
  map["name"] = "DefaultStandardOutput";
  map["file"] = SYSTEMD;
  map["type"] = LIST;
  map["defVal"] = "journal";
  map["possibleVals"] = QStringList() << "inherit" << "null" << "tty" << "journal"
                                      << "journal+console" << "syslog" << "syslog+console"
                                      << "kmsg" << "kmsg+console";
  map["toolTip"] = i18n("<p>Sets the default output for all services and sockets.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "DefaultStandardError";
  map["file"] = SYSTEMD;
  map["type"] = LIST;
  map["defVal"] = "inherit";
  map["possibleVals"] = QStringList() << "inherit" << "null" << "tty" << "journal"
                                      << "journal+console" << "syslog" << "syslog+console"
                                      << "kmsg" << "kmsg+console";
  map["toolTip"] = i18n("<p>Sets the default error output for all services and sockets.</p>");
  confOptList.append(confOption(map));

  if (systemdVersion >= 209)
  {
    map.clear();
    map["name"] = "DefaultTimeoutStartSec";
    map["file"] = SYSTEMD;
    map["type"] = TIME;
    map["defVal"] = 90;
    map["toolTip"] = i18n("<p>The default timeout for starting of units.</p>");
    confOptList.append(confOption(map));

    map.clear();
    map["name"] = "DefaultTimeoutStopSec";
    map["file"] = SYSTEMD;
    map["type"] = TIME;
    map["defVal"] = 90;
    map["toolTip"] = i18n("<p>The default timeout for stopping of units.</p>");
    confOptList.append(confOption(map));

    map.clear();
    map["name"] = "DefaultRestartSec";
    map["file"] = SYSTEMD;
    map["type"] = TIME;
    map["defVal"] = 100;
    map["defUnit"] = confOption::ms;
    map["toolTip"] = i18n("<p>The default time to sleep between automatic restart of units.</p>");
    confOptList.append(confOption(map));

    map.clear();
    map["name"] = "DefaultStartLimitInterval";
    map["file"] = SYSTEMD;
    map["type"] = TIME;
    map["defVal"] = 10;
    map["toolTip"] = i18n("<p>Time interval used in start rate limit for services.</p>");
    confOptList.append(confOption(map));

    map.clear();
    map["name"] = "DefaultStartLimitBurst";
    map["file"] = SYSTEMD;
    map["type"] = INTEGER;
    map["defVal"] = 5;
    map["toolTip"] = i18n("<p>Services are not allowed to start more than this number of times within the time interval defined in DefaultStartLimitInterval");
    confOptList.append(confOption(map));
  }

  if (systemdVersion >= 205)
  {
    map.clear();
    map["name"] = "DefaultEnvironment";
    map["file"] = SYSTEMD;
    map["type"] = STRING;
    map["defVal"] = QString("");
    map["toolTip"] = i18n("<p>Manager environment variables passed to all executed processes. Takes a space-separated list of variable assignments.</p>");
    confOptList.append(confOption(map));
  }

  if (systemdVersion >= 211)
  {
    map.clear();
    map["name"] = "DefaultCPUAccounting";
    map["file"] = SYSTEMD;
    map["type"] = BOOL;
    map["defVal"] = false;
    map["toolTip"] = i18n("<p>Default CPU usage accounting.</p>");
    confOptList.append(confOption(map));

    map.clear();
    map["name"] = "DefaultBlockIOAccounting";
    map["file"] = SYSTEMD;
    map["type"] = BOOL;
    map["defVal"] = false;
    map["toolTip"] = i18n("<p>Default block IO accounting.</p>");
    confOptList.append(confOption(map));

    map.clear();
    map["name"] = "DefaultMemoryAccounting";
    map["file"] = SYSTEMD;
    map["type"] = BOOL;
    map["defVal"] = false;
    map["toolTip"] = i18n("<p>Default process and kernel memory accounting.</p>");
    confOptList.append(confOption(map));
  }

  // Loop through all RESLIMITs
  QStringList resLimits = QStringList() << "DefaultLimitCPU" << "DefaultLimitFSIZE" << "DefaultLimitDATA"
                                        << "DefaultLimitSTACK" << "DefaultLimitCORE" << "DefaultLimitRSS"
                                        << "DefaultLimitNOFILE" << "DefaultLimitAS" << "DefaultLimitNPROC"
                                        << "DefaultLimitMEMLOCK" << "DefaultLimitLOCKS" << "DefaultLimitSIGPENDING"
                                        << "DefaultLimitMSGQUEUE" << "DefaultLimitNICE" << "DefaultLimitRTPRIO"
                                        << "DefaultLimitRTTIME";
  foreach (QString s, resLimits)
  {
    map.clear();
    map["name"] = s;
    map["file"] = SYSTEMD;
    map["type"] = RESLIMIT;
    map["minVal"] = -1;
    map["toolTip"] = i18n("<p>Default resource limit for units. Set to -1 for no limit.</p>");
    confOptList.append(confOption(map));
  }

  // journald.conf

  map.clear();
  map["name"] = "Storage";
  map["file"] = JOURNALD;
  map["type"] = LIST;
  map["defVal"] = "auto";
  map["possibleVals"] = QStringList() << "volatile" << "persistent" << "auto" << "none";
  map["toolTip"] = i18n("<p>Where to store log files.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "Compress";
  map["file"] = JOURNALD;
  map["type"] = BOOL;
  map["defVal"] = true;
  map["toolTip"] = i18n("<p>Compress log files.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "Seal";
  map["file"] = JOURNALD;
  map["type"] = BOOL;
  map["defVal"] = true;
  map["toolTip"] = i18n("<p>Enable Forward Secure Sealing (FSS) for all persistent journal files.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "SplitMode";
  map["file"] = JOURNALD;
  map["type"] = LIST;
  if (systemdVersion >= 215)
    map["defVal"] = "uid";
  else
    map["defVal"] = "login";
  map["possibleVals"] = QStringList() << "login" << "uid" << "none";
  map["toolTip"] = i18n("<p>Whether and how to split log files. If <i>uid</i>, all users will get each their own journal files regardless of whether they possess a login session or not, however system users will log into the system journal. If <i>login</i>, actually logged-in users will get each their own journal files, but users without login session and system users will log into the system journal. If <i>none</i>, journal files are not split up by user and all messages are instead stored in the single system journal.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "SyncIntervalSec";
  map["file"] = JOURNALD;
  map["type"] = TIME;
  map["defVal"] = 5;
  map["defUnit"] = confOption::min;
  map["toolTip"] = i18n("<p>The timeout before synchronizing journal files to disk.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "RateLimitInterval";
  map["file"] = JOURNALD;
  map["type"] = TIME;
  map["defVal"] = 30;
  map["toolTip"] = i18n("<p>Time interval for rate limiting of log messages. Set to 0 to turn off rate-limiting.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "RateLimitBurst";
  map["file"] = JOURNALD;
  map["type"] = INTEGER;
  map["defVal"] = 1000;
  map["toolTip"] = i18n("<p>Maximum number of messages logged for a unit in the interval specified in RateLimitInterval. Set to 0 to turn off rate-limiting.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "SystemMaxUse";
  map["file"] = JOURNALD;
  map["type"] = SIZE;
  map["defVal"] = QVariant(0.1 * partPersSizeMB).toULongLong();
  map["maxVal"] = partPersSizeMB;
  map["toolTip"] = i18n("<p>Maximum disk space the persistent journal may take up. Defaults to 10% of file system size.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "SystemKeepFree";
  map["file"] = JOURNALD;
  map["type"] = SIZE;
  map["defVal"] = QVariant(0.15 * partPersSizeMB).toULongLong();
  map["maxVal"] = partPersSizeMB;
  map["toolTip"] = i18n("<p>Minimum disk space the persistent journal should keep free for other uses. Defaults to 15% of file system size.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "SystemMaxFileSize";
  map["file"] = JOURNALD;
  map["type"] = SIZE;
  map["defVal"] = QVariant(0.125 * 0.1 * partPersSizeMB).toULongLong();
  map["maxVal"] = partPersSizeMB;
  map["toolTip"] = i18n("<p>Maximum size of individual journal files on persistent storage. Defaults to 1/8 of SystemMaxUse.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "RuntimeMaxUse";
  map["file"] = JOURNALD;
  map["type"] = SIZE;
  map["defVal"] = QVariant(0.1 * partVolaSizeMB).toULongLong();
  map["maxVal"] = partVolaSizeMB;
  map["toolTip"] = i18n("<p>Maximum disk space the volatile journal may take up. Defaults to 10% of file system size.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "RuntimeKeepFree";
  map["file"] = JOURNALD;
  map["type"] = SIZE;
  map["defVal"] = QVariant(0.15 * partVolaSizeMB).toULongLong();
  map["maxVal"] = partVolaSizeMB;
  map["toolTip"] = i18n("<p>Minimum disk space the volatile journal should keep free for other uses. Defaults to 15% of file system size.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "RuntimeMaxFileSize";
  map["file"] = JOURNALD;
  map["type"] = SIZE;
  map["defVal"] = QVariant(0.125 * 0.1 * partVolaSizeMB).toULongLong();
  map["maxVal"] = partVolaSizeMB;
  map["toolTip"] = i18n("<p>Maximum size of individual journal files on volatile storage. Defaults to 1/8 of RuntimeMaxUse.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "MaxRetentionSec";
  map["file"] = JOURNALD;
  map["type"] = TIME;
  map["defVal"] = 0;
  map["defUnit"] = confOption::d;
  map["toolTip"] = i18n("<p>Maximum time to store journal entries. Set to 0 to disable.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "MaxFileSec";
  map["file"] = JOURNALD;
  map["type"] = TIME;
  map["defVal"] = 30;
  map["defUnit"] = confOption::d;
  map["toolTip"] = i18n("<p>Maximum time to store entries in a single journal file before rotating to the next one. Set to 0 to disable.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "ForwardToSyslog";
  map["file"] = JOURNALD;
  map["type"] = BOOL;
  map["defVal"] = true;
  map["toolTip"] = i18n("<p>Forward journal messages to syslog.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "ForwardToKMsg";
  map["file"] = JOURNALD;
  map["type"] = BOOL;
  map["defVal"] = false;
  map["toolTip"] = i18n("<p>Forward journal messages to kernel log buffer</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "ForwardToConsole";
  map["file"] = JOURNALD;
  map["type"] = BOOL;
  map["defVal"] = false;
  map["toolTip"] = i18n("<p>Forward journal messages to the system console. The console can be changed with TTYPath.</p>");
  confOptList.append(confOption(map));

  if (systemdVersion >= 212)
  {
    map.clear();
    map["name"] = "ForwardToWall";
    map["file"] = JOURNALD;
    map["type"] = BOOL;
    map["defVal"] = true;
    map["toolTip"] = i18n("<p>Forward journal messages as wall messages to all logged-in users.</p>");
    confOptList.append(confOption(map));
  }

  map.clear();
  map["name"] = "TTYPath";
  map["file"] = JOURNALD;
  map["type"] = STRING;
  map["defVal"] = "/dev/console";
  map["toolTip"] = i18n("<p>Forward journal messages to this TTY if ForwardToConsole is set.</p>");
  confOptList.append(confOption(map));

  QStringList listLogLevel = QStringList() << "MaxLevelStore" << "MaxLevelSyslog"
                                           << "MaxLevelKMsg" << "MaxLevelConsole";
  if (systemdVersion >= 212)
    listLogLevel << "MaxLevelWall";

  foreach (QString s, listLogLevel)
  {
    map.clear();
    map["name"] = s;
    map["file"] = JOURNALD;
    map["type"] = LIST;
    if (s == "MaxLevelKMsg")
      map["defVal"] = "notice";
    else if (s == "MaxLevelConsole")
      map["defVal"] = "info";
    else if (s == "MaxLevelWall")
      map["defVal"] = "emerg";
    else
      map["defVal"] = "debug";
    map["possibleVals"] = QStringList() << "emerg" << "alert" << "crit" << "err"
                                        << "warning" << "notice" << "info" << "debug";
    map["toolTip"] = i18n("<p>Max log level to forward/store to the specified target.</p>");
    confOptList.append(confOption(map));
  }
  
  // logind.conf

  map.clear();
  map["name"] = "NAutoVTs";
  map["file"] = LOGIND;
  map["type"] = INTEGER;
  map["defVal"] = 6;
  map["maxVal"] = 12;
  map["toolTip"] = i18n("<p>Number of virtual terminals (VTs) to allocate by default and which will autospawn. Set to 0 to disable.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "ReserveVT";
  map["file"] = LOGIND;
  map["type"] = INTEGER;
  map["defVal"] = 6;
  map["maxVal"] = 12;
  map["toolTip"] = i18n("<p>Virtual terminal that shall unconditionally be reserved for autospawning. Set to 0 to disable.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "KillUserProcesses";
  map["file"] = LOGIND;
  map["type"] = BOOL;
  map["defVal"] = false;
  map["toolTip"] = i18n("<p>Kill the processes of a user when the user completely logs out.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "KillOnlyUsers";
  map["file"] = LOGIND;
  map["type"] = STRING;
  map["toolTip"] = i18n("<p>Space-separated list of usernames for which only processes will be killed if KillUserProcesses is enabled.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "KillExcludeUsers";
  map["file"] = LOGIND;
  map["type"] = STRING;
  map["defVal"] = "root";
  map["toolTip"] = i18n("<p>Space-separated list of usernames for which processes will be excluded from being killed if KillUserProcesses is enabled.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "InhibitDelayMaxSec";
  map["file"] = LOGIND;
  map["type"] = TIME;
  map["defVal"] = 5;
  map["toolTip"] = i18n("<p>Specifies the maximum time a system shutdown or sleep request is delayed due to an inhibitor lock of type delay being active before the inhibitor is ignored and the operation executes anyway.</p>");
  confOptList.append(confOption(map));

  QStringList listPowerActions = QStringList() << "ignore" << "poweroff" << "reboot" << "halt" << "kexec"
                                               << "suspend" << "hibernate" << "hybrid-sleep" << "lock";
  QStringList listPower = QStringList() << "HandlePowerKey" << "HandleSuspendKey"
                                           << "HandleHibernateKey" << "HandleLidSwitch";
  if (systemdVersion >= 217)
    listPower << "HandleLidSwitchDocked";
  foreach (QString s, listPower)
  {
    map.clear();
    map["name"] = s;
    map["file"] = LOGIND;
    map["type"] = LIST;
    if (s == "HandlePowerKey")
      map["defVal"] = "poweroff";
    else if (s == "HandleHibernateKey")
      map["defVal"] = "hibernate";
    else if (s == "HandleLidSwitchDocked")
      map["defVal"] = "ignore";
    else
      map["defVal"] = "suspend";
    map["possibleVals"] = listPowerActions;
    map["toolTip"] = i18n("<p>Controls whether logind shall handle the system power and sleep keys and the lid switch to trigger power actions.</p>");
    confOptList.append(confOption(map));
  }

  map.clear();
  map["name"] = "PowerKeyIgnoreInhibited";
  map["file"] = LOGIND;
  map["type"] = BOOL;
  map["defVal"] = false;
  map["toolTip"] = i18n("<p>Controls whether actions triggered by the power key are subject to inhibitor locks.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "SuspendKeyIgnoreInhibited";
  map["file"] = LOGIND;
  map["type"] = BOOL;
  map["defVal"] = false;
  map["toolTip"] = i18n("<p>Controls whether actions triggered by the suspend key are subject to inhibitor locks.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "HibernateKeyIgnoreInhibited";
  map["file"] = LOGIND;
  map["type"] = BOOL;
  map["defVal"] = false;
  map["toolTip"] = i18n("<p>Controls whether actions triggered by the hibernate key are subject to inhibitor locks.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "LidSwitchIgnoreInhibited";
  map["file"] = LOGIND;
  map["type"] = BOOL;
  map["defVal"] = true;
  map["toolTip"] = i18n("<p>Controls whether actions triggered by the lid switch are subject to inhibitor locks.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "IdleAction";
  map["file"] = LOGIND;
  map["type"] = LIST;
  map["defVal"] = "ignore";
  map["possibleVals"] = listPowerActions;
  map["toolTip"] = i18n("<p>Configures the action to take when the system is idle.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "IdleActionSec";
  map["file"] = LOGIND;
  map["type"] = TIME;
  map["defVal"] = 0;
  map["toolTip"] = i18n("<p>Configures the delay after which the action configured in IdleAction is taken after the system is idle.</p>");
  confOptList.append(confOption(map));

  if (systemdVersion >= 212)
  {
    map.clear();
    map["name"] = "RemoveIPC";
    map["file"] = LOGIND;
    map["type"] = BOOL;
    map["defVal"] = true;
    map["toolTip"] = i18n("<p>Controls whether System V and POSIX IPC objects belonging to the user shall be removed when the user fully logs out.</p>");
    confOptList.append(confOption(map));
  }

  // coredump.conf
  if (systemdVersion >= 215)
  {
    map.clear();
    map["name"] = "Storage";
    map["file"] = COREDUMP;
    map["type"] = LIST;
    map["defVal"] = "external";
    map["possibleVals"] = QStringList() << "none" << "external" << "journal" << "both";
    map["toolTip"] = i18n("<p>Controls where to store cores. When <i>none</i>, the coredumps will be logged but not stored permanently. When <i>external</i>, cores will be stored in /var/lib/systemd/coredump. When <i>journal</i>, cores will be stored in the journal and rotated following normal journal rotation patterns.</p>");
    confOptList.append(confOption(map));

    map.clear();
    map["name"] = "Compress";
    map["file"] = COREDUMP;
    map["type"] = BOOL;
    map["defVal"] = true;
    map["toolTip"] = i18n("<p>Controls compression for external storage.</p>");
    confOptList.append(confOption(map));

    map.clear();
    map["name"] = "ProcessSizeMax";
    map["file"] = COREDUMP;
    map["type"] = SIZE;
    map["defVal"] = 2*1024;
    map["maxVal"] = partPersSizeMB;
    map["toolTip"] = i18n("<p>The maximum size of a core which will be processed. Coredumps exceeding this size will be logged, but the backtrace will not be generated and the core will not be stored.</p>");
    confOptList.append(confOption(map));

    map.clear();
    map["name"] = "ExternalSizeMax";
    map["file"] = COREDUMP;
    map["type"] = SIZE;
    map["defVal"] = 2*1024;
    map["maxVal"] = partPersSizeMB;
    map["toolTip"] = i18n("<p>The maximum (uncompressed) size of a core to be saved.</p>");
    confOptList.append(confOption(map));

    map.clear();
    map["name"] = "JournalSizeMax";
    map["file"] = COREDUMP;
    map["type"] = SIZE;
    map["defVal"] = 767;
    map["maxVal"] = partPersSizeMB;
    map["toolTip"] = i18n("<p>The maximum (uncompressed) size of a core to be saved.</p>");
    confOptList.append(confOption(map));

    map.clear();
    map["name"] = "MaxUse";
    map["file"] = COREDUMP;
    map["type"] = SIZE;
    map["defVal"] = QVariant(0.1 * partPersSizeMB).toULongLong();
    map["maxVal"] = partPersSizeMB;
    map["toolTip"] = i18n("<p>Old coredumps are removed as soon as the total disk space taken up by coredumps grows beyond this limit. Defaults to 10% of the total disk size.</p>");
    confOptList.append(confOption(map));

    map.clear();
    map["name"] = "KeepFree";
    map["file"] = COREDUMP;
    map["type"] = SIZE;
    map["defVal"] = QVariant(0.15 * partPersSizeMB).toULongLong();
    map["maxVal"] = partPersSizeMB;
    map["toolTip"] = i18n("<p>Minimum disk space to keep free. Defaults to 15% of the total disk size.</p>");
    confOptList.append(confOption(map));
  }

  /* We can dismiss these options now...
  if (systemdVersion < 207)
  {
    confOptList.append(confOption(LOGIND, "Controllers", STRING, ""));
    confOptList.append(confOption(LOGIND, "ResetControllers", STRING, "cpu"));
  }
  */
}

void kcmsystemd::readConfFile(int fileindex)
{
  QFile file (etcDir + "/" + listConfFiles.at(fileindex));
  if (file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    QTextStream in(&file);
    QString line = in.readLine();

    while(!line.isNull())
    {
      if (!line.startsWith('#') && !line.startsWith('[') && !line.isEmpty())
      {
        // qDebug() << "line: " << line;
        int index = confOptList.indexOf(confOption(QString(line.section("=",0,0).trimmed() + "_" + QString::number(fileindex))));
        if (index >= 0)
        {
          if (confOptList[index].setValueFromFile(line) == -1)
            KMessageBox::error(this, i18n("\"%1\" is not a valid value for %2. Using default value for this parameter.", line.section("=",1).trimmed(), confOptList.at(index).realName));
        }
      }
      line = in.readLine();
    } // read lines until empty

    qDebug() << QString("Successfully read " + etcDir + "/" + listConfFiles.at(fileindex));
  } // if file open
  else
    KMessageBox::error(this, i18n("Failed to read %1/%2. Using default values.", etcDir, listConfFiles.at(fileindex)));
}

void kcmsystemd::setupConf()
{
  // Sets up the configfile model and tableview

  confModel = new ConfModel(this);
  proxyModelConf = new QSortFilterProxyModel(this);
  proxyModelConf->setSourceModel(confModel);

  ui.tblConf->setModel(proxyModelConf);
  
  // Use a custom delegate to enable different editor elements in the QTableView
  ConfDelegate *myDelegate;
  myDelegate = new ConfDelegate(this);
  ui.tblConf->setItemDelegate(myDelegate);

  ui.tblConf->setColumnHidden(2, true);
  ui.tblConf->resizeColumnsToContents();
}

void kcmsystemd::setupUnitslist()
{
  // Sets up the units list initially

  // Register the meta type for storing units
  qDBusRegisterMetaType<SystemdUnit>();

  QMap<filterType, QString> filters;
  filters[activeState] = "";
  filters[unitType] = "";
  filters[unitName] = "";

  // QList<SystemdUnit> *ptrUnits;
  // ptrUnits = &unitslist;

  // Setup the system unit model
  systemUnitModel = new UnitModel(this, &unitslist);
  systemUnitFilterModel = new SortFilterUnitModel(this);
  systemUnitFilterModel->setDynamicSortFilter(false);
  systemUnitFilterModel->initFilterMap(filters);
  systemUnitFilterModel->setSourceModel(systemUnitModel);
  ui.tblUnits->setModel(systemUnitFilterModel);
  ui.tblUnits->sortByColumn(3, Qt::AscendingOrder);

  // Setup the user unit model
  userUnitModel = new UnitModel(this, &userUnitslist, userBusPath);
  userUnitFilterModel = new SortFilterUnitModel(this);
  userUnitFilterModel->setDynamicSortFilter(false);
  userUnitFilterModel->initFilterMap(filters);
  userUnitFilterModel->setSourceModel(userUnitModel);
  ui.tblUserUnits->setModel(userUnitFilterModel);
  ui.tblUserUnits->sortByColumn(3, Qt::AscendingOrder);

  slotChkShowUnits(-1);
}

void kcmsystemd::setupSessionlist()
{
  // Sets up the session list initially

  // Register the meta type for storing units
  qDBusRegisterMetaType<SystemdSession>();

  // Setup model for session list
  sessionModel = new QStandardItemModel(this);

  // Install eventfilter to capture mouse move events
  ui.tblSessions->viewport()->installEventFilter(this);

  // Set header row
  sessionModel->setHorizontalHeaderItem(0, new QStandardItem(i18n("Session ID")));
  sessionModel->setHorizontalHeaderItem(1, new QStandardItem(i18n("Session Object Path"))); // This column is hidden
  sessionModel->setHorizontalHeaderItem(2, new QStandardItem(i18n("State")));
  sessionModel->setHorizontalHeaderItem(3, new QStandardItem(i18n("User ID")));
  sessionModel->setHorizontalHeaderItem(4, new QStandardItem(i18n("User Name")));
  sessionModel->setHorizontalHeaderItem(5, new QStandardItem(i18n("Seat ID")));

  // Set model for QTableView (should be called after headers are set)
  ui.tblSessions->setModel(sessionModel);
  ui.tblSessions->setColumnHidden(1, true);

  // Add all the sessions
  slotRefreshSessionList();
}

void kcmsystemd::setupTimerlist()
{
  // Sets up the timer list initially

  // Setup model for timer list
  timerModel = new QStandardItemModel(this);

  // Install eventfilter to capture mouse move events
  // ui.tblTimers->viewport()->installEventFilter(this);

  // Set header row
  timerModel->setHorizontalHeaderItem(0, new QStandardItem(i18n("Timer")));
  timerModel->setHorizontalHeaderItem(1, new QStandardItem(i18n("Next")));
  timerModel->setHorizontalHeaderItem(2, new QStandardItem(i18n("Left")));
  timerModel->setHorizontalHeaderItem(3, new QStandardItem(i18n("Last")));
  timerModel->setHorizontalHeaderItem(4, new QStandardItem(i18n("Passed")));
  timerModel->setHorizontalHeaderItem(5, new QStandardItem(i18n("Activates")));

  // Set model for QTableView (should be called after headers are set)
  ui.tblTimers->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  ui.tblTimers->setModel(timerModel);
  ui.tblTimers->sortByColumn(1, Qt::AscendingOrder);

  // Setup a timer that updates the left and passed columns every 5secs
  timer = new QTimer(this);
  connect(timer, SIGNAL(timeout()), this, SLOT(slotUpdateTimers()));
  timer->start(1000);

  slotRefreshTimerList();
}

void kcmsystemd::defaults()
{
  if (KMessageBox::warningYesNo(this, i18n("Load default settings for all files?")) == KMessageBox::Yes)
  { 
    //defaults for system.conf
    for (int i = 0; i < confOptList.size(); ++i)
    {
      confOptList[i].setToDefault();
    }
    emit changed (true);
  }
}

void kcmsystemd::save()
{  
  QString systemConfFileContents;
  systemConfFileContents.append("# " + etcDir + "/system.conf\n# Generated by kcmsystemd control module v" + KCM_SYSTEMD_VERSION + ".\n");
  systemConfFileContents.append("[Manager]\n");
  foreach (confOption i, confOptList)
  {
    if (i.file == SYSTEMD)
      systemConfFileContents.append(i.getLineForFile());
  }

  QString journaldConfFileContents;
  journaldConfFileContents.append("# " + etcDir + "/journald.conf\n# Generated by kcmsystemd control module v" + KCM_SYSTEMD_VERSION + ".\n");
  journaldConfFileContents.append("[Journal]\n");
  foreach (confOption i, confOptList)
  {
    if (i.file == JOURNALD)
      journaldConfFileContents.append(i.getLineForFile());
  }  
  
  QString logindConfFileContents;
  logindConfFileContents.append("# " + etcDir + "/logind.conf\n# Generated by kcmsystemd control module v" + KCM_SYSTEMD_VERSION + ".\n");
  logindConfFileContents.append("[Login]\n");
  foreach (confOption i, confOptList)
  {
    if (i.file == LOGIND)
      logindConfFileContents.append(i.getLineForFile());
  }
  
  QString coredumpConfFileContents;
  coredumpConfFileContents.append("# " + etcDir + "/coredump.conf\n# Generated by kcmsystemd control module v" + KCM_SYSTEMD_VERSION + ".\n");
  coredumpConfFileContents.append("[Coredump]\n");
  foreach (confOption i, confOptList)
  {
    if (i.file == COREDUMP)
      coredumpConfFileContents.append(i.getLineForFile());
  }
  
  // Declare a QVariantMap with arguments for the helper
  QVariantMap helperArgs;
  if (QDir(etcDir).exists()) {
    helperArgs["etcDir"] = etcDir;
  } else {
    // Failed to find systemd config directory
    KMessageBox::error(this, i18n("Unable to find directory for configuration files."));
    return;
  }
  
  QVariantMap files;
  files["system.conf"] = systemConfFileContents;
  files["journald.conf"] = journaldConfFileContents;
  files["logind.conf"] = logindConfFileContents;
  if (systemdVersion >= 215)
    files["coredump.conf"] = coredumpConfFileContents;  
  helperArgs["files"] = files;
  
  // Action saveAction("org.kde.kcontrol.kcmsystemd.save");
  Action saveAction = authAction();
  saveAction.setHelperId("org.kde.kcontrol.kcmsystemd");
  saveAction.setArguments(helperArgs);

  ExecuteJob *job = saveAction.execute();

  if (!job->exec())
  {
    KMessageBox::error(this, i18n("Unable to authenticate/execute the action: %1, %2", job->error(), job->errorString()));
  }
  else {
    KMessageBox::information(this, i18n("Configuration files succesfully written to: %1", helperArgs["etcDir"].toString()));
  }
}

void kcmsystemd::slotConfChanged(const QModelIndex &, const QModelIndex &)
{
  // qDebug() << "dataChanged emitted";
  emit changed(true);
}

void kcmsystemd::slotKdeConfig()
{
  // Set a QString containing the kde prefix
  kdePrefix = QString::fromLatin1(kdeConfig->readAllStandardOutput()).trimmed();
}

void kcmsystemd::slotChkShowUnits(int state)
{
  if (state == -1 ||
      QObject::sender()->objectName() == "chkInactiveUnits" ||
      QObject::sender()->objectName() == "chkUnloadedUnits")
  {
    // System units
    if (ui.chkInactiveUnits->isChecked())
    {
      ui.chkUnloadedUnits->setEnabled(true);
      if (ui.chkUnloadedUnits->isChecked())
        systemUnitFilterModel->addFilterRegExp(activeState, "");
      else
        systemUnitFilterModel->addFilterRegExp(activeState, "active");
    }
    else
    {
      ui.chkUnloadedUnits->setEnabled(false);
      systemUnitFilterModel->addFilterRegExp(activeState, "^(active)");
    }
    systemUnitFilterModel->invalidate();
    ui.tblUnits->sortByColumn(ui.tblUnits->horizontalHeader()->sortIndicatorSection(),
                              ui.tblUnits->horizontalHeader()->sortIndicatorOrder());
  }
  if (state == -1 ||
      QObject::sender()->objectName() == "chkInactiveUserUnits" ||
      QObject::sender()->objectName() == "chkUnloadedUserUnits")
  {
    // User units
    if (ui.chkInactiveUserUnits->isChecked())
    {
      ui.chkUnloadedUserUnits->setEnabled(true);
      if (ui.chkUnloadedUserUnits->isChecked())
        userUnitFilterModel->addFilterRegExp(activeState, "");
      else
        userUnitFilterModel->addFilterRegExp(activeState, "active");
    }
    else
    {
      ui.chkUnloadedUserUnits->setEnabled(false);
      userUnitFilterModel->addFilterRegExp(activeState, "^(active)");
    }
    userUnitFilterModel->invalidate();
    ui.tblUserUnits->sortByColumn(ui.tblUserUnits->horizontalHeader()->sortIndicatorSection(),
                                  ui.tblUserUnits->horizontalHeader()->sortIndicatorOrder());
  }
  updateUnitCount();
}

void kcmsystemd::slotCmbUnitTypes(int index)
{
  // Filter unit list for a selected unit type

  if (QObject::sender()->objectName() == "cmbUnitTypes")
  {
    systemUnitFilterModel->addFilterRegExp(unitType, "(" + unitTypeSufx.at(index) + ")$");
    systemUnitFilterModel->invalidate();
    ui.tblUnits->sortByColumn(ui.tblUnits->horizontalHeader()->sortIndicatorSection(),
                              ui.tblUnits->horizontalHeader()->sortIndicatorOrder());
  }
  else if (QObject::sender()->objectName() == "cmbUserUnitTypes")
  {
    userUnitFilterModel->addFilterRegExp(unitType, "(" + unitTypeSufx.at(index) + ")$");
    userUnitFilterModel->invalidate();
    ui.tblUserUnits->sortByColumn(ui.tblUserUnits->horizontalHeader()->sortIndicatorSection(),
                                  ui.tblUserUnits->horizontalHeader()->sortIndicatorOrder());
  }
  updateUnitCount();
}

void kcmsystemd::slotRefreshUnitsList(bool initial, dbusBus bus)
{
  // Updates the unit lists

  if (bus == sys)
  {
    qDebug() << "Refreshing system units...";

    // get an updated list of system units via dbus
    unitslist.clear();
    unitslist = getUnitsFromDbus(sys);
    noActSystemUnits = 0;
    foreach (SystemdUnit unit, unitslist)
    {
      if (unit.active_state == "active")
        noActSystemUnits++;
    }
    if (!initial)
    {
      systemUnitModel->dataChanged(systemUnitModel->index(0, 0), systemUnitModel->index(systemUnitModel->rowCount(), 3));
      systemUnitFilterModel->invalidate();
      updateUnitCount();
      slotRefreshTimerList();
    }
  }

  else if (enableUserUnits && bus == user)
  {
    qDebug() << "Refreshing user units...";

    // get an updated list of user units via dbus
    userUnitslist.clear();
    userUnitslist = getUnitsFromDbus(user);
    noActUserUnits = 0;
    foreach (SystemdUnit unit, userUnitslist)
    {
      if (unit.active_state == "active")
        noActUserUnits++;
    }
    if (!initial)
    {
      userUnitModel->dataChanged(userUnitModel->index(0, 0), userUnitModel->index(userUnitModel->rowCount(), 3));
      userUnitFilterModel->invalidate();
      updateUnitCount();
      slotRefreshTimerList();
    }
  }
}

void kcmsystemd::slotRefreshSessionList()
{
  // Updates the session list
  qDebug() << "Refreshing session list...";

  // clear list
  sessionlist.clear();

  // get an updated list of sessions via dbus
  QDBusMessage dbusreply = callDbusMethod("ListSessions", logdMgr);

  // extract the list of sessions from the reply
  const QDBusArgument arg = dbusreply.arguments().at(0).value<QDBusArgument>();
  if (arg.currentType() == QDBusArgument::ArrayType)
  {
    arg.beginArray();
    while (!arg.atEnd())
    {
      SystemdSession session;
      arg >> session;
      sessionlist.append(session);
    }
    arg.endArray();
  }

  // Iterate through the new list and compare to model
  for (int i = 0;  i < sessionlist.size(); ++i)
  {
    // This is needed to get the "State" property

    QList<QStandardItem *> items = sessionModel->findItems(sessionlist.at(i).session_id, Qt::MatchExactly, 0);

    if (items.isEmpty())
    {
      // New session discovered so add it to the model
      QList<QStandardItem *> row;
      row <<
      new QStandardItem(sessionlist.at(i).session_id) <<
      new QStandardItem(sessionlist.at(i).session_path.path()) <<
      new QStandardItem(getDbusProperty("State", logdSession, sessionlist.at(i).session_path).toString()) <<
      new QStandardItem(QString::number(sessionlist.at(i).user_id)) <<
      new QStandardItem(sessionlist.at(i).user_name) <<
      new QStandardItem(sessionlist.at(i).seat_id);
      sessionModel->appendRow(row);
    } else {
      sessionModel->item(items.at(0)->row(), 2)->setData(getDbusProperty("State", logdSession, sessionlist.at(i).session_path).toString(), Qt::DisplayRole);
    }
  }

  // Check to see if any sessions were removed
  if (sessionModel->rowCount() != sessionlist.size())
  {
    QList<QPersistentModelIndex> indexes;
    // Loop through model and compare to retrieved sessionlist
    for (int row = 0; row < sessionModel->rowCount(); ++row)
    {
      SystemdSession session;
      session.session_id = sessionModel->index(row,0).data().toString();
      if (!sessionlist.contains(session))
      {
        // Add removed units to list for deletion
        // qDebug() << "Unit removed: " << systemUnitModel->index(row,3).data().toString();
        indexes << sessionModel->index(row,0);
      }
    }
    // Delete the identified units from model
    foreach (QPersistentModelIndex i, indexes)
      sessionModel->removeRow(i.row());
  }

  // Update the text color in model
  QColor newcolor;
  for (int row = 0; row < sessionModel->rowCount(); ++row)
  {
    if (sessionModel->data(sessionModel->index(row,2), Qt::DisplayRole) == "active")
      newcolor = Qt::darkGreen;
    else if (sessionModel->data(sessionModel->index(row,2), Qt::DisplayRole) == "closing")
      newcolor = Qt::darkGray;
    else
      newcolor = Qt::black;
    for (int col = 0; col < sessionModel->columnCount(); ++col)
      sessionModel->setData(sessionModel->index(row,col), QVariant(newcolor), Qt::ForegroundRole);
  }
}

void kcmsystemd::slotRefreshTimerList()
{
  // Updates the timer list
  // qDebug() << "Refreshing timer list...";

  timerModel->removeRows(0, timerModel->rowCount());

  // Iterate through system unitlist and add timers to the model
  foreach (SystemdUnit unit, unitslist)
  {
    if (unit.id.endsWith(".timer") && unit.load_state != "unloaded")
      timerModel->appendRow(buildTimerListRow(unit, unitslist, sys));
  }

  // Iterate through user unitlist and add timers to the model
  foreach (SystemdUnit unit, userUnitslist)
  {
    if (unit.id.endsWith(".timer") && unit.load_state != "unloaded")
      timerModel->appendRow(buildTimerListRow(unit, userUnitslist, user));
  }

  // Update the left and passed columns
  slotUpdateTimers();

  ui.tblTimers->resizeColumnsToContents();
  ui.tblTimers->sortByColumn(ui.tblTimers->horizontalHeader()->sortIndicatorSection(),
                             ui.tblTimers->horizontalHeader()->sortIndicatorOrder());
}

QList<QStandardItem *> kcmsystemd::buildTimerListRow(const SystemdUnit &unit, const QList<SystemdUnit> &list, dbusBus bus)
{
  // Builds a row for the timers list

  QDBusObjectPath path = unit.unit_path;
  QString unitToActivate = getDbusProperty("Unit", sysdTimer, path, bus).toString();

  QDateTime time;
  QIcon icon;
  if (bus == sys)
    icon = QIcon::fromTheme("object-locked");
  else
    icon = QIcon::fromTheme("user-identity");

  // Add the next elapsation point
  qlonglong nextElapseMonotonicMsec = getDbusProperty("NextElapseUSecMonotonic", sysdTimer, path, bus).toULongLong() / 1000;
  qlonglong nextElapseRealtimeMsec = getDbusProperty("NextElapseUSecRealtime", sysdTimer, path, bus).toULongLong() / 1000;
  qlonglong lastTriggerMSec = getDbusProperty("LastTriggerUSec", sysdTimer, path, bus).toULongLong() / 1000;

  if (nextElapseMonotonicMsec == 0)
  {
    // Timer is calendar-based
    time.setMSecsSinceEpoch(nextElapseRealtimeMsec);
  }
  else
  {
    // Timer is monotonic
    time = QDateTime().currentDateTime();
    time = time.addMSecs(nextElapseMonotonicMsec);

    // Get the monotonic system clock
    struct timespec ts;
    if (clock_gettime( CLOCK_MONOTONIC, &ts ) != 0)
      qDebug() << "Failed to get the monotonic system clock!";

    // Convert the monotonic system clock to microseconds
    qlonglong now_mono_usec = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

    // And substract it
    time = time.addMSecs(-now_mono_usec/1000);
  }

  QString next = time.toString("yyyy.MM.dd hh:mm:ss");

  QString last;

  // use unit object to get last time for activated service
  int index = list.indexOf(SystemdUnit(unitToActivate));
  if (index != -1)
  {
    qlonglong inactivateExitTimestampMsec =
        getDbusProperty("InactiveExitTimestamp", sysdUnit, list.at(index).unit_path, bus).toULongLong() / 1000;

    if (inactivateExitTimestampMsec == 0)
    {
      // The unit has not run in this boot
      // Use LastTrigger to see if the timer is persistent
      if (lastTriggerMSec == 0)
        last = "n/a";
      else
      {
        time.setMSecsSinceEpoch(lastTriggerMSec);
        last = time.toString("yyyy.MM.dd hh:mm:ss");
      }
    }
    else
    {
      QDateTime time;
      time.setMSecsSinceEpoch(inactivateExitTimestampMsec);
      last = time.toString("yyyy.MM.dd hh:mm:ss");
    }
  }

  // Set icon for id column
  QStandardItem *id = new QStandardItem(unit.id);
  id->setData(icon, Qt::DecorationRole);

  // Build a row from QStandardItems
  QList<QStandardItem *> row;
  row << id <<
         new QStandardItem(next) <<
         new QStandardItem("") <<
         new QStandardItem(last) <<
         new QStandardItem("") <<
         new QStandardItem(unitToActivate);

  return row;
}

void kcmsystemd::updateUnitCount()
{
  ui.lblUnitCount->setText(i18n("Total: %1 units, %2 active, %3 displayed",
                                QString::number(systemUnitModel->rowCount()),
                                QString::number(noActSystemUnits),
                                QString::number(systemUnitFilterModel->rowCount())));
  ui.lblUserUnitCount->setText(i18n("Total: %1 units, %2 active, %3 displayed",
                                    QString::number(userUnitModel->rowCount()),
                                    QString::number(noActUserUnits),
                                    QString::number(userUnitFilterModel->rowCount())));
}

void kcmsystemd::authServiceAction(QString service, QString path, QString interface, QString method, QList<QVariant> args)
{
  // Function to call the helper to authenticate a call to systemd over the system DBus

  // Declare a QVariantMap with arguments for the helper
  QVariantMap helperArgs;
  helperArgs["service"] = service;
  helperArgs["path"] = path;
  helperArgs["interface"] = interface;
  helperArgs["method"] = method;
  helperArgs["argsForCall"] = args;
    
  // Call the helper
  Action serviceAction("org.kde.kcontrol.kcmsystemd.dbusaction");
  serviceAction.setHelperId("org.kde.kcontrol.kcmsystemd");
  serviceAction.setArguments(helperArgs);

  ExecuteJob* job = serviceAction.execute();
  job->exec();

  if (!job->exec())
  {
    KMessageBox::error(this, i18n("Unable to authenticate/execute the action.\nError code: %1\nError string: %2\nError text: %3", job->error(), job->errorString(), job->errorText()));
  }
  else
  {
    qDebug() << "DBus action successful.";
    // KMessageBox::information(this, i18n("DBus action successful."));
  }
}

void kcmsystemd::slotUnitContextMenu(const QPoint &pos)
{
  // Slot for creating the right-click menu in unitlists

  // Setup objects which can be used for both system and user units
  QList<SystemdUnit> *list;
  QTableView *tblView;
  dbusBus bus;
  bool requiresAuth = true;
  if (ui.tabWidget->currentIndex() == 0)
  {
    list = &unitslist;
    tblView = ui.tblUnits;
    bus = sys;
  }
  else if (ui.tabWidget->currentIndex() == 1)
  {
    list = &userUnitslist;
    tblView = ui.tblUserUnits;
    bus = user;
    requiresAuth = false;
  }

  // Find name and object path of unit
  QString unit = tblView->model()->index(tblView->indexAt(pos).row(), 3).data().toString();
  QDBusObjectPath pathUnit = list->at(list->indexOf(SystemdUnit(unit))).unit_path;

  // Create rightclick menu items
  QMenu menu(this);
  QAction *start = menu.addAction(i18n("&Start unit"));
  QAction *stop = menu.addAction(i18n("S&top unit"));
  QAction *restart = menu.addAction(i18n("&Restart unit"));
  QAction *reload = menu.addAction(i18n("Re&load unit"));
  menu.addSeparator();
  QAction *edit = menu.addAction(i18n("&Edit unit file"));
  QAction *isolate = menu.addAction(i18n("&Isolate unit"));
  menu.addSeparator();
  QAction *enable = menu.addAction(i18n("En&able unit"));
  QAction *disable = menu.addAction(i18n("&Disable unit"));
  menu.addSeparator();
  QAction *mask = menu.addAction(i18n("&Mask unit"));
  QAction *unmask = menu.addAction(i18n("&Unmask unit"));
  menu.addSeparator();
  QAction *reloaddaemon = menu.addAction(i18n("Rel&oad all unit files"));
  QAction *reexecdaemon = menu.addAction(i18n("Ree&xecute systemd"));
  
  // Get UnitFileState (have to use Manager object for this)
  QList<QVariant> args;
  args << unit;
  QString UnitFileState = callDbusMethod("GetUnitFileState", sysdMgr, bus, args).arguments().at(0).toString();

  // Check capabilities of unit
  QString LoadState, ActiveState;
  bool CanStart, CanStop, CanReload;
  if (!pathUnit.path().isEmpty() && getDbusProperty("Test", sysdUnit, pathUnit, bus).toString() != "invalidIface")
  {
    // Unit has a Unit DBus object, fetch properties
    isolate->setEnabled(getDbusProperty("CanIsolate", sysdUnit, pathUnit, bus).toBool());
    LoadState = getDbusProperty("LoadState", sysdUnit, pathUnit, bus).toString();
    ActiveState = getDbusProperty("ActiveState", sysdUnit, pathUnit, bus).toString();
    CanStart = getDbusProperty("CanStart", sysdUnit, pathUnit, bus).toBool();
    CanStop = getDbusProperty("CanStop", sysdUnit, pathUnit, bus).toBool();
    CanReload = getDbusProperty("CanReload", sysdUnit, pathUnit, bus).toBool();
  }
  else
  {
    // No Unit DBus object, only enable Start
    isolate->setEnabled(false);
    CanStart = true;
    CanStop = false;
    CanReload = false;
  }

  // Enable/disable menu items
  if (CanStart && ActiveState != "active")
    start->setEnabled(true);
  else
    start->setEnabled(false);

  if (CanStop &&
      ActiveState != "inactive" &&
      ActiveState != "failed")
    stop->setEnabled(true);
  else
    stop->setEnabled(false);

  if (CanStart &&
      ActiveState != "inactive" &&
      ActiveState != "failed" &&
      !LoadState.isEmpty())
    restart->setEnabled(true);
  else
    restart->setEnabled(false);

  if (CanReload &&
      ActiveState != "inactive" &&
      ActiveState != "failed")
    reload->setEnabled(true);
  else
    reload->setEnabled(false);

  if (UnitFileState == "disabled")
    disable->setEnabled(false);
  else if (UnitFileState == "enabled")
    enable->setEnabled(false);
  else
  {
    enable->setEnabled(false);    
    disable->setEnabled(false);
  }

  if (LoadState == "masked")
    mask->setEnabled(false);
  else if (LoadState != "masked")
    unmask->setEnabled(false);
  
  // Check if unit has a unit file, if not disable editing
  QString frpath;
  int index = list->indexOf(SystemdUnit(unit));
  if (index != -1)
    frpath = list->at(index).unit_file;
  if (frpath.isEmpty())
    edit->setEnabled(false);

  QAction *a = menu.exec(tblView->viewport()->mapToGlobal(pos));
   
  if (a == edit)
  {
    // Find the application associated with text files
    KMimeTypeTrader* trader = KMimeTypeTrader::self();
    const KService::Ptr service = trader->preferredService("text/plain");

    // Open the unit file using the found application
    QString app;
    QStringList args;
    if (frpath.startsWith(getenv("HOME")))
    {
      // Unit file is in $HOME, no authentication required
      app = service->exec().section(' ', 0, 0);
      args << frpath;
    }
    else
    {
      // Unit file is outside $HOME, use kdesu for authentication
      app = "kdesu";
      args << "-t" << "--" << service->exec().section(' ', 0, 0) << frpath;
    }
    QProcess editor (this);
    bool r = editor.startDetached(app, args);
    if (!r && app == "kdesu")
      r = editor.startDetached(kdePrefix + "/lib/libexec/kdesu", args);
    if (!r)
      KMessageBox::error(this, i18n("Failed to open unit file!"));
    return;
  }

  // Setup method and arguments for DBus call
  QStringList unitsForCall = QStringList() << unit;
  QString method;
  QList<QVariant> argsForCall;

  if (a == start)
  {
    argsForCall << unit << "replace";
    method = "StartUnit";
  }
  else if (a == stop)
  {
    argsForCall << unit << "replace";
    method = "StopUnit";
  }
  else if (a == restart)
  {
    argsForCall << unit << "replace";
    method = "RestartUnit";
  }
  else if (a == reload)
  {
    argsForCall << unit << "replace";
    method = "ReloadUnit";
  }
  else if (a == isolate)
  {
    argsForCall << unit << "isolate";
    method = "StartUnit";
  }
  else if (a == enable)
  {
    argsForCall << QVariant(unitsForCall) << false << true;
    method = "EnableUnitFiles";
  }
  else if (a == disable)
  {
    argsForCall << QVariant(unitsForCall) << false;
    method = "DisableUnitFiles";
  }
  else if (a == mask)
  {
    argsForCall << QVariant(unitsForCall) << false << true;
    method = "MaskUnitFiles";
  }
  else if (a == unmask)
  {
    argsForCall << QVariant(unitsForCall) << false;
    method = "UnmaskUnitFiles";
  }
  else if (a == reloaddaemon)
    method = "Reload";
  else if (a == reexecdaemon)
    method = "Reexecute";

  // Execute the DBus actions
  if (!method.isEmpty() && requiresAuth)
    authServiceAction(connSystemd, pathSysdMgr, ifaceMgr, method, argsForCall);
  else if (!method.isEmpty())
  {
    // user unit
    callDbusMethod(method, sysdMgr, bus, argsForCall);
    if (method == "EnableUnitFiles" || method == "DisableUnitFiles" || method == "MaskUnitFiles" || method == "UnmaskUnitFiles")
      callDbusMethod("Reload", sysdMgr, bus);
  }
}

void kcmsystemd::slotSessionContextMenu(const QPoint &pos)
{
  // Slot for creating the right-click menu in the session list

  // Setup DBus interfaces
  QDBusObjectPath pathSession = QDBusObjectPath(ui.tblSessions->model()->index(ui.tblSessions->indexAt(pos).row(),1).data().toString());

  // Create rightclick menu items
  QMenu menu(this);
  QAction *activate = menu.addAction(i18n("&Activate session"));
  QAction *terminate = menu.addAction(i18n("&Terminate session"));
  QAction *lock = menu.addAction(i18n("&Lock session"));

  if (ui.tblSessions->model()->index(ui.tblSessions->indexAt(pos).row(),2).data().toString() == "active")
    activate->setEnabled(false);

  if (getDbusProperty("Type", logdSession, pathSession) == "tty")
    lock->setEnabled(false);

  QAction *a = menu.exec(ui.tblSessions->viewport()->mapToGlobal(pos));

  QString method;
  if (a == activate)
  {
    method = "Activate";
    QList<QVariant> argsForCall;
    authServiceAction(connLogind, pathSession.path(), ifaceSession, method, argsForCall);
  }
  if (a == terminate)
  {
    method = "Terminate";
    QList<QVariant> argsForCall;
    authServiceAction(connLogind, pathSession.path(), ifaceSession, method, argsForCall);
  }
  if (a == lock)
  {
    method = "Lock";
    QList<QVariant> argsForCall;
    authServiceAction(connLogind, pathSession.path(), ifaceSession, method, argsForCall);
  }
}


bool kcmsystemd::eventFilter(QObject *obj, QEvent* event)
{
  // Eventfilter for catching mouse move events over session list
  // Used for dynamically generating tooltips

  if (event->type() == QEvent::MouseMove && obj->parent()->objectName() == "tblSessions")
  {
    // Session list
    QMouseEvent *me = static_cast<QMouseEvent*>(event);
    QModelIndex inSessionModel = ui.tblSessions->indexAt(me->pos());
    if (!inSessionModel.isValid())
      return false;

    if (sessionModel->itemFromIndex(inSessionModel)->row() != lastSessionRowChecked)
    {
      // Cursor moved to a different row. Only build tooltips when moving
      // cursor to a new row to avoid excessive DBus calls.

      QString selSession = ui.tblSessions->model()->index(ui.tblSessions->indexAt(me->pos()).row(),0).data().toString();
      QDBusObjectPath spath = QDBusObjectPath(ui.tblSessions->model()->index(ui.tblSessions->indexAt(me->pos()).row(),1).data().toString());

      QString toolTipText;
      toolTipText.append("<FONT COLOR=white>");
      toolTipText.append("<b>" + selSession + "</b><hr>");

      // Use the DBus interface to get session properties
      if (getDbusProperty("test", logdSession, spath) != "invalidIface")
      {
        // Session has a valid session DBus object
        toolTipText.append(i18n("<b>VT:</b> %1", getDbusProperty("VTNr", logdSession, spath).toString()));

        QString remoteHost = getDbusProperty("RemoteHost", logdSession, spath).toString();
        if (getDbusProperty("Remote", logdSession, spath).toBool())
        {
          toolTipText.append(i18n("<br><b>Remote host:</b> %1", remoteHost));
          toolTipText.append(i18n("<br><b>Remote user:</b> %1", getDbusProperty("RemoteUser", logdSession, spath).toString()));
        }
        toolTipText.append(i18n("<br><b>Service:</b> %1", getDbusProperty("Service", logdSession, spath).toString()));

        QString type = getDbusProperty("Type", logdSession, spath).toString();
        toolTipText.append(i18n("<br><b>Type:</b> %1", type));
        if (type == "x11")
          toolTipText.append(i18n(" (display %1)", getDbusProperty("Display", logdSession, spath).toString()));
        else if (type == "tty")
        {
          QString path, tty = getDbusProperty("TTY", logdSession, spath).toString();
          if (!tty.isEmpty())
            path = tty;
          else if (!remoteHost.isEmpty())
            path = getDbusProperty("Name", logdSession, spath).toString() + "@" + remoteHost;
          toolTipText.append(" (" + path + ")");
        }
        toolTipText.append(i18n("<br><b>Class:</b> %1", getDbusProperty("Class", logdSession, spath).toString()));
        toolTipText.append(i18n("<br><b>State:</b> %1", getDbusProperty("State", logdSession, spath).toString()));
        toolTipText.append(i18n("<br><b>Scope:</b> %1", getDbusProperty("Scope", logdSession, spath).toString()));


        toolTipText.append(i18n("<br><b>Created: </b>"));
        if (getDbusProperty("Timestamp", logdSession, spath).toULongLong() == 0)
          toolTipText.append("n/a");
        else
        {
          QDateTime time;
          time.setMSecsSinceEpoch(getDbusProperty("Timestamp", logdSession, spath).toULongLong()/1000);
          toolTipText.append(time.toString());
        }
      }

      toolTipText.append("</FONT");
      sessionModel->itemFromIndex(inSessionModel)->setToolTip(toolTipText);

      lastSessionRowChecked = sessionModel->itemFromIndex(inSessionModel)->row();
      return true;

    } // Row was different
  }
  return false;
  // return true;
}

void kcmsystemd::slotSystemSystemdReloading(bool status)
{
  if (status)
    qDebug() << "System systemd reloading...";
  else
    slotRefreshUnitsList(false, sys);
}

void kcmsystemd::slotUserSystemdReloading(bool status)
{
  if (status)
    qDebug() << "User systemd reloading...";
  else
    slotRefreshUnitsList(false, user);
}

/*
void kcmsystemd::slotUnitLoaded(QString id, QDBusObjectPath path)
{
  qDebug() << "Unit loaded: " << id << " (" << path.path() << ")";
}

void kcmsystemd::slotUnitUnloaded(QString id, QDBusObjectPath path)
{
  qDebug() << "Unit unloaded: " << id << " (" << path.path() << ")";
}
*/

void kcmsystemd::slotSystemUnitsChanged()
{
  // qDebug() << "System units changed";
  slotRefreshUnitsList(false, sys);
}

void kcmsystemd::slotUserUnitsChanged()
{
  // qDebug() << "User units changed";
  slotRefreshUnitsList(false, user);
}

void kcmsystemd::slotLogindPropertiesChanged(QString, QVariantMap, QStringList)
{
  // qDebug() << "Logind properties changed on iface " << iface_name;
  slotRefreshSessionList();
}

void kcmsystemd::slotLeSearchUnitChanged(QString term)
{
  if (QObject::sender()->objectName() == "leSearchUnit")
  {
    systemUnitFilterModel->addFilterRegExp(unitName, term);
    systemUnitFilterModel->invalidate();
    ui.tblUnits->sortByColumn(ui.tblUnits->horizontalHeader()->sortIndicatorSection(),
                              ui.tblUnits->horizontalHeader()->sortIndicatorOrder());
  }
  else if (QObject::sender()->objectName() == "leSearchUserUnit")
  {
    userUnitFilterModel->addFilterRegExp(unitName, term);
    userUnitFilterModel->invalidate();
    ui.tblUserUnits->sortByColumn(ui.tblUserUnits->horizontalHeader()->sortIndicatorSection(),
                                  ui.tblUserUnits->horizontalHeader()->sortIndicatorOrder());
  }
  updateUnitCount();
}

void kcmsystemd::slotCmbConfFileChanged(int index)
{
  ui.lblConfFile->setText(i18n("File to be written: %1/%2", etcDir, listConfFiles.at(index)));

  proxyModelConf->setFilterRegExp(ui.cmbConfFile->itemText(index));
  proxyModelConf->setFilterKeyColumn(2);
}

void kcmsystemd::slotUpdateTimers()
{
  // Updates the left and passed columns in the timers list
  for (int row = 0; row < timerModel->rowCount(); ++row)
  {
    QDateTime next = timerModel->index(row, 1).data().toDateTime();
    QDateTime last = timerModel->index(row, 3).data().toDateTime();
    QDateTime current = QDateTime().currentDateTime();
    qlonglong leftSecs = current.secsTo(next);
    qlonglong passedSecs = last.secsTo(current);

    QString left;
    if (leftSecs >= 31536000)
      left = QString::number(leftSecs / 31536000) + " years";
    else if (leftSecs >= 604800)
      left = QString::number(leftSecs / 604800) + " weeks";
    else if (leftSecs >= 86400)
      left = QString::number(leftSecs / 86400) + " days";
    else if (leftSecs >= 3600)
      left = QString::number(leftSecs / 3600) + " hr";
    else if (leftSecs >= 60)
      left = QString::number(leftSecs / 60) + " min";
    else if (leftSecs < 0)
      left = "0 s";
    else
      left = QString::number(leftSecs) + " s";
    timerModel->setData(timerModel->index(row, 2), left);

    QString passed;
    if (timerModel->index(row, 3).data() == "n/a")
      passed = "n/a";
    else if (passedSecs >= 31536000)
      passed = QString::number(passedSecs / 31536000) + " years";
    else if (passedSecs >= 604800)
      passed = QString::number(passedSecs / 604800) + " weeks";
    else if (passedSecs >= 86400)
      passed = QString::number(passedSecs / 86400) + " days";
    else if (passedSecs >= 3600)
      passed = QString::number(passedSecs / 3600) + " hr";
    else if (passedSecs >= 60)
      passed = QString::number(passedSecs / 60) + " min";
    else if (passedSecs < 0)
      passed = "0 s";
    else
      passed = QString::number(passedSecs) + " s";
    timerModel->setData(timerModel->index(row, 4), passed);
  }
}

QList<SystemdUnit> kcmsystemd::getUnitsFromDbus(dbusBus bus)
{
  // get an updated list of units via dbus

  QList<SystemdUnit> list;
  QList<unitfile> unitfileslist;
  QDBusMessage dbusreply;

  dbusreply = callDbusMethod("ListUnits", sysdMgr, bus);

  const QDBusArgument argUnits = dbusreply.arguments().at(0).value<QDBusArgument>();
  int tal = 0;
  if (argUnits.currentType() == QDBusArgument::ArrayType)
  {
    argUnits.beginArray();
    while (!argUnits.atEnd())
    {
      SystemdUnit unit;
      argUnits >> unit;
      list.append(unit);

      // qDebug() << "Added unit " << unit.id;
      tal++;
    }
    argUnits.endArray();
  }
  // qDebug() << "Added " << tal << " units on bus " << bus;
  tal = 0;

  // Get a list of unit files
  dbusreply = callDbusMethod("ListUnitFiles", sysdMgr, bus);
  const QDBusArgument argUnitFiles = dbusreply.arguments().at(0).value<QDBusArgument>();
  argUnitFiles.beginArray();
  while (!argUnitFiles.atEnd())
  {
    unitfile u;
    argUnitFiles.beginStructure();
    argUnitFiles >> u.name >> u.status;
    argUnitFiles.endStructure();
    unitfileslist.append(u);
  }
  argUnitFiles.endArray();

  // Add unloaded units to the list
  for (int i = 0;  i < unitfileslist.size(); ++i)
  {
    int index = list.indexOf(SystemdUnit(unitfileslist.at(i).name.section('/',-1)));
    if (index > -1)
    {
      // The unit was already in the list, add unit file and its status
      list[index].unit_file = unitfileslist.at(i).name;
      list[index].unit_file_status = unitfileslist.at(i).status;
    }
    else
    {
      // Unit not in the list, add it
      QFile unitfile(unitfileslist.at(i).name);
      if (unitfile.symLinkTarget().isEmpty())
      {
        SystemdUnit unit;
        unit.id = unitfileslist.at(i).name.section('/',-1);
        unit.load_state = "unloaded";
        unit.active_state = "-";
        unit.sub_state = "-";
        unit.unit_file = unitfileslist.at(i).name;
        unit.unit_file_status= unitfileslist.at(i).status;
        list.append(unit);

        // qDebug() << "Added unit " << unit.id;
        tal++;
      }
    }
  }
  // qDebug() << "Added " << tal << " units from files on bus " << bus;

  return list;
}

QVariant kcmsystemd::getDbusProperty(QString prop, dbusIface ifaceName, QDBusObjectPath path, dbusBus bus)
{
  // qDebug() << "Fetching property" << prop << ifaceName << path.path() << "on bus" << bus;
  QString conn, ifc;
  QDBusConnection abus("");
  if (bus == user)
    abus = QDBusConnection::connectToBus(userBusPath, connSystemd);
  else
    abus = systembus;

  if (ifaceName == sysdMgr)
  {
    conn = connSystemd;
    ifc = ifaceMgr;
  }
  else if (ifaceName == sysdUnit)
  {
    conn = connSystemd;
    ifc = ifaceUnit;
  }
  else if (ifaceName == sysdTimer)
  {
    conn = connSystemd;
    ifc = ifaceTimer;
  }
  else if (ifaceName == logdSession)
  {
    conn = connLogind;
    ifc = ifaceSession;
  }
  QVariant r;
  QDBusInterface *iface = new QDBusInterface (conn, path.path(), ifc, abus, this);
  if (iface->isValid())
  {
    r = iface->property(prop.toLatin1());
    delete iface;
    return r;
  }
  qDebug() << "Interface" << ifc << "invalid for" << path.path();
  return QVariant("invalidIface");
}

QDBusMessage kcmsystemd::callDbusMethod(QString method, dbusIface ifaceName, dbusBus bus, const QList<QVariant> &args)
{
  // qDebug() << "Calling method" << method << "with iface" << ifaceName << "on bus" << bus;
  QDBusConnection abus("");
  if (bus == user)
    abus = QDBusConnection::connectToBus(userBusPath, connSystemd);
  else
    abus = systembus;

  QDBusInterface *iface;
  if (ifaceName == sysdMgr)
    iface = new QDBusInterface (connSystemd, pathSysdMgr, ifaceMgr, abus, this);
  else if (ifaceName == logdMgr)
    iface = new QDBusInterface (connLogind, pathLogdMgr, ifaceLogdMgr, abus, this);

  QDBusMessage msg;
  if (iface->isValid())
  {
    if (args.isEmpty())
      msg = iface->call(QDBus::AutoDetect, method.toLatin1());
    else
      msg = iface->callWithArgumentList(QDBus::AutoDetect, method.toLatin1(), args);
    delete iface;
    if (msg.type() == QDBusMessage::ErrorMessage)
      qDebug() << "DBus method call failed: " << msg.errorMessage();
  }
  else
  {
    qDebug() << "Invalid DBus interface!";
    delete iface;
  }
  return msg;
}

#include "kcmsystemd.moc"
