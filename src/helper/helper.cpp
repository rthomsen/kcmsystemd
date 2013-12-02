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

#include "helper.h"

#include <QtDBus>

#include "../config.h"

ActionReply Helper::save(QVariantMap args)
{
  ActionReply reply;
  
  // Declare QStrings with file names and contents
  QString systemConfFileContents = args["systemConfFileContents"].toString();
  QString journaldConfFileContents = args["journaldConfFileContents"].toString();
  QString logindConfFileContents = args["logindConfFileContents"].toString();
   
  // write system.conf
  QFile sysFile(args["etcDir"].toString() + "/system.conf");
  if (!sysFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    reply = ActionReply::HelperErrorReply;
    reply.addData("errorDescription", sysFile.errorString());
    reply.setErrorCode(sysFile.error());
    reply.addData("filename", "system.conf");
    return reply;
  }
  QTextStream sysStream(&sysFile);
  sysStream << systemConfFileContents;
  sysFile.close();

  // write journald.conf
  QFile jrnlFile(args["etcDir"].toString() + "/journald.conf");
  if (!jrnlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
      reply = ActionReply::HelperErrorReply;
      reply.setErrorCode(jrnlFile.error());
      reply.addData("filename", "journald.conf");
      return reply;
  }
  QTextStream jrnlStream(&jrnlFile);
  jrnlStream << journaldConfFileContents;
  jrnlFile.close();
  
  // write logind.conf
  QFile loginFile(args["etcDir"].toString() + "/logind.conf");
  if (!loginFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
      reply = ActionReply::HelperErrorReply;
      reply.setErrorCode(loginFile.error());
      reply.addData("filename", "logind.conf");
      return reply;
  }
  QTextStream loginStream(&loginFile);
  loginStream << logindConfFileContents;
  loginFile.close();
  
  // return a reply
  return reply;
}

ActionReply Helper::dbusaction(QVariantMap args)
{
  ActionReply reply;
  QDBusMessage dbusreply;
  
  // Get arguments to method call
  QString service = args["service"].toString();
  QString path = args["path"].toString();
  QString interface = args["interface"].toString();
  QString method = args["method"].toString();
  QList<QVariant> argsForCall = args["argsForCall"].toList();
  
  QDBusConnection systembus = QDBusConnection::systemBus();  
  QDBusInterface *iface = new QDBusInterface (service,
					      path,
					      interface,
					      systembus,
					      this);
  if (iface->isValid())
    dbusreply = iface->callWithArgumentList(QDBus::AutoDetect, method, argsForCall);
  delete iface;
  
  // Error handling
  if (dbusreply.type() == QDBusMessage::ErrorMessage)
  {
    reply.setErrorCode(ActionReply::DBusError);
    reply.setErrorDescription(dbusreply.errorMessage());
  }

  // Reload systemd daemon to update the enabled/disabled status
  if (method == "EnableUnitFiles" || method == "DisableUnitFiles")
  {
    // systemd does not update properties when these methods are called so we
    // need to reload the systemd daemon.
    iface = new QDBusInterface ("org.freedesktop.systemd1",
						"/org/freedesktop/systemd1",
						"org.freedesktop.systemd1.Manager",
						systembus,
						this);
    dbusreply = iface->call(QDBus::AutoDetect, "Reload");
    delete iface;
  }
  // return a reply
  return reply;
}

KDE4_AUTH_HELPER_MAIN("org.kde.kcontrol.kcmsystemd", Helper)