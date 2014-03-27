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

#include "kcmsystemd.h"
#include "reslimits.h"
#include "environ.h"
#include "advanced.h"

#include <config.h>

#include <QMouseEvent>
#include <QMenu>
#include <QThread>

#include <KAboutData>
#include <KPluginFactory>
#include <KMessageBox>
#include <KAuth/ActionWatcher>
using namespace KAuth;

#include <boost/filesystem.hpp>

K_PLUGIN_FACTORY(kcmsystemdFactory, registerPlugin<kcmsystemd>();)
K_EXPORT_PLUGIN(kcmsystemdFactory("kcmsystemd"))

kcmsystemd::kcmsystemd(QWidget *parent, const QVariantList &list) : KCModule(kcmsystemdFactory::componentData(), parent, list)
{
  KAboutData *about = new KAboutData("kcmsystemd", "kcmsystemd", ki18nc("@title", "KDE Systemd Control Module"), KCM_SYSTEMD_VERSION, ki18nc("@title", "A KDE Control Module for configuring Systemd."), KAboutData::License_GPL_V3, ki18nc("@info:credit", "Copyright (C) 2013 Ragnar Thomsen"), KLocalizedString(), "https://github.com/rthomsen/kcmsystemd");
  about->addAuthor(ki18nc("@info:credit", "Ragnar Thomsen"), ki18nc("@info:credit", "Main Developer"), "rthomsen6@gmail.com");
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
  if (iface->isValid()) {
    systemdVersion = iface->property("Version").toString().remove("systemd ").toInt();
    // This option was added in systemd 205
    if (systemdVersion < 205)
      ui.btnEnviron->setEnabled(false);
    // These options were removed in systemd 207
    if (systemdVersion >= 207)
      ui.grpControlGroups->setEnabled(false);
    // These options were added in systemd 212
    if (systemdVersion < 212)
    {
      ui.chkForwardToWall->setEnabled(false);
      ui.cmbMaxLevelWall->setEnabled(false);
    }

    qDebug() << "Systemd" << systemdVersion << "detected.";
  } else {
    qDebug() << "Unable to contact systemd daemon!";
    ui.stackedWidget->setCurrentIndex(1);
  }
  delete iface;

  // Use kde4-config to get kde prefix
  kdeConfig = new QProcess(this);
  connect(kdeConfig, SIGNAL(readyReadStandardOutput()), this, SLOT(slotKdeConfig()));
  kdeConfig->start("kde4-config", QStringList() << "--prefix");
  
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
  QDBusConnection::systemBus().connect("org.freedesktop.systemd1","/org/freedesktop/systemd1","org.freedesktop.systemd1.Manager","UnitNew",this,SLOT(slotUnitLoaded(QString, QDBusObjectPath)));
  QDBusConnection::systemBus().connect("org.freedesktop.systemd1","/org/freedesktop/systemd1","org.freedesktop.systemd1.Manager","UnitRemoved",this,SLOT(slotUnitUnloaded(QString, QDBusObjectPath)));
  QDBusConnection::systemBus().connect("org.freedesktop.systemd1","/org/freedesktop/systemd1","org.freedesktop.systemd1.Manager","UnitFilesChanged",this,SLOT(slotUnitFilesChanged()));
  QDBusConnection::systemBus().connect("org.freedesktop.systemd1","","org.freedesktop.DBus.Properties","PropertiesChanged",this,SLOT(slotPropertiesChanged(QString, QVariantMap, QStringList)));
  
  // Setup the unitslist
  setupUnitslist();
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
  connect(ui.btnStartUnit, SIGNAL(clicked()), this, SLOT(slotBtnStartUnit()));
  connect(ui.btnStopUnit, SIGNAL(clicked()), this, SLOT(slotBtnStopUnit()));
  connect(ui.btnRestartUnit, SIGNAL(clicked()), this, SLOT(slotBtnRestartUnit()));
  connect(ui.btnReloadUnit, SIGNAL(clicked()), this, SLOT(slotBtnReloadUnit()));
  connect(ui.btnRefreshUnits, SIGNAL(clicked()), this, SLOT(slotRefreshUnitsList()));
  connect(ui.chkInactiveUnits, SIGNAL(stateChanged(int)), this, SLOT(slotChkShowUnits()));
  connect(ui.chkShowUnloadedUnits, SIGNAL(stateChanged(int)), this, SLOT(slotChkShowUnits()));
  connect(ui.cmbUnitTypes, SIGNAL(currentIndexChanged(int)), this, SLOT(slotCmbUnitTypes()));
  connect(ui.tblServices, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(slotDisplayMenu(QPoint)));
  connect(ui.leSearchUnit, SIGNAL(textChanged(QString)), this, SLOT(slotLeSearchUnitChanged(QString)));
  
  // Connect signals for system.conf  
  connect(ui.btnResourceLimits, SIGNAL(clicked()), this, SLOT(slotOpenResourceLimits()));
  connect(ui.btnEnviron, SIGNAL(clicked()), this, SLOT(slotOpenEnviron()));
  connect(ui.btnAdvanced, SIGNAL(clicked()), this, SLOT(slotOpenAdvanced()));
  
  // Connect signals for journald.conf:
  connect(ui.cmbStorage, SIGNAL(currentIndexChanged(int)), this, SLOT(slotStorageChanged()));
  connect(ui.spnMaxUse, SIGNAL(valueChanged(int)), this, SLOT(slotSpnMaxUseChanged()));
  connect(ui.spnKeepFree, SIGNAL(valueChanged(int)), this, SLOT(slotSpnKeepFreeChanged()));
  connect(ui.spnMaxFileSize, SIGNAL(valueChanged(int)), this, SLOT(slotSpnMaxFileSizeChanged()));
  connect(ui.chkForwardToSyslog, SIGNAL(stateChanged(int)), this, SLOT(slotFwdToSyslogChanged()));
  connect(ui.chkForwardToKMsg, SIGNAL(stateChanged(int)), this, SLOT(slotFwdToKmsgChanged()));
  connect(ui.chkForwardToConsole, SIGNAL(stateChanged(int)), this, SLOT(slotFwdToConsoleChanged()));
  connect(ui.chkForwardToWall, SIGNAL(stateChanged(int)), this, SLOT(slotFwdToWallChanged()));
  
  QList<QCheckBox *> listChk = this->findChildren<QCheckBox *>(QRegExp("MaxUse|KeepFree|MaxFileSize"));
  foreach(QCheckBox *chk, listChk)
    connect(chk, SIGNAL(stateChanged(int)), this, SLOT(slotStorageChkBoxes(int)));
  
  // Connect signals for logind.conf
  connect(ui.chkKillUserProcesses, SIGNAL(stateChanged(int)), this, SLOT(slotKillUserProcessesChanged()));
}

void kcmsystemd::load()
{
  // Only initialize the interface once
  if (timesLoad == 0)
    initializeInterface();
  
  // Read the three configuration files
  readSystemConf();
  readJournaldConf();
  readLogindConf();
  
  // Apply the read settings to the interface
  applyToInterface();
  
  // Connect signals to slots different widget types
  QList<QCheckBox *> listChk = this->findChildren<QCheckBox *>();
  foreach(QCheckBox *chk, listChk)
  {
    if (chk->objectName() != "chkMaxUse" && 
        chk->objectName() != "chkKeepFree" &&
        chk->objectName() != "chkMaxFileSize" &&
        chk->objectName() != "chkInactiveUnits" &&
        chk->objectName() != "chkShowUnloadedUnits")
      connect(chk, SIGNAL(stateChanged(int)), this, SLOT(slotUpdateConfOption()));
  }
    
  QList<QSpinBox *> listSpn = this->findChildren<QSpinBox *>();
  foreach(QSpinBox *spn, listSpn)
  {
    // Three spinboxes must not be connected to this slot
    if (spn->objectName() != "spnMaxUse" && spn->objectName() != "spnKeepFree" && spn->objectName() != "spnMaxFileSize")
    {
      if (confOptList.at(confOptList.indexOf(confOption(spn->objectName().remove("spn")))).type == INTEGER)
        spn->setMaximum(confOptList.at(confOptList.indexOf(confOption(spn->objectName().remove("spn")))).maxVal);
      connect(spn, SIGNAL(valueChanged(int)), this, SLOT(slotUpdateConfOption()));
      
    }
  }
  
  QList<QComboBox *> listCmb = this->findChildren<QComboBox *>();
  foreach(QComboBox *cmb, listCmb)
  {
    if (cmb->objectName() != "cmbUnitTypes")
      connect(cmb, SIGNAL(currentIndexChanged(int)), this, SLOT(slotUpdateConfOption()));
  }
  
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
  QStringList ShowStatusList = QStringList() << "auto" << "true" << "false";
  confOptList.append(confOption(SYSTEMD, "ShowStatus", LIST, "auto", ShowStatusList));
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
  confOptList.append(confOption(JOURNALD, "SplitMode", LIST, "login", SplitModeList));
  confOptList.append(confOption(JOURNALD, "SyncIntervalSec", TIME, 5, confOption::min, confOption::s, confOption::us, confOption::w));
  confOptList.append(confOption(JOURNALD, "RateLimitInterval", TIME, 10, confOption::s, confOption::s, confOption::us, confOption::h));
  confOptList.append(confOption(JOURNALD, "RateLimitBurst", INTEGER, 200, 0, 99999));
  confOptList.append(confOption(JOURNALD, "SystemMaxUse", SIZE, QVariant(0.1 * partPersSizeMB).toULongLong()));
  confOptList.append(confOption(JOURNALD, "SystemKeepFree", SIZE, QVariant(0.15 * partPersSizeMB).toULongLong()));
  confOptList.append(confOption(JOURNALD, "SystemMaxFileSize", SIZE, QVariant(0.125 * 0.1 * partPersSizeMB).toULongLong()));
  confOptList.append(confOption(JOURNALD, "RuntimeMaxUse", SIZE, QVariant(0.1 * partVolaSizeMB).toULongLong()));
  confOptList.append(confOption(JOURNALD, "RuntimeKeepFree", SIZE, QVariant(0.15 * partVolaSizeMB).toULongLong()));
  confOptList.append(confOption(JOURNALD, "RuntimeMaxFileSize", SIZE, QVariant(0.125 * 0.1 * partVolaSizeMB).toULongLong()));
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
}

void kcmsystemd::initializeInterface()
{
  // This function must only be run once
  timesLoad = timesLoad + 1;

  // Populate comboboxes:
  QList<QComboBox *> listCmb = this->findChildren<QComboBox *>();
  foreach (QComboBox *cmb, listCmb)
  {
    QString name = cmb->objectName().remove("cmb");
    int index = confOptList.indexOf(confOption(name));
    if (index > -1)
      cmb->addItems(confOptList.at(index).possibleVals);
  }
  // cmbCrashChVT needs special treatment
  ui.cmbCrashChVT->setItemData(0, "Off", Qt::DisplayRole);
  
  QStringList allowUnitTypes = QStringList() << "All unit types" << "Targets" << "Services"
                                             << "Devices" << "Mounts" << "Automounts" << "Swaps" 
                                             << "Sockets" << "Paths" << "Timers" << "Snapshots" 
                                             << "Slices" << "Scopes";
  for (int i = 0; i < allowUnitTypes.size(); i++)
    ui.cmbUnitTypes->addItem(allowUnitTypes[i]);
}

void kcmsystemd::readSystemConf()
{ 
  QFile file (etcDir + "/system.conf");
  
  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&file);
    QString line = in.readLine();

    while(!line.isNull())
    {
      for (int i = 0; i < confOptList.size(); ++i)
      {
        if (confOptList.at(i) == confOption(line.section("=",0,0).trimmed()))
        {        
          confOptList[i].setValueFromFile(line);
          break;          
        }
      }

      line = in.readLine();
    } // read lines until empty
        
  } // if file open
  else 
    KMessageBox::error(this, i18n("Failed to read %1/system.conf. Using default values.", etcDir));
}

void kcmsystemd::readJournaldConf()
{
  QFile file (etcDir + "/journald.conf");
  
  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&file);
    QString line = in.readLine();

    while(!line.isNull())
    {
      for (int i = 0; i < confOptList.size(); ++i)
      {
        if (confOptList.at(i) == confOption(line.section("=",0,0).trimmed()))
        {        
          confOptList[i].setValueFromFile(line);
          break;          
        }
      }

      line = in.readLine();
    } // read lines until empty
        
  } // if file open
  else 
    KMessageBox::error(this, i18n("Failed to read %1/journald.conf. Using default values.", etcDir));
}

void kcmsystemd::readLogindConf()
{
  QFile file (etcDir + "/logind.conf");
  
  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&file);
    QString line = in.readLine();

    while(!line.isNull())
    { 
      for (int i = 0; i < confOptList.size(); ++i)
      {
        if (confOptList.at(i) == confOption(line.section("=",0,0).trimmed()))
        {        
          confOptList[i].setValueFromFile(line);
          break;          
        }
      }

      line = in.readLine();
    } // read lines until empty
        
  } // if file open
  else 
    KMessageBox::error(this, i18n("Failed to read %1/logind.conf. Using default values.", etcDir));
} 

void kcmsystemd::applyToInterface()
{
  // Loop through all confOptions and update interface
  foreach(confOption i, confOptList)
  {
    if (i.type == BOOL)
    {
      QCheckBox *chk = this->findChild<QCheckBox *>("chk" + i.name);
      if (chk)
        chk->setChecked(i.getValue().toBool());
    }
    else if (i.type == STRING)
    {
      QLineEdit *le = this->findChild<QLineEdit *>("le" + i.name);
      if (le)
        le->setText(i.getValue().toString());
    }
    else if (i.type == INTEGER)
    {
      QSpinBox *spn = this->findChild<QSpinBox *>("spn" + i.name);
      if (spn)
        spn->setValue(i.getValue().toInt());
    }
    else if (i.type == TIME)
    {
      QSpinBox *spn = this->findChild<QSpinBox *>("spn" + i.name);
      if (spn)
        spn->setValue(i.getValue().toInt());
    }
    else if (i.type == LIST)
    {
      QComboBox *cmb = this->findChild<QComboBox *>("cmb" + i.name);
      if (cmb)
        cmb->setCurrentIndex(i.possibleVals.indexOf(i.getValue().toString()));
    }
    else if (i.type == SIZE)
    {
      QString basename = i.name;
      basename.remove(QRegExp("System|Runtime"));
      QSpinBox *spn = this->findChild<QSpinBox *>("spn" + basename);
      if (spn)
      {
        if (isPersistent && i.name.contains("System"))
          spn->setValue(i.getValue().toULongLong());
        else if (!isPersistent && i.name.contains("Runtime"))
          spn->setValue(i.getValue().toULongLong());
      }
      QCheckBox *chk = this->findChild<QCheckBox *>("chk" + basename);
      if (chk)
      {
        if ((isPersistent && i.name.contains("System")) || (!isPersistent && i.name.contains("Runtime")))
        {
          if (i.getValue() == i.defVal)
            chk->setChecked(Qt::Checked);
          else
            chk->setChecked(Qt::Unchecked);
        }
      }      
      
    }
    
  }
  
  // Radiobutton
  QRadioButton *rad = this->findChild<QRadioButton *>("rad" + confOptList.at(confOptList.indexOf(confOption("SplitMode"))).getValue().toString());
  if (rad)
    rad->setChecked(true);
  
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
      confOptList[i].setValue(confOptList.at(i).defVal);
      confOptList[i].active = false;
      if (confOptList.at(i).type == RESLIMIT)
      {
        qDebug() << confOptList.at(i).name << ":" << confOptList.at(i).getValue();
        confOption::resLimitsMap[confOptList.at(i).name] = confOptList.at(i).getValue();
        
      }
    }
    applyToInterface();
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
  
  // Declare a QVariantMap with arguments for the helper
  QVariantMap helperArgs;
  if (QDir(etcDir).exists()) {
    helperArgs["etcDir"] = etcDir;
  } else {
    // Failed to find systemd config directory
    KMessageBox::error(this, i18n("Unable to find directory for configuration files."));
    return;
  }
  helperArgs["systemConfFileContents"] = systemConfFileContents;
  helperArgs["journaldConfFileContents"] = journaldConfFileContents;
  helperArgs["logindConfFileContents"] = logindConfFileContents;
    
  // Call the helper to write the configuration files
  Action *saveAction = authAction();      
  saveAction->setArguments(helperArgs);
  ActionReply reply = saveAction->execute();
  
  // Respond to reply of the helper
  if(reply.failed())
  { // Writing the configuration files failed
    if (reply.type() == ActionReply::KAuthError) // Authorization error
      KMessageBox::error(this, i18n("Unable to authenticate/execute the action: code %1", reply.errorCode()));
    else // Other error
      KMessageBox::error(this, i18n("Unable to write the (%1) file:\n%2", reply.data()["filename"].toString(), reply.data()["errorDescription"].toString()));
  } else // Writing succeeded
    KMessageBox::information(this, i18n("Configuration files succesfully written to: %1", helperArgs["etcDir"].toString()));
}

void kcmsystemd::slotUpdateConfOption()
{
  QString name = QObject::sender()->objectName().remove(QRegExp("^(chk|spn|cmb|le)"));
  confOptList[confOptList.indexOf(confOption(name))].active = true;
  
  if (QObject::sender()->objectName().contains(QRegExp("^(chk|spn|cmb|le)")))
  {    
    if (QObject::sender()->objectName().contains(QRegExp("^chk")))
    {
      // Checkboxes
      QCheckBox *chk = this->findChild<QCheckBox *>(QObject::sender()->objectName());
      if (chk)
      {
        // qDebug() << name << " changed to " << chk->isChecked();
        confOptList[confOptList.indexOf(confOption(name))].setValue(chk->isChecked());
      }
      
    } else if (QObject::sender()->objectName().contains(QRegExp("^spn")))
    {
      // Spinboxes
      QSpinBox *spn = this->findChild<QSpinBox *>(QObject::sender()->objectName());
      if (spn)
      {
        // qDebug() << name << " changed to " << spn->value();
        confOptList[confOptList.indexOf(confOption(name))].setValue(spn->value());
      }
    
    } else if (QObject::sender()->objectName().contains(QRegExp("^cmb")))
    {
      // Comboboxes
      QComboBox *cmb = this->findChild<QComboBox *>(QObject::sender()->objectName());
      if (cmb)
      {
        // qDebug() << name << " changed to " << cmb->currentText();
        if (cmb->objectName() == "cmbCrashChVT" && cmb->currentText() == "Off")
          confOptList[confOptList.indexOf(confOption(name))].setValue("-1");
        else
          confOptList[confOptList.indexOf(confOption(name))].setValue(cmb->currentText());
      }
    } else if (QObject::sender()->objectName().contains(QRegExp("^le")))
    {
      // Lineedits
      QLineEdit *le = this->findChild<QLineEdit *>(QObject::sender()->objectName());
      if (le)
      {
        // qDebug() << name << " changed to " << le->text();
        confOptList[confOptList.indexOf(confOption(name))].setValue(le->text());
      }
    }

  }
  emit changed(true);
  
}

void kcmsystemd::slotStorageChanged()
{
  if (ui.cmbStorage->currentText() == "none") {
    // no storage of logs
    ui.grpSizeRotation->setEnabled(0);
    ui.grpTimeRotation->setEnabled(0);
    ui.grpSplitMode->setEnabled(0);
  
  } else {
    // storage of logs
    ui.grpSizeRotation->setEnabled(1);
    ui.grpTimeRotation->setEnabled(1);
    ui.grpSplitMode->setEnabled(1);

    
    if (ui.cmbStorage->currentText() == "persistent" || (ui.cmbStorage->currentText() == "auto" && varLogDirExists)) {
      // using persistent storage:
      isPersistent = true;
      ui.spnMaxUse->setMaximum(partPersSizeMB);
      ui.spnMaxUse->setValue(confOptList.at(confOptList.indexOf(confOption("SystemMaxUse"))).getValue().toULongLong());
      ui.spnKeepFree->setMaximum(partPersSizeMB);
      ui.spnKeepFree->setValue(confOptList.at(confOptList.indexOf(confOption("SystemKeepFree"))).getValue().toULongLong());
      ui.spnMaxFileSize->setMaximum(partPersSizeMB);
      ui.spnMaxFileSize->setValue(confOptList.at(confOptList.indexOf(confOption("SystemMaxFileSize"))).getValue().toULongLong());
      
      if (confOptList[confOptList.indexOf(confOption("SystemMaxUse"))].isDefault())
        ui.chkMaxUse->setCheckState(Qt::Checked);
      else
        ui.chkMaxUse->setCheckState(Qt::Unchecked);
      if (confOptList[confOptList.indexOf(confOption("SystemKeepFree"))].isDefault())
        ui.chkKeepFree->setCheckState(Qt::Checked);
      else
        ui.chkKeepFree->setCheckState(Qt::Unchecked);
      if (confOptList[confOptList.indexOf(confOption("SystemMaxFileSize"))].isDefault())
        ui.chkMaxFileSize->setCheckState(Qt::Checked);
      else
        ui.chkMaxFileSize->setCheckState(Qt::Unchecked);
      
    } else if (ui.cmbStorage->currentText() == "volatile" || (ui.cmbStorage->currentText() == "auto" && !varLogDirExists)) {
      // using volatile storage:
      isPersistent = false;
      ui.spnMaxUse->setValue(confOptList.at(confOptList.indexOf(confOption("RuntimeMaxUse"))).getValue().toULongLong());
      ui.spnMaxUse->setMaximum(partVolaSizeMB);
      ui.spnKeepFree->setValue(confOptList.at(confOptList.indexOf(confOption("RuntimeKeepFree"))).getValue().toULongLong());
      ui.spnKeepFree->setMaximum(partVolaSizeMB);
      ui.spnMaxFileSize->setValue(confOptList.at(confOptList.indexOf(confOption("RuntimeMaxFileSize"))).getValue().toULongLong());
      ui.spnMaxFileSize->setMaximum(partVolaSizeMB);
      if (confOptList[confOptList.indexOf(confOption("RuntimeMaxUse"))].isDefault())
        ui.chkMaxUse->setCheckState(Qt::Checked);
      else
        ui.chkMaxUse->setCheckState(Qt::Unchecked);
      if (confOptList[confOptList.indexOf(confOption("RuntimeKeepFree"))].isDefault())
        ui.chkKeepFree->setCheckState(Qt::Checked);
      else
        ui.chkKeepFree->setCheckState(Qt::Unchecked);
      if (confOptList[confOptList.indexOf(confOption("RuntimeMaxFileSize"))].isDefault())
        ui.chkMaxFileSize->setCheckState(Qt::Checked);
      else
        ui.chkMaxFileSize->setCheckState(Qt::Unchecked);
    }
  }
  emit changed(true);
}

void kcmsystemd::slotStorageChkBoxes(int state)
{
  QString basename = QString(QObject::sender()->objectName().remove("chk"));
 
  if (state == Qt::Checked)
  {
    confOptList[confOptList.indexOf(confOption("System" + basename))].active = false;
    confOptList[confOptList.indexOf(confOption("Runtime" + basename))].active = false;
    
    QList<QWidget *> lst = this->findChildren<QWidget *>(QRegExp("(lbl|spn)" + basename + "\\d?"));
    foreach (QWidget *wdgt, lst)
      wdgt->setEnabled(false);

  }
  else
  {
   
    if (isPersistent)
    {
      confOptList[confOptList.indexOf(confOption("System" + basename))].active = true;
      confOptList[confOptList.indexOf(confOption("Runtime" + basename))].active = false;
    }
    else
    {
      confOptList[confOptList.indexOf(confOption("System" + basename))].active = false;
      confOptList[confOptList.indexOf(confOption("Runtime" + basename))].active = true;
    }
    QList<QWidget *> lst = this->findChildren<QWidget *>(QRegExp("(lbl|spn)" + basename + "\\d?"));
    foreach (QWidget *wdgt, lst)
      wdgt->setEnabled(true);
  }
  emit changed(true);
}

void kcmsystemd::slotSpnMaxUseChanged()
{
  if (isPersistent)
    confOptList[confOptList.indexOf(confOption("SystemMaxUse"))].setValue(ui.spnMaxUse->value());
  else
    confOptList[confOptList.indexOf(confOption("RuntimeMaxUse"))].setValue(ui.spnMaxUse->value());
  emit changed(true);
}

void kcmsystemd::slotSpnKeepFreeChanged()
{
  if (isPersistent)
    confOptList[confOptList.indexOf(confOption("SystemKeepFree"))].setValue(ui.spnKeepFree->value());
  else
    confOptList[confOptList.indexOf(confOption("RuntimeKeepFree"))].setValue(ui.spnKeepFree->value());
  emit changed(true);
}

void kcmsystemd::slotSpnMaxFileSizeChanged()
{
  if (isPersistent)
    confOptList[confOptList.indexOf(confOption("SystemMaxFileSize"))].setValue(ui.spnMaxFileSize->value());
  else
    confOptList[confOptList.indexOf(confOption("RuntimeMaxFileSize"))].setValue(ui.spnMaxFileSize->value());
  emit changed(true);
}

void kcmsystemd::slotFwdToSyslogChanged()
{
  if ( ui.chkForwardToSyslog->isChecked())
    ui.cmbMaxLevelSyslog->setEnabled(true);
  else
    ui.cmbMaxLevelSyslog->setEnabled(false);
  emit changed(true);
}

void kcmsystemd::slotFwdToKmsgChanged()
{
  if ( ui.chkForwardToKMsg->isChecked())
    ui.cmbMaxLevelKMsg->setEnabled(true);
  else
    ui.cmbMaxLevelKMsg->setEnabled(false);
  emit changed(true);
}

void kcmsystemd::slotFwdToConsoleChanged()
{
  if ( ui.chkForwardToConsole->isChecked()) {
    ui.leTTYPath->setEnabled(true);
    ui.cmbMaxLevelConsole->setEnabled(true);
  } else {
    ui.leTTYPath->setEnabled(false);
    ui.cmbMaxLevelConsole->setEnabled(false);
  }
  emit changed(true);
}

void kcmsystemd::slotFwdToWallChanged()
{
  if ( ui.chkForwardToWall->isChecked())
    ui.cmbMaxLevelWall->setEnabled(true);
  else
    ui.cmbMaxLevelWall->setEnabled(false);
  emit changed(true);
}

void kcmsystemd::slotKdeConfig()
{
  // Set a QString containing the kde prefix
  kdePrefix = QString::fromAscii(kdeConfig->readAllStandardOutput()).trimmed();
}

void kcmsystemd::slotKillUserProcessesChanged()
{
  if ( ui.chkKillUserProcesses->isChecked()) {
    ui.leKillOnlyUsers->setEnabled(true);
    ui.leKillExcludeUsers->setEnabled(true);
  } else {
    ui.leKillOnlyUsers->setEnabled(false);
    ui.leKillExcludeUsers->setEnabled(false);
  }
  emit changed(true);
}

void kcmsystemd::slotOpenResourceLimits()
{
  QPointer<ResLimitsDialog> resDialog = new ResLimitsDialog(this,
                                                            Qt::Dialog,
                                                            confOption::resLimitsMap);
  
  if (resDialog->exec() == QDialog::Accepted)
  {
    confOption::setResLimitsMap(resDialog->getResLimits());
    
    for(QVariantMap::const_iterator iter = confOption::resLimitsMap.begin(); iter != confOption::resLimitsMap.end(); ++iter)
      confOptList[confOptList.indexOf(confOption(iter.key()))].setValue(iter.value());
  }

  if (resDialog->getChanged())
    emit changed(true);  
  
  delete resDialog;
}

void kcmsystemd::slotOpenEnviron()
{
  QPointer<EnvironDialog> environDialog = new EnvironDialog(this,
                                                            Qt::Dialog,
                                                            confOptList.at(confOptList.indexOf(confOption("DefaultEnvironment"))).getValue().toString());
  if (environDialog->exec() == QDialog::Accepted)
  {
    confOptList[confOptList.indexOf(confOption("DefaultEnvironment"))].setValue(environDialog->getEnviron());
  }
    
  if (environDialog->getChanged())
    emit changed(true);
  
  delete environDialog;
    
}

void kcmsystemd::slotOpenAdvanced()
{
  QVariantMap args;
  args["systemdVersion"] = systemdVersion;
  args["JoinControllers"] = confOptList.at(confOptList.indexOf(confOption("JoinControllers"))).getValue();
  if (systemdVersion < 208)
    args["DefaultControllers"] = confOptList.at(confOptList.indexOf(confOption("DefaultControllers"))).getValue();
  args["TimerSlackNSec"] = confOptList.at(confOptList.indexOf(confOption("TimerSlackNSec"))).getValue();
  args["RuntimeWatchdogSec"] = confOptList.at(confOptList.indexOf(confOption("RuntimeWatchdogSec"))).getValue();
  args["ShutdownWatchdogSec"] = confOptList.at(confOptList.indexOf(confOption("ShutdownWatchdogSec"))).getValue();
  args["CPUAffinity"] = confOptList.at(confOptList.indexOf(confOption("CPUAffinity"))).getValue();
  args["CPUAffinityActive"] = confOptList.at(confOptList.indexOf(confOption("CPUAffinity"))).active;
  if (systemdVersion >= 209)
  {
    args["SystemCallArchitectures"] = confOptList.at(confOptList.indexOf(confOption("SystemCallArchitectures"))).getValue();
    args["SystemCallArchitecturesActive"] = confOptList.at(confOptList.indexOf(confOption("SystemCallArchitectures"))).active;
  }
  args["CapabilityBoundingSet"] = confOptList.at(confOptList.indexOf(confOption("CapabilityBoundingSet"))).getValue();
  args["CapabilityBoundingSetActive"] = confOptList.at(confOptList.indexOf(confOption("CapabilityBoundingSet"))).active;
  
  QPointer<AdvancedDialog> advancedDialog = new AdvancedDialog(this, Qt::Dialog, args);
 
  if (advancedDialog->exec() == QDialog::Accepted)
  {
    confOptList[confOptList.indexOf(confOption("JoinControllers"))].setValue(advancedDialog->getJoinControllers());
    if (systemdVersion < 208)
      confOptList[confOptList.indexOf(confOption("DefaultControllers"))].setValue(advancedDialog->getDefaultControllers());
    confOptList[confOptList.indexOf(confOption("TimerSlackNSec"))].setValue(advancedDialog->getTimerSlack());
    confOptList[confOptList.indexOf(confOption("RuntimeWatchdogSec"))].setValue(advancedDialog->getRuntimeWatchdog());
    confOptList[confOptList.indexOf(confOption("ShutdownWatchdogSec"))].setValue(advancedDialog->getShutdownWatchdog());
    confOptList[confOptList.indexOf(confOption("CPUAffinity"))].setValue(advancedDialog->getCPUAffinity());
    confOptList[confOptList.indexOf(confOption("CPUAffinity"))].active = advancedDialog->getCPUAffActive();
    if (systemdVersion >= 209)
    {
      confOptList[confOptList.indexOf(confOption("SystemCallArchitectures"))].setValue(advancedDialog->getSystemCallArchitectures());
      confOptList[confOptList.indexOf(confOption("SystemCallArchitectures"))].active = advancedDialog->getSysCallActive();
    }
    confOptList[confOptList.indexOf(confOption("CapabilityBoundingSet"))].setValue(advancedDialog->getCapabilities());
    confOptList[confOptList.indexOf(confOption("CapabilityBoundingSet"))].active = advancedDialog->getCapActive();
        
    if (advancedDialog->getChanged())
      emit changed(true);
  }

  delete advancedDialog;
}

void kcmsystemd::slotTblRowChanged(const QModelIndex &current, const QModelIndex &previous)
{
  // Find the selected unit
  selectedUnit = ui.tblServices->model()->index(current.row(),3).data().toString();

  // Find the selected row
  QModelIndex inProxyModelAct = proxyModelUnitId->mapToSource(const_cast<QModelIndex &> (current));
  selectedRow = proxyModelAct->mapToSource(inProxyModelAct).row();
  
  updateUnitProps(selectedUnit);
}

void kcmsystemd::slotBtnStartUnit()
{
  QList<QVariant> args;
  args.append(selectedUnit);
  args.append("replace");
  authServiceAction("org.freedesktop.systemd1", "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager", "StartUnit", args);
}

void kcmsystemd::slotBtnStopUnit()
{
  QList<QVariant> args;
  args.append("replace");
  authServiceAction("org.freedesktop.systemd1", unitpaths[selectedUnit].toString(), "org.freedesktop.systemd1.Unit", "Stop", args);
}

void kcmsystemd::slotBtnRestartUnit()
{
  QList<QVariant> args;
  args.append("replace");
  authServiceAction("org.freedesktop.systemd1", unitpaths[selectedUnit].toString(), "org.freedesktop.systemd1.Unit", "Restart", args);
}

void kcmsystemd::slotBtnReloadUnit()
{
  QList<QVariant> args;
  args.append("replace");
  authServiceAction("org.freedesktop.systemd1", unitpaths[selectedUnit].toString(), "org.freedesktop.systemd1.Unit", "Reload", args);
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
  for (int row = 0; row < unitsModel->rowCount(); ++row)
  {
    if (unitsModel->data(unitsModel->index(row,1), Qt::DisplayRole) == "inactive") {
      for (int col = 0; col < 4; ++col)
	unitsModel->setData(unitsModel->index(row,col), QVariant(QColor(Qt::red)), Qt::ForegroundRole);
    } else if (unitsModel->data(unitsModel->index(row,1), Qt::DisplayRole) == "active") {
      for (int col = 0; col < 4; ++col)
	unitsModel->setData(unitsModel->index(row,col), QVariant(QColor(Qt::darkGreen)), Qt::ForegroundRole);
    } else if (unitsModel->data(unitsModel->index(row,1), Qt::DisplayRole) == "failed") {
      for (int col = 0; col < 4; ++col)
	unitsModel->setData(unitsModel->index(row,col), QVariant(QColor(Qt::darkRed)), Qt::ForegroundRole); 
    } else if (unitsModel->data(unitsModel->index(row,1), Qt::DisplayRole) == "-") {
      for (int col = 0; col < 4; ++col)
	unitsModel->setData(unitsModel->index(row,col), QVariant(QColor(Qt::darkGray)), Qt::ForegroundRole);
    }
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
    ui.lblId->setText(iface->property("Id").toString());
    ui.lblDesc->setText(iface->property("Description").toString());
    ui.lblFragmentPath->setText(iface->property("FragmentPath").toString());
    if (iface->property("CanStart").toBool() && iface->property("ActiveState").toString() != "active")
      ui.btnStartUnit->setEnabled(true);
    else
      ui.btnStartUnit->setEnabled(false);
    if (iface->property("CanStop").toBool() && iface->property("ActiveState").toString() != "inactive"
					    && iface->property("ActiveState").toString() != "failed")
      ui.btnStopUnit->setEnabled(true);
    else
      ui.btnStopUnit->setEnabled(false);
    if (iface->property("CanStart").toBool() && iface->property("ActiveState").toString() != "inactive"
					     && iface->property("ActiveState").toString() != "failed")
      ui.btnRestartUnit->setEnabled(true);
    else
      ui.btnRestartUnit->setEnabled(false);
    if (iface->property("CanReload").toBool() && iface->property("ActiveState").toString() != "inactive"
					      && iface->property("ActiveState").toString() != "failed")
      ui.btnReloadUnit->setEnabled(true);
    else
      ui.btnReloadUnit->setEnabled(false);
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

    ui.lblId->setText(selectedUnit);
    ui.lblDesc->setText("");
    unitfile a;
    a.name = selectedUnit;
    if (!a.name.isEmpty())
    {
      ui.lblFragmentPath->setText(unitfileslist.at(unitfileslist.indexOf(a)).name);
      ui.lblUnitFileState->setText(iface->callWithArgumentList(QDBus::AutoDetect, "GetUnitFileState", args).arguments().at(0).toString());
      ui.btnStartUnit->setEnabled(true);
    } else {
      ui.lblFragmentPath->setText("");
      ui.lblUnitFileState->setText("");
      ui.btnStartUnit->setEnabled(false);
    }
    ui.lblTimeActivated->setText("");
    ui.lblTimeDeactivated->setText("");
    ui.btnStopUnit->setEnabled(false);
    ui.btnRestartUnit->setEnabled(false);
    ui.btnReloadUnit->setEnabled(false);
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
  // Function to call the helper to authenticate a call to systemd over the system bus

  // Declare a QVariantMap with arguments for the helper
  QVariantMap helperArgs;
  helperArgs["service"] = service;
  helperArgs["path"] = path;
  helperArgs["interface"] = interface;
  helperArgs["method"] = method;
  helperArgs["argsForCall"] = args;
    
  // Call the helper
  Action serviceAction("org.kde.kcontrol.kcmsystemd.dbusaction");
  serviceAction.setArguments(helperArgs);
  serviceAction.setHelperID("org.kde.kcontrol.kcmsystemd");
  ActionReply reply = serviceAction.execute();
  
  // Respond to reply of the helper
  if(reply.failed())
  {
    // Writing the configuration files failed
    if (reply.type() == ActionReply::KAuthError)
    {
      // Authorization error
      KMessageBox::error(this, i18n("Unable to authenticate/execute the action: code %1\n%2",
				    reply.errorCode(), reply.errorDescription()));
    }
    else 
    {
      // Other error
      KMessageBox::error(this, i18n("Unable to perform the action!"));
    }
  }
}

void kcmsystemd::slotDisplayMenu(const QPoint &pos)
{
  // Slot for creating the right-click menu over the unit list
  QMenu menu(this);
  QAction *edit = menu.addAction("&Edit unit file");
  QAction *isolate = menu.addAction("&Isolate unit");
  menu.addSeparator();
  QAction *enable = menu.addAction("En&able unit");
  QAction *disable = menu.addAction("&Disable unit");  
  
  QString unit = ui.tblServices->model()->index(ui.tblServices->indexAt(pos).row(),3).data().toString();
  QString path = unitpaths[unit].toString();
  
  QDBusConnection systembus = QDBusConnection::systemBus();
  QDBusInterface *iface = new QDBusInterface (
	  "org.freedesktop.systemd1",
	  "/org/freedesktop/systemd1",
	  "org.freedesktop.systemd1.Manager",
	  systembus,
	  this);  
  QList<QVariant> args;
  args << unit;
  QString UnitFileState = iface->callWithArgumentList(QDBus::AutoDetect, "GetUnitFileState", args).arguments().at(0).toString();
  
  iface = new QDBusInterface (
	  "org.freedesktop.systemd1",
	  path,
	  "org.freedesktop.systemd1.Unit",
	  systembus,
	  this);
  isolate->setEnabled(iface->property("CanIsolate").toBool());
  delete iface;
  
  if (UnitFileState == "disabled")
  {
    disable->setEnabled(false);
  } else if (UnitFileState == "enabled") {
    enable->setEnabled(false);
  } else {
    enable->setEnabled(false);    
    disable->setEnabled(false);
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
    kwrite.startDetached(kdePrefix + "/lib/kde4/libexec/kdesu", QStringList() << "-t" << "--" << "kwrite" << frpath);
  }
  if (a == isolate)
  {
    QList<QVariant> argsForCall;
    argsForCall << unit << "isolate";
    authServiceAction("org.freedesktop.systemd1",
	"/org/freedesktop/systemd1",
	"org.freedesktop.systemd1.Manager",
	"StartUnit",
	argsForCall);    
  }
  if (a == enable)
  {
    QList<QString> unitsForCall;
    QList<QVariant> argsForCall;
    unitsForCall << unit;
    argsForCall << QVariant(unitsForCall) << false << false;
    authServiceAction("org.freedesktop.systemd1",
	"/org/freedesktop/systemd1",
	"org.freedesktop.systemd1.Manager",
	"EnableUnitFiles",
	argsForCall);
  }
  if (a == disable)
  {
    QList<QString> unitsForCall;
    QList<QVariant> argsForCall;
    unitsForCall << unit;
    argsForCall << QVariant(unitsForCall) << false;
    authServiceAction("org.freedesktop.systemd1",
	"/org/freedesktop/systemd1",
	"org.freedesktop.systemd1.Manager",
	"DisableUnitFiles",
	argsForCall);
  }
}

bool kcmsystemd::eventFilter(QObject *watched, QEvent* event)
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
	  // If unit-id correesponds to column 3:
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

	    toolTipText.append("<FONT COLOR=black>");
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

void kcmsystemd::slotUnitLoaded(QString id, QDBusObjectPath path)
{
  // qDebug() << "Unit loaded: " << id << " (" << path.path() << ")";
}

void kcmsystemd::slotUnitUnloaded(QString id, QDBusObjectPath path)
{
  // qDebug() << "Unit unloaded: " << id << " (" << path.path() << ")";
}

void kcmsystemd::slotUnitFilesChanged()
{
  // qDebug() << "Unit files changed.";
  // slotRefreshUnitsList();
}

void kcmsystemd::slotPropertiesChanged(QString iface_name, QVariantMap changed_props, QStringList invalid_props)
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