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

  // ConfItem item = ConfItem("test");
  
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

  // Use kde4-config to get kde prefix
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
    ui.lblFailMessage->setText("Unable to find directory with systemd configuration files.");
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
  // Only initialize the interface once
  if (timesLoad == 0)
    initializeInterface();
  
  // Read the four configuration files
  readConfFile("system.conf");
  readConfFile("journald.conf");
  readConfFile("logind.conf");
  if (systemdVersion >= 215)
    readConfFile("coredump.conf");
  
  // Connect signals to slots, which need to be after initializeInterface()
  connect(confModel, SIGNAL(dataChanged(const QModelIndex &, const QModelIndex &)), this, SLOT(slotConfChanged(const QModelIndex &, const QModelIndex &)));
}

void kcmsystemd::setupConfigParms()
{
  // Setup classes for all configuration options in conf.files
  
  // system.conf
  QStringList LogLevelList = QStringList() << "emerg" << "alert" << "crit" << "err" << "warning" << "notice" << "info" << "debug";
  confOptList.append(confOption(SYSTEMD, "LogLevel", LIST, "info", LogLevelList));
  QStringList LogTargetList = QStringList() << "console" << "journal" << "syslog" << "kmsg" << "journal-or-kmsg" << "syslog-or-kmsg" << "null";
  confOptList.append(confOption(SYSTEMD, "LogTarget", LIST, "journal-or-kmsg", LogTargetList));  
  confOptList.append(confOption(SYSTEMD, "LogColor", BOOL, true));
  confOptList.append(confOption(SYSTEMD, "LogLocation", BOOL, true));
  confOptList.append(confOption(SYSTEMD, "DumpCore", BOOL, true));
  confOptList.append(confOption(SYSTEMD, "CrashShell", BOOL, false));
  QStringList ShowStatusList = QStringList() << "yes" << "no" << "auto";
  confOptList.append(confOption(SYSTEMD, "ShowStatus", LIST, "yes", ShowStatusList));
  QStringList CrashChVTList = QStringList() << "-1" << "1" << "2" << "3" << "4" << "5" << "6" << "7" << "8";
  confOptList.append(confOption(SYSTEMD, "CrashChVT", LIST, "-1", CrashChVTList));
  QStringList CPUAffinityList;
  for (int i = 1; i <= QThread::idealThreadCount(); ++i)
    CPUAffinityList << QString::number(i);
  confOptList.append(confOption(SYSTEMD, "CPUAffinity", MULTILIST, CPUAffinityList));
  confOptList.append(confOption(SYSTEMD, "JoinControllers", STRING, "cpu,cpuacct net_cls,net_prio"));
  if (systemdVersion < 208)
    confOptList.append(confOption(SYSTEMD, "DefaultControllers", STRING, "cpu"));
  confOptList.append(confOption(SYSTEMD, "RuntimeWatchdogSec", TIME, 0, confOption::s, confOption::s, confOption::ms, confOption::w));
  confOptList.append(confOption(SYSTEMD, "ShutdownWatchdogSec", TIME, 10, confOption::min, confOption::s, confOption::ms, confOption::w));
  QStringList CapabilityBoundingSetList = confOption::capabilities;
  confOptList.append(confOption(SYSTEMD, "CapabilityBoundingSet", MULTILIST, CapabilityBoundingSetList));
  if (systemdVersion >= 209)
  {
    QStringList SystemCallArchitecturesList = QStringList() << "native" << "x86" << "x86-64" << "x32" << "arm";
    confOptList.append(confOption(SYSTEMD, "SystemCallArchitectures", MULTILIST, SystemCallArchitecturesList));
  }
  confOptList.append(confOption(SYSTEMD, "TimerSlackNSec", TIME, 0, confOption::ns, confOption::ns, confOption::ns, confOption::w));
  if (systemdVersion >= 212)
    confOptList.append(confOption(SYSTEMD, "DefaultTimerAccuracySec", TIME, 60, confOption::s, confOption::s, confOption::us, confOption::w));
  QStringList DefaultStandardOutputList = QStringList() << "inherit" << "null" << "tty" << "journal"
                                                        << "journal+console" << "syslog" << "syslog+console"
                                                        << "kmsg" << "kmsg+console";
  confOptList.append(confOption(SYSTEMD, "DefaultStandardOutput", LIST, "journal", DefaultStandardOutputList));
  confOptList.append(confOption(SYSTEMD, "DefaultStandardError", LIST, "inherit", DefaultStandardOutputList));
  if (systemdVersion >= 209)
  {
    confOptList.append(confOption(SYSTEMD, "DefaultTimeoutStartSec", TIME, 90, confOption::s, confOption::s, confOption::us, confOption::w));
    confOptList.append(confOption(SYSTEMD, "DefaultTimeoutStopSec", TIME, 90, confOption::s, confOption::s, confOption::us, confOption::w));
    confOptList.append(confOption(SYSTEMD, "DefaultRestartSec", TIME, 100, confOption::ms, confOption::s, confOption::us, confOption::w));
    confOptList.append(confOption(SYSTEMD, "DefaultStartLimitInterval", TIME, 10, confOption::s, confOption::s, confOption::us, confOption::w));
    confOptList.append(confOption(SYSTEMD, "DefaultStartLimitBurst", INTEGER, 5, 0, 99999));
  }
  if (systemdVersion >= 205)
    confOptList.append(confOption(SYSTEMD, "DefaultEnvironment", STRING, ""));
  if (systemdVersion >= 211)
  {
    confOptList.append(confOption(SYSTEMD, "DefaultCPUAccounting", BOOL, false));
    confOptList.append(confOption(SYSTEMD, "DefaultBlockIOAccounting", BOOL, false));
    confOptList.append(confOption(SYSTEMD, "DefaultMemoryAccounting", BOOL, false));
  }
  confOptList.append(confOption(SYSTEMD, "DefaultLimitCPU", RESLIMIT));  
  confOptList.append(confOption(SYSTEMD, "DefaultLimitFSIZE", RESLIMIT));
  confOptList.append(confOption(SYSTEMD, "DefaultLimitDATA", RESLIMIT));
  confOptList.append(confOption(SYSTEMD, "DefaultLimitSTACK", RESLIMIT));
  confOptList.append(confOption(SYSTEMD, "DefaultLimitCORE", RESLIMIT));
  confOptList.append(confOption(SYSTEMD, "DefaultLimitRSS", RESLIMIT));
  confOptList.append(confOption(SYSTEMD, "DefaultLimitNOFILE", RESLIMIT));
  confOptList.append(confOption(SYSTEMD, "DefaultLimitAS", RESLIMIT));
  confOptList.append(confOption(SYSTEMD, "DefaultLimitNPROC", RESLIMIT));
  confOptList.append(confOption(SYSTEMD, "DefaultLimitMEMLOCK", RESLIMIT));
  confOptList.append(confOption(SYSTEMD, "DefaultLimitLOCKS", RESLIMIT));
  confOptList.append(confOption(SYSTEMD, "DefaultLimitSIGPENDING", RESLIMIT));
  confOptList.append(confOption(SYSTEMD, "DefaultLimitMSGQUEUE", RESLIMIT));
  confOptList.append(confOption(SYSTEMD, "DefaultLimitNICE", RESLIMIT));
  confOptList.append(confOption(SYSTEMD, "DefaultLimitRTPRIO", RESLIMIT));
  confOptList.append(confOption(SYSTEMD, "DefaultLimitRTTIME", RESLIMIT));
  
  // journald.conf
  QStringList StorageList = QStringList() << "volatile" << "persistent" << "auto" << "none";
  confOptList.append(confOption(JOURNALD, "Storage", LIST, "auto", StorageList));
  confOptList.append(confOption(JOURNALD, "Compress", BOOL, true));
  confOptList.append(confOption(JOURNALD, "Seal", BOOL, true));
  QStringList SplitModeList = QStringList() << "login" << "uid" << "none";
  if (systemdVersion >= 215)
    confOptList.append(confOption(JOURNALD, "SplitMode", LIST, "uid", SplitModeList));
  else
    confOptList.append(confOption(JOURNALD, "SplitMode", LIST, "login", SplitModeList));
  confOptList.append(confOption(JOURNALD, "SyncIntervalSec", TIME, 5, confOption::min, confOption::s, confOption::us, confOption::w));
  confOptList.append(confOption(JOURNALD, "RateLimitInterval", TIME, 10, confOption::s, confOption::s, confOption::us, confOption::h));
  confOptList.append(confOption(JOURNALD, "RateLimitBurst", INTEGER, 200, 0, 99999));
  confOptList.append(confOption(JOURNALD, "SystemMaxUse", SIZE, QVariant(0.1 * partPersSizeMB).toULongLong(), partPersSizeMB));
  confOptList.append(confOption(JOURNALD, "SystemKeepFree", SIZE, QVariant(0.15 * partPersSizeMB).toULongLong(), partPersSizeMB));
  confOptList.append(confOption(JOURNALD, "SystemMaxFileSize", SIZE, QVariant(0.125 * 0.1 * partPersSizeMB).toULongLong(), partPersSizeMB));
  confOptList.append(confOption(JOURNALD, "RuntimeMaxUse", SIZE, QVariant(0.1 * partVolaSizeMB).toULongLong(), partVolaSizeMB));
  confOptList.append(confOption(JOURNALD, "RuntimeKeepFree", SIZE, QVariant(0.15 * partVolaSizeMB).toULongLong(), partVolaSizeMB));
  confOptList.append(confOption(JOURNALD, "RuntimeMaxFileSize", SIZE, QVariant(0.125 * 0.1 * partVolaSizeMB).toULongLong(), partVolaSizeMB));
  confOptList.append(confOption(JOURNALD, "MaxRetentionSec", TIME, 0, confOption::d, confOption::s, confOption::s, confOption::year));
  confOptList.append(confOption(JOURNALD, "MaxFileSec", TIME, 30, confOption::d, confOption::s, confOption::s, confOption::year));
  confOptList.append(confOption(JOURNALD, "ForwardToSyslog", BOOL, true));
  confOptList.append(confOption(JOURNALD, "ForwardToKMsg", BOOL, false));
  confOptList.append(confOption(JOURNALD, "ForwardToConsole", BOOL, false));
  if (systemdVersion >= 212)
    confOptList.append(confOption(JOURNALD, "ForwardToWall", BOOL, true));
  confOptList.append(confOption(JOURNALD, "TTYPath", STRING, "/dev/console"));
  QStringList SyslogList = QStringList() << "emerg" << "alert" << "crit" << "err" 
                                         << "warning" << "notice" << "info" << "debug";
  confOptList.append(confOption(JOURNALD, "MaxLevelStore", LIST, "debug", SyslogList));
  confOptList.append(confOption(JOURNALD, "MaxLevelSyslog", LIST, "debug", SyslogList));
  confOptList.append(confOption(JOURNALD, "MaxLevelKMsg", LIST, "notice", SyslogList));
  confOptList.append(confOption(JOURNALD, "MaxLevelConsole", LIST, "info", SyslogList));
  if (systemdVersion >= 212)
    confOptList.append(confOption(JOURNALD, "MaxLevelWall", LIST, "emerg", SyslogList));
  
  // logind.conf
  confOptList.append(confOption(LOGIND, "NAutoVTs", INTEGER, 6, 0, 12));
  confOptList.append(confOption(LOGIND, "ReserveVT", INTEGER, 6, 0, 12));
  confOptList.append(confOption(LOGIND, "KillUserProcesses", BOOL, false));
  confOptList.append(confOption(LOGIND, "KillOnlyUsers", STRING, ""));
  confOptList.append(confOption(LOGIND, "KillExcludeUsers", STRING, "root"));
  confOptList.append(confOption(LOGIND, "InhibitDelayMaxSec", TIME, 5, confOption::s, confOption::s, confOption::us, confOption::d));
  QStringList HandlePowerEvents = QStringList() << "ignore" << "poweroff" << "reboot" << "halt" << "kexec" << "suspend" << "hibernate" << "hybrid-sleep" << "lock";
  confOptList.append(confOption(LOGIND, "HandlePowerKey", LIST, "poweroff", HandlePowerEvents));
  confOptList.append(confOption(LOGIND, "HandleSuspendKey", LIST, "suspend", HandlePowerEvents));
  confOptList.append(confOption(LOGIND, "HandleHibernateKey", LIST, "hibernate", HandlePowerEvents));
  confOptList.append(confOption(LOGIND, "HandleLidSwitch", LIST, "suspend", HandlePowerEvents));
  if (systemdVersion >= 217)
    confOptList.append(confOption(LOGIND, "HandleLidSwitchDocked", LIST, "ignore", HandlePowerEvents));
  confOptList.append(confOption(LOGIND, "PowerKeyIgnoreInhibited", BOOL, false));
  confOptList.append(confOption(LOGIND, "SuspendKeyIgnoreInhibited", BOOL, false));
  confOptList.append(confOption(LOGIND, "HibernateKeyIgnoreInhibited", BOOL, false));
  confOptList.append(confOption(LOGIND, "LidSwitchIgnoreInhibited", BOOL, true));
  confOptList.append(confOption(LOGIND, "IdleAction", LIST, "ignore", HandlePowerEvents));
  confOptList.append(confOption(LOGIND, "IdleActionSec", TIME, 0, confOption::s, confOption::s, confOption::us, confOption::d));
  if (systemdVersion < 207)
  {
    confOptList.append(confOption(LOGIND, "Controllers", STRING, ""));
    confOptList.append(confOption(LOGIND, "ResetControllers", STRING, "cpu"));
  }
  if (systemdVersion >= 212)
  {
    confOptList.append(confOption(LOGIND, "RemoveIPC", BOOL, true));
  }
  
  if (systemdVersion >= 215)
  {  
    // coredump.conf
    QStringList CoreStorage = QStringList() << "none" << "external" << "journal" << "both";
    confOptList.append(confOption(COREDUMP, "Storage", LIST, "external", CoreStorage));
    confOptList.append(confOption(COREDUMP, "Compress", BOOL, true));
    confOptList.append(confOption(COREDUMP, "ProcessSizeMax", SIZE, 2*1024, partPersSizeMB));
    confOptList.append(confOption(COREDUMP, "ExternalSizeMax", SIZE, 2*1024, partPersSizeMB));
    confOptList.append(confOption(COREDUMP, "JournalSizeMax", SIZE, 767, partPersSizeMB));
    confOptList.append(confOption(COREDUMP, "MaxUse", SIZE, QVariant(0.1 * partPersSizeMB).toULongLong(), partPersSizeMB));
    confOptList.append(confOption(COREDUMP, "KeepFree", SIZE, QVariant(0.15 * partPersSizeMB).toULongLong(), partPersSizeMB));
  }
}

void kcmsystemd::initializeInterface()
{
  // This function must only be run once
  timesLoad = timesLoad + 1;

  // Populate comboboxes
  QStringList allowUnitTypes = QStringList() << "All unit types" << "Targets" << "Services"
                                             << "Devices" << "Mounts" << "Automounts" << "Swaps" 
                                             << "Sockets" << "Paths" << "Timers" << "Snapshots" 
                                             << "Slices" << "Scopes";
  ui.cmbUnitTypes->addItems(allowUnitTypes);
  ui.cmbConfFile->addItems(listConfFiles);
}

void kcmsystemd::readConfFile(QString filename)
{ 
  QFile file (etcDir + "/" + filename);
  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&file);
    QString line = in.readLine();

    while(!line.isNull())
    {
      // qDebug() << "line is: " << line;

      for (int i = 0; i < confOptList.size(); ++i)
      {
        
        if (filename == "system.conf" && 
            confOptList.at(i) == confOption(QString(line.section("=",0,0).trimmed()) + "_0"))
        {
          confOptList[i].setValueFromFile(line);
          break;
        }
        else if (filename == "journald.conf" &&
                  confOptList.at(i) == confOption(QString(line.section("=",0,0).trimmed()) + "_1"))
        {
          confOptList[i].setValueFromFile(line);
          break;
        }            
        else if (filename == "logind.conf" && 
                  confOptList.at(i) == confOption(QString(line.section("=",0,0).trimmed()) + "_2"))
        {
          confOptList[i].setValueFromFile(line);
          break;
        }
        
        else if (filename == "coredump.conf" && 
                  confOptList.at(i) == confOption(QString(line.section("=",0,0).trimmed()) + "_3"))
        {
          confOptList[i].setValueFromFile(line);
          break;
        }

      }

      line = in.readLine();
    } // read lines until empty
        
  } // if file open
  else 
    KMessageBox::error(this, i18n("Failed to read %1/%2. Using default values.", etcDir, filename));
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
  proxyModelUnitId = new QSortFilterProxyModel (this);
  proxyModelAct->setSourceModel(unitsModel);
  proxyModelUnitId->setSourceModel(proxyModelAct);

  // Install eventfilter to capture mouse move events
  ui.tblServices->viewport()->installEventFilter(this);
  
  // Set header row
  unitsModel->setHorizontalHeaderItem(0, new QStandardItem(QString("Load state")));
  unitsModel->setHorizontalHeaderItem(1, new QStandardItem(QString("Active state")));
  unitsModel->setHorizontalHeaderItem(2, new QStandardItem(QString("Unit state")));
  unitsModel->setHorizontalHeaderItem(3, new QStandardItem(QString("Unit")));

  // Set model for QTableView (should be called after headers are set)
  ui.tblServices->setModel(proxyModelUnitId);
  
  // Connect the currentRowChanged signal
  QItemSelectionModel *selectionModel = ui.tblServices->selectionModel();
  connect(selectionModel, SIGNAL(currentRowChanged(const QModelIndex &, const QModelIndex &)), this, SLOT(slotTblRowChanged(const QModelIndex &, const QModelIndex &)));
  
  // Setup initial filters and sorting
  ui.tblServices->sortByColumn(3, Qt::AscendingOrder);
  proxyModelUnitId->setSortCaseSensitivity(Qt::CaseInsensitive);
  slotChkShowUnits();
  
  // Add all the units
  slotRefreshUnitsList();
}

void kcmsystemd::defaults()
{
  if (KMessageBox::warningYesNo(this, i18n("Load deafult settings for all tabs?")) == KMessageBox::Yes)
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
  systemConfFileContents.append("# " + etcDir + "/system.conf\n# Generated by kcmsystemd control module v." + KCM_SYSTEMD_VERSION + ".\n");
  systemConfFileContents.append("[Manager]\n");
  foreach (confOption i, confOptList)
  {
    if (i.file == SYSTEMD)
      systemConfFileContents.append(i.getLineForFile());
  }

  QString journaldConfFileContents;
  journaldConfFileContents.append("# " + etcDir + "/journald.conf\n# Generated by kcmsystemd control module v." + KCM_SYSTEMD_VERSION + ".\n");
  journaldConfFileContents.append("[Journal]\n");
  foreach (confOption i, confOptList)
  {
    if (i.file == JOURNALD)
      journaldConfFileContents.append(i.getLineForFile());
  }  
  
  QString logindConfFileContents;
  logindConfFileContents.append("# " + etcDir + "/logind.conf\n# Generated by kcmsystemd control module v." + KCM_SYSTEMD_VERSION + ".\n");
  logindConfFileContents.append("[Login]\n");
  foreach (confOption i, confOptList)
  {
    if (i.file == LOGIND)
      logindConfFileContents.append(i.getLineForFile());
  }
  
  QString coredumpConfFileContents;
  coredumpConfFileContents.append("# " + etcDir + "/coredump.conf\n# Generated by kcmsystemd control module v." + KCM_SYSTEMD_VERSION + ".\n");
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

void kcmsystemd::slotTblRowChanged(const QModelIndex &current, __attribute__((unused)) const QModelIndex &previous)
{
  // Find the selected unit
  selectedUnit = ui.tblServices->model()->index(current.row(),3).data().toString();

  // Find the selected row
  QModelIndex inProxyModelAct = proxyModelUnitId->mapToSource(const_cast<QModelIndex &> (current));
  selectedRow = proxyModelAct->mapToSource(inProxyModelAct).row();
  
  updateUnitProps(selectedUnit);
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
  
  // Update unit properties for the selected unit
  if (!selectedUnit.isEmpty())
    updateUnitProps(selectedUnit);
  slotChkShowUnits();
}

void kcmsystemd::updateUnitProps(QString unit)
{
  // Function to update the selected units properties
  
  // Create a DBus interface
  QDBusConnection systembus = QDBusConnection::systemBus();
  QDBusInterface *iface = new QDBusInterface (
	  "org.freedesktop.systemd1",
	  unitpaths[unit].toString(),
	  "org.freedesktop.systemd1.Unit",
	  systembus,
	  this);
  
  // Use the DBus interface to get unit properties and update labels
  if (iface->isValid())
  {
    ui.lblDesc->setText(iface->property("Description").toString());
    ui.lblFragmentPath->setText(iface->property("FragmentPath").toString());
    if (iface->property("ActiveEnterTimestamp").toULongLong() == 0)
      ui.lblTimeActivated->setText("n/a");
    else
    {
      QDateTime timeActivated;
      timeActivated.setMSecsSinceEpoch(iface->property("ActiveEnterTimestamp").toULongLong()/1000);
      ui.lblTimeActivated->setText(timeActivated.toString());
    }
    if (iface->property("InactiveEnterTimestamp").toULongLong() == 0)
      ui.lblTimeDeactivated->setText("n/a");
    else
    {
      QDateTime timeDeactivated;
      timeDeactivated.setMSecsSinceEpoch(iface->property("InactiveEnterTimestamp").toULongLong()/1000);
      ui.lblTimeDeactivated->setText(timeDeactivated.toString());
    }
    ui.lblUnitFileState->setText(iface->property("UnitFileState").toString());	  
  } else {
    iface = new QDBusInterface (
	  "org.freedesktop.systemd1",
	  "/org/freedesktop/systemd1",
	  "org.freedesktop.systemd1.Manager",
	  systembus,
	  this);
    QList<QVariant> args;
    args << selectedUnit;

    ui.lblDesc->setText("");
    unitfile a;
    a.name = selectedUnit;
    if (!a.name.isEmpty())
    {
      ui.lblFragmentPath->setText(unitfileslist.at(unitfileslist.indexOf(a)).name);
      ui.lblUnitFileState->setText(iface->callWithArgumentList(QDBus::AutoDetect, "GetUnitFileState", args).arguments().at(0).toString());
    } else {
      ui.lblFragmentPath->setText("");
      ui.lblUnitFileState->setText("");
    }
    ui.lblTimeActivated->setText("");
    ui.lblTimeDeactivated->setText("");
  }
  delete iface;
}

void kcmsystemd::updateUnitCount()
{
  ui.lblStatus->setText("Total: " + QString::number(unitsModel->rowCount()) + " units, " + 
			QString::number(noActUnits) + " active, " + 
			QString::number(proxyModelUnitId->rowCount()) + " displayed");
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
  QAction *start = menu.addAction("&Start unit");
  QAction *stop = menu.addAction("S&top unit");
  QAction *restart = menu.addAction("&Restart unit");
  QAction *reload = menu.addAction("Re&load unit");
  menu.addSeparator();
  QAction *edit = menu.addAction("&Edit unit file");
  QAction *isolate = menu.addAction("&Isolate unit");
  menu.addSeparator();
  QAction *enable = menu.addAction("En&able unit");
  QAction *disable = menu.addAction("&Disable unit");
  menu.addSeparator();
  QAction *mask = menu.addAction("&Mask unit");
  QAction *unmask = menu.addAction("&Unmask unit");
  menu.addSeparator();
  QAction *reloaddaemon = menu.addAction("Rel&oad all unit files");
  QAction *reexecdaemon = menu.addAction("Ree&xecute systemd");
  
  // Get UnitFileState (have to use Manager interface for this)
  QList<QVariant> args;
  args << unit;
  QString UnitFileState = dbusIfaceManager->callWithArgumentList(QDBus::AutoDetect, "GetUnitFileState", args).arguments().at(0).toString();
  
  // Check if unit can be isolated
  isolate->setEnabled(dbusIfaceUnit->property("CanIsolate").toBool());
  QString LoadState = dbusIfaceUnit->property("LoadState").toString();
  QString ActiveState = dbusIfaceUnit->property("ActiveState").toString();
  bool CanStart = dbusIfaceUnit->property("CanStart").toBool();

  if (CanStart &&
      ActiveState != "active")
    start->setEnabled(true);
  else
    start->setEnabled(false);

  if (dbusIfaceUnit->property("CanStop").toBool() &&
      ActiveState != "inactive" &&
      ActiveState != "failed")
    stop->setEnabled(true);
  else
    stop->setEnabled(false);

  if (CanStart &&
      ActiveState != "inactive" &&
      ActiveState != "failed")
    restart->setEnabled(true);
  else
    restart->setEnabled(false);

  if (dbusIfaceUnit->property("CanReload").toBool() &&
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
  } else if (LoadState == "loaded" || LoadState == "error") {
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
    kwrite.startDetached(kdePrefix + "/lib/libexec/kdesu", QStringList() << "-t" << "--" << "kwrite" << frpath);
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

bool kcmsystemd::eventFilter(__attribute__((unused)) QObject *watched, QEvent* event)
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
      for(QVariantMap::const_iterator iter = unitpaths.begin(); iter != unitpaths.end(); ++iter)
      {
	if (iter.key() == ui.tblServices->model()->index(ui.tblServices->indexAt(me->pos()).row(),3).data().toString())
	{
	  // If unit-id corresponds to column 3:
	  QString toolTipText;

	  // Create a DBus interface
	  QDBusConnection systembus = QDBusConnection::systemBus();
	  QDBusInterface *iface = new QDBusInterface (
		  "org.freedesktop.systemd1",
		  iter.value().toString(),
		  "org.freedesktop.systemd1.Unit",
		  systembus,
		  this);
	  
	  // Use the DBus interface to get unit properties
	  if (iface->isValid())
	  {
	    QStringList req = iface->property("Requires").toStringList();
	    QStringList wan = iface->property("Wants").toStringList();
	    QStringList con = iface->property("Conflicts").toStringList();
	    QStringList reqby = iface->property("RequiredBy").toStringList();
	    QStringList wanby = iface->property("WantedBy").toStringList();
	    QStringList conby = iface->property("ConflictedBy").toStringList();
	    QStringList aft = iface->property("After").toStringList();	    
	    QStringList bef = iface->property("Before").toStringList();	    

      toolTipText.append("<FONT COLOR=white>");
	    toolTipText.append("<span style=\"font-weight:bold;\">" + iface->property("Id").toString() + "</span><hr>");
	    
	    toolTipText.append("<span style=\"font-weight:bold;\">Requires:</span> ");
	    foreach (QString i, req)
	      toolTipText.append(i + " ");
	    toolTipText.append("<br><span style=\"font-weight:bold;\">Wants:</span> ");
	    foreach (QString i, wan)
	      toolTipText.append(i + " ");
	    toolTipText.append("<br><span style=\"font-weight:bold;\">Conflicts:</span> ");
	    foreach (QString i, con)
	      toolTipText.append(i + " ");
	    toolTipText.append("<hr><span style=\"font-weight:bold;\">Required by:</span> ");
	    foreach (QString i, reqby)
	      toolTipText.append(i + " ");
	    toolTipText.append("<br><span style=\"font-weight:bold;\">Wanted by:</span> ");
	    foreach (QString i, wanby)
	      toolTipText.append(i + " ");
	    toolTipText.append("<br><span style=\"font-weight:bold;\">Conflicted by:</span> ");
	    foreach (QString i, conby)
	      toolTipText.append(i + " ");
	    toolTipText.append("<hr><span style=\"font-weight:bold;\">After:</span> ");
	    foreach (QString i, aft)
	      toolTipText.append(i + " ");
	    toolTipText.append("<br><span style=\"font-weight:bold;\">Before:</span> ");
	    foreach (QString i, bef)
	      toolTipText.append(i + " ");
	    toolTipText.append("</FONT");
	  }
	  unitsModel->itemFromIndex(inUnitsModel)->setToolTip(toolTipText);
	  delete iface;
	}
      }
    lastRowChecked = unitsModel->itemFromIndex(inUnitsModel)->row();
    return true;
    }
  }
  return false;
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
  ui.lblConfFile->setText("File to be written: " + etcDir + "/" + listConfFiles.at(index));

  proxyModelConf->setFilterRegExp(ui.cmbConfFile->itemText(index));
  proxyModelConf->setFilterKeyColumn(2);
}

#include "kcmsystemd.moc"
