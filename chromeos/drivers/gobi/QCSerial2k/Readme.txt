Gobi2000 Serial driver for HP 1.0.80
04/19/2010

This readme covers important information concerning 
the QCSerial2kHP driver.

Table of Contents

1. Prerequisites
2. Installation instructions
3. What's new in this release
4. Notes
5. Known issues
6. Known platform issues

-------------------------------------------------------------------------------

1. PREREQUISITES

a. Kernel headers or full kernel source installed for the currently 
   running kernel.  There must be a link "/lib/modules/<uname -r>/build"
   that points to these kernel headers or sources.
b. The kernel must support the usbcore and usbserial drivers, either as
   modules or built into the kernel.
c. Tools required for building the kernel.  These tools usually come in a 
   package called "kernel-devel".
d. "gcc" compiler
e. "make" tool

-------------------------------------------------------------------------------

2. INSTALLATION INSTRUCTIONS

a. Navigate to the folder "QCSerial2k" that contains:
      Makefile
      QCSerial2kHP.c
b. Run the command:
      make
c. Copy the newly created QCSerial2kHP.ko into the directory
   /lib/modules/`uname -r`/kernel/drivers/usb/serial/
d. Run the command:
      depmod
e. (Optional) Load the driver manually with the command:
      modprobe QCSerial2kHP
   - This is only required the first time after you install the
     drivers.  If you restart or plug the Gobi device in the drivers 
     will be automatically loaded.

-------------------------------------------------------------------------------

3. WHAT'S NEW

This Release (Gobi2000 Serial driver for HP 1.0.80) 04/19/2010
a. Addition of QCResume function for 2.6.23 kernel compatibility
b. Change driver versions to match installer package

Prior Release (in correlation with Gobi2000-Linux-Package 1.0.03) 09/10/2009
a. Minor update for 2.6.29 kernel compatibility
b. Addition of QCSuspend function to ensure device is rescanned
   after hibernation.

-------------------------------------------------------------------------------

4. NOTES

a. In QDL mode, the Gobi 2000 device will enumerate a serial port 
   /dev/ttyUSB<#> where <#> signifies the next available serial 
   device node.  This device node is for use by the QDLService2kHP.
b. In Composite mode, the Gobi device will enumerate a serial modem
   at /dev/ttyUSB<#>.
c. Ownership, group, and permissions are managed by your system 
   configuration.

-------------------------------------------------------------------------------

5. KNOWN ISSUES

No known issues.

-------------------------------------------------------------------------------

6. KNOWN PLATFORM ISSUES

a. If a user attempts to obtain a Simple IP address the device may become
   unresponsive to AT commands and will need to be restarted.  This may be
   a result of a problem in one or more of the following open source products:
      1. pppd daemon
      2. ppp protocol stack
      3. USB driver
   
   

-------------------------------------------------------------------------------



