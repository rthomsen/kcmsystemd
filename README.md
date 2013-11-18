Kcmsystemd
==========

Systemd control module for KDE. Provides a graphical frontend for modifying 
the systemd configuration files: system.conf, journald.conf and login.conf.
Integrates in the System Settings dialogue.

The module looks for the files in /etc/systemd and /usr/etc/systemd.


Installation
------------
cmake -DCMAKE_INSTALL_PREFIX=<kde_install_directory> .. 
make 
make install 


Dependencies
------------
KDE >= 4.4 
Qt >= 4.6 
Boost >= 1.45 


Developed by: Ragnar Thomsen (rthomsen6@gmail.com) 
Website: https://github.com/rthomsen/kcmsystemd 
