Kcmsystemd
==========

Systemd control module for KDE. Provides a graphical frontend for the systemd 
daemon, which allows for viewing and controlling systemd units, as well as 
modifying configuration files.
Integrates in the System Settings dialogue in KDE.


Installation
------------
mkdir build  
cd build  
cmake -DCMAKE_INSTALL_PREFIX=\`kde4-config --prefix\` ..  
make  
make install  


Dependencies
------------
*   KDE >= 4.4  
*   Qt >= 4.6  
*   Boost >= 1.45  


Execution
---------
Kcmsystemd can be accessed through System Settings, or by issuing the command:
kcmshell4 kcm_systemd


Developed by: Ragnar Thomsen (rthomsen6@gmail.com)  
Website: https://github.com/rthomsen/kcmsystemd  
