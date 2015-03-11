#ifndef SYSTEMDUNIT_H
#define SYSTEMDUNIT_H

// struct for storing units retrieved from systemd via DBus
struct SystemdUnit
{
  QString id, description, load_state, active_state, sub_state, following, job_type, unit_file, unit_file_status;
  QDBusObjectPath unit_path, job_path;
  unsigned int job_id;
  
  // The == operator must be provided to use contains() and indexOf()
  // on QLists of this struct
  bool operator==(const SystemdUnit& right) const
  {
    if (id == right.id)
      return true;
    else
      return false;
  }
  SystemdUnit(){};

  SystemdUnit(QString newId)
  {
    id = newId;
  }
};
Q_DECLARE_METATYPE(SystemdUnit)

// struct for storing sessions retrieved from logind via DBus
struct SystemdSession
{
  QString session_id, user_name, seat_id, session_state;
  QDBusObjectPath session_path;
  unsigned int user_id;

  // The == operator must be provided to use contains() and indexOf()
  // on QLists of this struct
  bool operator==(const SystemdSession& right) const
  {
    if (session_id == right.session_id)
      return true;
    else
      return false;
  }
};
Q_DECLARE_METATYPE(SystemdSession)

enum dbusBus
{
  sys, session, user
};

#endif // SYSTEMDUNIT_H