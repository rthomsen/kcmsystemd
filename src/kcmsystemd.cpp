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
#include <KAuth/kauth.h>
using namespace KAuth;

#include <boost/filesystem.hpp>

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
  setNeedsAuthorization(true);

  // See if systemd is reachable via dbus
  QDBusConnection systembus = QDBusConnection::systemBus();
  QDBusInterface *iface = new QDBusInterface ("org.freedesktop.systemd1",
					      "/org/freedesktop/systemd1",
					      "org.freedesktop.systemd1.Manager",
					      systembus,
					      this);
  if (iface->isValid())
  {
    systemdVersion = iface->property("Version").toString().remove("systemd ").toInt();
    qDebug() << "Systemd" << systemdVersion << "detected.";
  }
  else
  {
    qDebug() << "Unable to contact systemd daemon!";
    ui.stackedWidget->setCurrentIndex(1);
  }
  delete iface;

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
  
  varLogDirExists = QDir("/var/log/journal").exists();
  if (varLogDirExists)
    isPersistent = true;
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
  
  // Subscribe to dbus signals from systemd daemon and connect them to slots
  iface = new QDBusInterface ("org.freedesktop.systemd1",
					      "/org/freedesktop/systemd1",
					      "org.freedesktop.systemd1.Manager",
					      systembus,
					      this);
  if (iface->isValid())
    iface->call(QDBus::AutoDetect, "Subscribe");
  delete iface;
  QDBusConnection::systemBus().connect("org.freedesktop.systemd1","/org/freedesktop/systemd1","org.freedesktop.systemd1.Manager","Reloading",this,SLOT(slotSystemdReloading(bool)));
  // QDBusConnection::systemBus().connect("org.freedesktop.systemd1","/org/freedesktop/systemd1","org.freedesktop.systemd1.Manager","UnitNew",this,SLOT(slotUnitLoaded(QString, QDBusObjectPath)));
  // QDBusConnection::systemBus().connect("org.freedesktop.systemd1","/org/freedesktop/systemd1","org.freedesktop.systemd1.Manager","UnitRemoved",this,SLOT(slotUnitUnloaded(QString, QDBusObjectPath)));
  QDBusConnection::systemBus().connect("org.freedesktop.systemd1","/org/freedesktop/systemd1","org.freedesktop.systemd1.Manager","UnitFilesChanged",this,SLOT(slotUnitFilesChanged()));
  QDBusConnection::systemBus().connect("org.freedesktop.systemd1","","org.freedesktop.DBus.Properties","PropertiesChanged",this,SLOT(slotPropertiesChanged(QString, QVariantMap, QStringList)));
  
  // Setup the unitslist
  setupUnitslist();
  
  setupConf();
}

kcmsystemd::~kcmsystemd()
{
}

QDBusArgument &operator<<(QDBusArgument &argument, const SystemdUnit &service)
{
  argument.beginStructure();
  argument << service.id
	   << service.description
 	   << service.load_state
 	   << service.active_state
 	   << service.sub_state
	   << service.following
	   << service.unit_path
	   << service.job_id
	   << service.job_type
	   << service.job_path;
  argument.endStructure();
  return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, SystemdUnit &service)
{
     argument.beginStructure();
     argument >> service.id
	      >> service.description
	      >> service.load_state
	      >> service.active_state
	      >> service.sub_state
	      >> service.following
	      >> service.unit_path
	      >> service.job_id
	      >> service.job_type
	      >> service.job_path;
     argument.endStructure();
     return argument;
}

void kcmsystemd::setupSignalSlots()
{
  // Connect signals for units tab
  connect(ui.btnRefreshUnits, SIGNAL(clicked()), this, SLOT(slotRefreshUnitsList()));
  connect(ui.chkInactiveUnits, SIGNAL(stateChanged(int)), this, SLOT(slotChkShowUnits()));
  connect(ui.chkShowUnloadedUnits, SIGNAL(stateChanged(int)), this, SLOT(slotChkShowUnits()));
  connect(ui.cmbUnitTypes, SIGNAL(currentIndexChanged(int)), this, SLOT(slotCmbUnitTypes()));
  connect(ui.tblServices, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(slotDisplayMenu(QPoint)));
  connect(ui.leSearchUnit, SIGNAL(textChanged(QString)), this, SLOT(slotLeSearchUnitChanged(QString)));

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
  map["defUnit"] = confOption::s;
  map["toolTip"] = i18n("<p>The watchdog hardware (/dev/watchdog) will be programmed to automatically reboot the system if it is not contacted within the specified timeout interval. The system manager will ensure to contact it at least once in half the specified timeout interval. This feature requires a hardware watchdog device.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "ShutdownWatchdogSec";
  map["file"] = SYSTEMD;
  map["type"] = TIME;
  map["defVal"] = 10;
  map["defUnit"] = confOption::min;
  map["defReadUnit"] = confOption::s;
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
  map["toolTip"] = i18n("<p>Sets the timer slack for PID 1 which is then inherited to all executed processes, unless overridden individually. The timer slack controls the accuracy of wake-ups triggered by timers.</p>");
  confOptList.append(confOption(map));

  if (systemdVersion >= 212)
  {
    map.clear();
    map["name"] = "DefaultTimerAccuracySec";
    map["file"] = SYSTEMD;
    map["type"] = TIME;
    map["defVal"] = 60;
    map["defUnit"] = confOption::s;
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
    map["defUnit"] = confOption::s;
    map["toolTip"] = i18n("<p>The default timeout for starting of units.</p>");
    confOptList.append(confOption(map));

    map.clear();
    map["name"] = "DefaultTimeoutStopSec";
    map["file"] = SYSTEMD;
    map["type"] = TIME;
    map["defVal"] = 90;
    map["defUnit"] = confOption::s;
    map["toolTip"] = i18n("<p>The default timeout for stopping of units.</p>");
    confOptList.append(confOption(map));

    map.clear();
    map["name"] = "DefaultRestartSec";
    map["file"] = SYSTEMD;
    map["type"] = TIME;
    map["defVal"] = 100;
    map["defUnit"] = confOption::ms;
    map["defReadUnit"] = confOption::s;
    map["toolTip"] = i18n("<p>The default time to sleep between automatic restart of units.</p>");
    confOptList.append(confOption(map));

    map.clear();
    map["name"] = "DefaultStartLimitInterval";
    map["file"] = SYSTEMD;
    map["type"] = TIME;
    map["defVal"] = 10;
    map["defUnit"] = confOption::s;
    map["toolTip"] = i18n("<p>Time interval used in start rate limit for services.</p>");
    confOptList.append(confOption(map));

    map.clear();
    map["name"] = "DefaultStartLimitBurst";
    map["file"] = SYSTEMD;
    map["type"] = INTEGER;
    map["defVal"] = 5;
    map["minVal"] = 0;
    map["maxVal"] = 99999;
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
  map["defReadUnit"] = confOption::s;
  map["toolTip"] = i18n("<p>The timeout before synchronizing journal files to disk.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "RateLimitInterval";
  map["file"] = JOURNALD;
  map["type"] = TIME;
  map["defVal"] = 30;
  map["defUnit"] = confOption::s;
  map["toolTip"] = i18n("<p>Time interval for rate limiting of log messages. Set to 0 to turn off rate-limiting.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "RateLimitBurst";
  map["file"] = JOURNALD;
  map["type"] = INTEGER;
  map["defVal"] = 1000;
  map["minVal"] = 0;
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
  map["defReadUnit"] = confOption::s;
  map["toolTip"] = i18n("<p>Maximum time to store journal entries. Set to 0 to disable.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "MaxFileSec";
  map["file"] = JOURNALD;
  map["type"] = TIME;
  map["defVal"] = 30;
  map["defUnit"] = confOption::d;
  map["defReadUnit"] = confOption::s;
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
  map["minVal"] = 0;
  map["maxVal"] = 12;
  map["toolTip"] = i18n("<p>Number of virtual terminals (VTs) to allocate by default and which will autospawn. Set to 0 to disable.</p>");
  confOptList.append(confOption(map));

  map.clear();
  map["name"] = "ReserveVT";
  map["file"] = LOGIND;
  map["type"] = INTEGER;
  map["defVal"] = 6;
  map["minVal"] = 0;
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
  map["defUnit"] = confOption::s;
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
  map["defUnit"] = confOption::s;
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
        int index = confOptList.indexOf(confOption(QString(line.section("=",0,0).trimmed()) + "_" + QString::number(fileindex)));
        if (index > 0)
        {
          // qDebug() << "found " << confOptList.at(index).uniqueName << " at index " << index;
          confOptList[index].setValueFromFile(line);
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
  // Register the meta type for storing units
  qDBusRegisterMetaType<SystemdUnit>();
  
  // Initialize some variables
  timesLoad = 0, lastRowChecked = 0;
  
  // Setup models for unitslist
  unitsModel = new QStandardItemModel(this);
  proxyModelAct = new QSortFilterProxyModel (this);
  proxyModelAct->setDynamicSortFilter(false);
  proxyModelUnitId = new QSortFilterProxyModel (this);
  proxyModelUnitId->setDynamicSortFilter(false);
  proxyModelAct->setSourceModel(unitsModel);
  proxyModelUnitId->setSourceModel(proxyModelAct);

  // Install eventfilter to capture mouse move events
  ui.tblServices->viewport()->installEventFilter(this);
  
  // Set header row
  unitsModel->setHorizontalHeaderItem(0, new QStandardItem(i18n("Load state")));
  unitsModel->setHorizontalHeaderItem(1, new QStandardItem(i18n("Active state")));
  unitsModel->setHorizontalHeaderItem(2, new QStandardItem(i18n("Unit state")));
  unitsModel->setHorizontalHeaderItem(3, new QStandardItem(i18n("Unit")));

  // Set model for QTableView (should be called after headers are set)
  ui.tblServices->setModel(proxyModelUnitId);
  
  // Setup initial filters and sorting
  ui.tblServices->sortByColumn(3, Qt::AscendingOrder);
  proxyModelUnitId->setSortCaseSensitivity(Qt::CaseInsensitive);
  slotChkShowUnits();
  
  // Add all the units
  slotRefreshUnitsList();
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

void kcmsystemd::slotChkShowUnits()
{
  if (ui.chkInactiveUnits->isChecked()) {
    ui.chkShowUnloadedUnits->setEnabled(true);
    if (ui.chkShowUnloadedUnits->isChecked())
      proxyModelAct->setFilterRegExp("");
    else
      proxyModelAct->setFilterRegExp("active");
  } else {
    ui.chkShowUnloadedUnits->setEnabled(false);
    proxyModelAct->setFilterRegExp(QRegExp("^(active)"));
  }
  proxyModelAct->setFilterKeyColumn(1);
  ui.tblServices->sortByColumn(ui.tblServices->horizontalHeader()->sortIndicatorSection(), ui.tblServices->horizontalHeader()->sortIndicatorOrder());
  updateUnitCount();
}

void kcmsystemd::slotCmbUnitTypes()
{
  // Filter unit list for a selected unit type
  switch (ui.cmbUnitTypes->currentIndex())
  {
  case 0:
    filterUnitType = "";
    break;
  case 1:
    filterUnitType = ".target";
    break;
  case 2:
    filterUnitType = ".service";
    break;
  case 3:
    filterUnitType = ".device";
    break;
  case 4:
    filterUnitType = ".mount";
    break;
  case 5:
    filterUnitType = ".automount";
    break;
  case 6:
    filterUnitType = ".swap";
    break;
  case 7:
    filterUnitType = ".socket";
    break;
  case 8:
    filterUnitType = ".path";
    break;
  case 9:
    filterUnitType = ".timer";
    break;
  case 10:
    filterUnitType = ".snapshot";
    break;
  case 11:
    filterUnitType = ".slice";
    break;
  case 12:
    filterUnitType = ".scope";
  }
  proxyModelUnitId->setFilterRegExp(QRegExp("(?=.*" + searchTerm + ")(?=.*" + filterUnitType + "$)",
                                            Qt::CaseInsensitive,
                                            QRegExp::RegExp));
  proxyModelUnitId->setFilterKeyColumn(3);
  ui.tblServices->sortByColumn(ui.tblServices->horizontalHeader()->sortIndicatorSection(), ui.tblServices->horizontalHeader()->sortIndicatorOrder());
  updateUnitCount();
}

void kcmsystemd::slotRefreshUnitsList()
{
  // qDebug() << "Refreshing units...";
  // clear lists
  unitslist.clear();
  unitfileslist.clear();
  unitpaths.clear();
  noActUnits = 0;
  
  // get an updated list of units via dbus
  QDBusMessage dbusreply;
  QDBusConnection systembus = QDBusConnection::systemBus();
  QDBusInterface *iface = new QDBusInterface ("org.freedesktop.systemd1",
					      "/org/freedesktop/systemd1",
					      "org.freedesktop.systemd1.Manager",
					      systembus,
					      this);
  if (iface->isValid())
    dbusreply = iface->call(QDBus::AutoDetect, "ListUnits");
  delete iface;
  const QDBusArgument myArg = dbusreply.arguments().at(0).value<QDBusArgument>();
  if (myArg.currentType() == QDBusArgument::ArrayType)
  {
    myArg.beginArray();
    while (!myArg.atEnd())
    {
      SystemdUnit unit;
      myArg >> unit;
      unitslist.append(unit);
      unitpaths[unit.id] = unit.unit_path.path();
    }
    myArg.endArray();
  }
  
  // Find unit files
  iface = new QDBusInterface ("org.freedesktop.systemd1",
			      "/org/freedesktop/systemd1",
			      "org.freedesktop.systemd1.Manager",
			      systembus,
			      this);
  if (iface->isValid())
  {
    dbusreply = iface->call("ListUnitFiles");
    const QDBusArgument arg = dbusreply.arguments().at(0).value<QDBusArgument>();
    arg.beginArray();
    while (!arg.atEnd())
    {
      unitfile u;
      arg.beginStructure();
      arg >> u.name >> u.status;
      arg.endStructure();
      unitfileslist.append(u);
    }
    arg.endArray();
  }
  delete iface;

  // Add unloaded units to the list
  for (int i = 0;  i < unitfileslist.size(); ++i)
  { 
    if (!unitpaths.contains(unitfileslist.at(i).name.section('/',-1)))
    {
      QFile unitfile(unitfileslist.at(i).name);
      if (unitfile.symLinkTarget().isEmpty())
      {
        SystemdUnit unit;
        unit.id = unitfileslist.at(i).name.section('/',-1);
        unit.load_state = "unloaded";
        unit.active_state = "-";
        unit.sub_state = "-";
        unitslist.append(unit);
      }
    }
  }

  // Iterate through the new list and compare to model
  for (int i = 0;  i < unitslist.size(); ++i)
  {
    QList<QStandardItem *> items = unitsModel->findItems(unitslist.at(i).id, Qt::MatchExactly, 3);
    if (items.isEmpty())
    {
      // New unit discovered so add it to the model
      // qDebug() << "Found new unit: " << unitslist.at(i).id;
      QList<QStandardItem *> row;
      row <<
      new QStandardItem(unitslist.at(i).load_state) <<
      new QStandardItem(unitslist.at(i).active_state) <<
      new QStandardItem(unitslist.at(i).sub_state) <<
      new QStandardItem(unitslist.at(i).id);
      unitsModel->appendRow(row);
      ui.tblServices->sortByColumn(ui.tblServices->horizontalHeader()->sortIndicatorSection(), ui.tblServices->horizontalHeader()->sortIndicatorOrder());
    } else {
      // Update stats for previously known units
      unitsModel->item(items.at(0)->row(), 0)->setData(unitslist.at(i).load_state, Qt::DisplayRole);
      unitsModel->item(items.at(0)->row(), 1)->setData(unitslist.at(i).active_state, Qt::DisplayRole);
      unitsModel->item(items.at(0)->row(), 2)->setData(unitslist.at(i).sub_state, Qt::DisplayRole);
    }
    if (unitslist.at(i).active_state == "active")
      noActUnits++;
  }
  
  // Check to see if any units were removed
  if (unitsModel->rowCount() != unitslist.size())
  {
    QList<QPersistentModelIndex> indexes;
    // Loop through model and compare to retrieved unitslist
    for (int row = 0; row < unitsModel->rowCount(); ++row)
    {
      SystemdUnit unit;
      unit.id = unitsModel->index(row,3).data().toString();    
      if (!unitslist.contains(unit))
      {
	// Add removed units to list for deletion
        // qDebug() << "Unit removed: " << unitsModel->index(row,3).data().toString();
	indexes << unitsModel->index(row,3);
      }
    }
    // Delete the identified units from model
    foreach (QPersistentModelIndex i, indexes)
      unitsModel->removeRow(i.row());
  }
  
  // Update the text color in model
  QColor newcolor;
  for (int row = 0; row < unitsModel->rowCount(); ++row)
  {
    if (unitsModel->data(unitsModel->index(row,1), Qt::DisplayRole) == "inactive")
      newcolor = Qt::red;
    else if (unitsModel->data(unitsModel->index(row,1), Qt::DisplayRole) == "active")
      newcolor = Qt::darkGreen;
    else if (unitsModel->data(unitsModel->index(row,1), Qt::DisplayRole) == "failed")
      newcolor = Qt::darkRed;
    else if (unitsModel->data(unitsModel->index(row,1), Qt::DisplayRole) == "-")
      newcolor = Qt::darkGray;
    else
      newcolor = Qt::black;
    for (int col = 0; col < 4; ++col)
      unitsModel->setData(unitsModel->index(row,col), QVariant(newcolor), Qt::ForegroundRole);
  }
  
  slotChkShowUnits();
}

void kcmsystemd::updateUnitCount()
{
  ui.lblStatus->setText(i18n("Total: %1 units, %2 active, %3 displayed",
                             QString::number(unitsModel->rowCount()),
                             QString::number(noActUnits),
                             QString::number(proxyModelUnitId->rowCount())));
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

void kcmsystemd::slotDisplayMenu(const QPoint &pos)
{
  // Slot for creating the right-click menu in the units list

  // Find name of unit
  QString unit = ui.tblServices->model()->index(ui.tblServices->indexAt(pos).row(),3).data().toString();

  // Setup DBus interfaces
  QString service = "org.freedesktop.systemd1";
  QString pathManager = "/org/freedesktop/systemd1";
  QString pathUnit = unitpaths[unit].toString();
  QString ifaceManager = "org.freedesktop.systemd1.Manager";
  QString ifaceUnit = "org.freedesktop.systemd1.Unit";
  QDBusConnection systembus = QDBusConnection::systemBus();
  QDBusInterface *dbusIfaceManager = new QDBusInterface (service, pathManager, ifaceManager, systembus, this);
  QDBusInterface *dbusIfaceUnit = new QDBusInterface (service, pathUnit, ifaceUnit, systembus, this);

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
  QString UnitFileState = dbusIfaceManager->callWithArgumentList(QDBus::AutoDetect, "GetUnitFileState", args).arguments().at(0).toString();

  // Check if unit can be isolated
  QString LoadState, ActiveState;
  bool CanStart, CanStop, CanReload;
  if (dbusIfaceUnit->isValid())
  {
    // Unit has a Unit DBus object, fetch properties
    isolate->setEnabled(dbusIfaceUnit->property("CanIsolate").toBool());
    LoadState = dbusIfaceUnit->property("LoadState").toString();
    ActiveState = dbusIfaceUnit->property("ActiveState").toString();
    CanStart = dbusIfaceUnit->property("CanStart").toBool();
    CanStop = dbusIfaceUnit->property("CanStop").toBool();
    CanReload = dbusIfaceUnit->property("CanReload").toBool();
  }
  else
  {
    // No Unit DBus object, only enable Start
    isolate->setEnabled(false);
    CanStart = true;
    CanStop = false;
    CanReload = false;
  }

  if (CanStart &&
      ActiveState != "active")
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
  {
    disable->setEnabled(false);
  } else if (UnitFileState == "enabled") {
    enable->setEnabled(false);
  } else {
    enable->setEnabled(false);    
    disable->setEnabled(false);
  }

  if (LoadState == "masked")
  {
    mask->setEnabled(false);
  } else if (LoadState != "masked") {
    unmask->setEnabled(false);
  }
  
  // Check if unit has a unit file, if not disable editing
  QString frpath;
  unitfile afile;
  afile.name = unit;
  int index = unitfileslist.indexOf(afile);
  if (index != -1)
    frpath = unitfileslist.at(index).name;
  if (frpath.isEmpty())
    edit->setEnabled(false);

  QAction *a = menu.exec(ui.tblServices->viewport()->mapToGlobal(pos));
   
  if (a == edit)
  {
    QProcess kwrite (this);
    QString alt1 = QString(kdePrefix + "/lib/libexec/kdesu");
    QString alt2 = QString(kdePrefix + "/bin/kdesu");
    if (QFile(alt1).exists())
      kwrite.startDetached(alt1, QStringList() << "-t" << "--" << "kwrite" << frpath);
    else if (QFile(alt2).exists())
      kwrite.startDetached(alt2, QStringList() << "-t" << "--" << "kwrite" << frpath);
    else
      KMessageBox::error(this, i18n("kdesu executable not found. Unable to start kwrite!"));
    return;
  }

  QList<QString> unitsForCall;
  unitsForCall << unit;
  QString method;

  if (a == start)
  {
    QList<QVariant> argsForCall;
    argsForCall << unit << "replace";
    method = "StartUnit";
    authServiceAction(service, pathManager, ifaceManager, method, argsForCall);
  }
  else if (a == stop)
  {
    QList<QVariant> argsForCall;
    argsForCall << unit << "replace";
    method = "StopUnit";
    authServiceAction(service, pathManager, ifaceManager, method, argsForCall);
  }
  else if (a == restart)
  {
    QList<QVariant> argsForCall;
    argsForCall << unit << "replace";
    method = "RestartUnit";
    authServiceAction(service, pathManager, ifaceManager, method, argsForCall);
  }
  else if (a == reload)
  {
    QList<QVariant> argsForCall;
    argsForCall << unit << "replace";
    method = "ReloadUnit";
    authServiceAction(service, pathManager, ifaceManager, method, argsForCall);
  }
  else if (a == isolate)
  {
    QList<QVariant> argsForCall;
    argsForCall << unit << "isolate";
    method = "StartUnit";
    authServiceAction(service, pathManager, ifaceManager, method, argsForCall);
  }
  else if (a == enable)
  {
    QList<QVariant> argsForCall;
    argsForCall << QVariant(unitsForCall) << false << true;
    method = "EnableUnitFiles";
    authServiceAction(service, pathManager, ifaceManager, method, argsForCall);
  }
  else if (a == disable)
  {
    QList<QVariant> argsForCall;
    argsForCall << QVariant(unitsForCall) << false;
    method = "DisableUnitFiles";
    authServiceAction(service, pathManager, ifaceManager, method, argsForCall);
  }
  else if (a == mask)
  {
    QList<QVariant> argsForCall;
    argsForCall << QVariant(unitsForCall) << false << true;
    method = "MaskUnitFiles";
    authServiceAction(service, pathManager, ifaceManager, method, argsForCall);
  }
  else if (a == unmask)
  {
    QList<QVariant> argsForCall;
    argsForCall << QVariant(unitsForCall) << false;
    method = "UnmaskUnitFiles";
    authServiceAction(service, pathManager, ifaceManager, method, argsForCall);
  }
  else if (a == reloaddaemon)
  {
    QList<QVariant> argsForCall;
    method = "Reload";
    authServiceAction(service, pathManager, ifaceManager, method, argsForCall);
  }
  else if (a == reexecdaemon)
  {
    QList<QVariant> argsForCall;
    method = "Reexecute";
    authServiceAction(service, pathManager, ifaceManager, method, argsForCall);
  }

  delete dbusIfaceManager;
  delete dbusIfaceUnit;
}

bool kcmsystemd::eventFilter(QObject *, QEvent* event)
{
  // Eventfilter for catching mouse move events over the unit list
  // Used for dynamically generating tooltips for units
  if (event->type() == QEvent::MouseMove)
  {
    QMouseEvent *me = static_cast<QMouseEvent*>(event);
    QModelIndex inUnitsModel, inProxyModelAct, inProxyModelUnitId;
    inProxyModelUnitId = ui.tblServices->indexAt(me->pos());
    inProxyModelAct = proxyModelUnitId->mapToSource(inProxyModelUnitId);
    inUnitsModel = proxyModelAct->mapToSource(inProxyModelAct);
    if (!inUnitsModel.isValid())
      return false;

    if (unitsModel->itemFromIndex(inUnitsModel)->row() != lastRowChecked)
    {
      // Cursor moved to a different row. Only build tooltips when moving
      // cursor to a new row to avoid excessive DBus calls.

      QString selUnit = ui.tblServices->model()->index(ui.tblServices->indexAt(me->pos()).row(),3).data().toString();
      // qDebug() << "selUnit: " << selUnit;

      QString toolTipText;
      toolTipText.append("<FONT COLOR=white>");
      toolTipText.append("<b>" + selUnit + "</b><hr>");

      // Create a DBus interface
      QDBusConnection systembus = QDBusConnection::systemBus();
      QDBusInterface *iface;
      iface = new QDBusInterface ("org.freedesktop.systemd1",
                                  unitpaths[selUnit].toString(),
                                  "org.freedesktop.systemd1.Unit",
                                  systembus,
                                  this);

      // Use the DBus interface to get unit properties
      if (iface->isValid())
      {
        // Unit has a valid unit DBus object
        toolTipText.append(i18n("<b>Description: </b>"));
        toolTipText.append(iface->property("Description").toString());
        toolTipText.append(i18n("<br><b>Fragment path: </b>"));
        toolTipText.append(iface->property("FragmentPath").toString());
        toolTipText.append(i18n("<br><b>Unit file state: </b>"));
        toolTipText.append(iface->property("UnitFileState").toString());

        toolTipText.append(i18n("<br><b>Activated: </b>"));
        if (iface->property("ActiveEnterTimestamp").toULongLong() == 0)
          toolTipText.append("n/a");
        else
        {
          QDateTime timeActivated;
          timeActivated.setMSecsSinceEpoch(iface->property("ActiveEnterTimestamp").toULongLong()/1000);
          toolTipText.append(timeActivated.toString());
        }

        toolTipText.append(i18n("<br><b>Deactivated: </b>"));
        if (iface->property("InactiveEnterTimestamp").toULongLong() == 0)
          toolTipText.append("n/a");
        else
        {
          QDateTime timeDeactivated;
          timeDeactivated.setMSecsSinceEpoch(iface->property("InactiveEnterTimestamp").toULongLong()/1000);
          toolTipText.append(timeDeactivated.toString());
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
                                    systembus,
                                    this);
        QList<QVariant> args;
        args << selUnit;
        unitfile a;
        a.name = selUnit;

        toolTipText.append(i18n("<b>Fragment path: </b>"));
        if (!a.name.isEmpty())
          toolTipText.append(unitfileslist.at(unitfileslist.indexOf(a)).name);

        toolTipText.append(i18n("<br><b>Unit file state: </b>"));
        if (!a.name.isEmpty())
          toolTipText.append(iface->callWithArgumentList(QDBus::AutoDetect, "GetUnitFileState", args).arguments().at(0).toString());

        delete iface;
      }

      toolTipText.append("</FONT");
      unitsModel->itemFromIndex(inUnitsModel)->setToolTip(toolTipText);

      lastRowChecked = unitsModel->itemFromIndex(inUnitsModel)->row();
      return true;

    } // Row was different
  } // Cursor was moved
  return false;
  // return true;
}

void kcmsystemd::slotSystemdReloading(bool status)
{
  if (status)
    qDebug() << "Systemd reloading...";
  else
    slotRefreshUnitsList();
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

void kcmsystemd::slotUnitFilesChanged()
{
  // qDebug() << "Unit files changed.";
  // slotRefreshUnitsList();
}

void kcmsystemd::slotPropertiesChanged(QString iface_name, __attribute__((unused)) QVariantMap changed_props, __attribute__((unused)) QStringList invalid_props)
{
  // qDebug() << "Properties changed.";
  // This signal gets emitted on two different interfaces,
  // but no reason to call slotRefreshUnitsList() twice
  if (iface_name == "org.freedesktop.systemd1.Unit")
    slotRefreshUnitsList();
  updateUnitCount();
}

void kcmsystemd::slotLeSearchUnitChanged(QString term)
{
  searchTerm = term;
  proxyModelUnitId->setFilterRegExp(QRegExp("(?=.*" + searchTerm + ")(?=.*" + filterUnitType + "$)", Qt::CaseInsensitive, QRegExp::RegExp));
  proxyModelUnitId->setFilterKeyColumn(3);
  int section = ui.tblServices->horizontalHeader()->sortIndicatorSection();
  Qt::SortOrder order = ui.tblServices->horizontalHeader()->sortIndicatorOrder();
  ui.tblServices->sortByColumn(section, order);
  updateUnitCount();
}

void kcmsystemd::slotCmbConfFileChanged(int index)
{
  ui.lblConfFile->setText(i18n("File to be written: %1/%2", etcDir, listConfFiles.at(index)));

  proxyModelConf->setFilterRegExp(ui.cmbConfFile->itemText(index));
  proxyModelConf->setFilterKeyColumn(2);
}

#include "kcmsystemd.moc"
