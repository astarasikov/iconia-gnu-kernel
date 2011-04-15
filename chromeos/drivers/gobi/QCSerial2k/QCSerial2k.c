/*===========================================================================
FILE: 
   QCSerial2k.c

DESCRIPTION:
   Linux Qualcomm Serial USB driver Implementation 

PUBLIC DRIVER FUNCTIONS:
   QCProbe
   QCReadBulkCallback (if kernel is less than 2.6.25)
   QCSuspend

Copyright (c) 2010, Code Aurora Forum. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Code Aurora Forum nor
      the names of its contributors may be used to endorse or promote
      products derived from this software without specific prior written
      permission.

Alternatively, provided that this notice is retained in full, this software
may be relicensed by the recipient under the terms of the GNU General Public
License version 2 ("GPL") and only version 2, in which case the provisions of
the GPL apply INSTEAD OF those given above.  If the recipient relicenses the
software under the GPL, then the identification text in the MODULE_LICENSE
macro must be changed to reflect "GPLv2" instead of "Dual BSD/GPL".  Once a
recipient changes the license terms to the GPL, subsequent recipients shall
not relicense under alternate licensing terms, including the BSD or dual
BSD/GPL terms.  In addition, the following license statement immediately
below and between the words START and END shall also then apply when this
software is relicensed under the GPL:

START

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License version 2 and only version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

END

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
==========================================================================*/
//---------------------------------------------------------------------------
// Include Files
//---------------------------------------------------------------------------

#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/usb.h> 
#include <linux/usb/serial.h>
#include <linux/version.h>

//---------------------------------------------------------------------------
// Global veriable and defination
//---------------------------------------------------------------------------

// Version Information
#define DRIVER_VERSION "1.0.80"
#define DRIVER_AUTHOR "Qualcomm Innovation Center"
#define DRIVER_DESC "QCSerial2k"

#define NUM_BULK_EPS         1
#define MAX_BULK_EPS         6

// Debug flag
static int debug;

// DBG macro
#define DBG( format, arg... ) \
   if (debug == 1)\
   { \
      printk( KERN_INFO "QCSerial2k::%s " format, __FUNCTION__, ## arg ); \
   } \

/*=========================================================================*/
// Function Prototypes
/*=========================================================================*/

// Attach to correct interfaces
static int QCProbe(
   struct usb_serial * pSerial, 
   const struct usb_device_id * pID );

#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,25 ))

// Read data from USB, push to TTY and user space
static void QCReadBulkCallback( struct urb * pURB );

#endif

// Set reset_resume flag
int QCSuspend( 
   struct usb_interface *     pIntf,
   pm_message_t               powerEvent );

#if (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,23 ))

// Restart URBs killed during usb_serial_suspend
int QCResume( struct usb_interface * pIntf );

#endif

/*=========================================================================*/
// Qualcomm Gobi 2000 VID/PIDs
/*=========================================================================*/
static struct usb_device_id QCVIDPIDTable[] = 
{
   { USB_DEVICE(0x05c6, 0x9214) },   // Acer Gobi 2000 QDL device
   { USB_DEVICE(0x05c6, 0x9215) },   // Acer Gobi 2000 Modem Device
   { USB_DEVICE(0x05c6, 0x9264) },   // Asus Gobi 2000 QDL device
   { USB_DEVICE(0x05c6, 0x9265) },   // Asus Gobi 2000 Modem Device
   { USB_DEVICE(0x16d8, 0x8001) },   // CMOTech Gobi 2000 QDL device
   { USB_DEVICE(0x16d8, 0x8002) },   // CMOTech Gobi 2000 Modem Device
   { USB_DEVICE(0x413c, 0x8185) },   // Dell Gobi 2000 QDL device
   { USB_DEVICE(0x413c, 0x8186) },   // Dell Gobi 2000 Modem Device
   { USB_DEVICE(0x1410, 0xa014) },   // Entourage Gobi 2000 QDL device
   { USB_DEVICE(0x1410, 0xa010) },   // Entourage Gobi 2000 Modem Device
   { USB_DEVICE(0x1410, 0xa011) },   // Entourage Gobi 2000 Modem Device
   { USB_DEVICE(0x1410, 0xa012) },   // Entourage Gobi 2000 Modem Device
   { USB_DEVICE(0x1410, 0xa013) },   // Entourage Gobi 2000 Modem Device
   { USB_DEVICE(0x03f0, 0x241d) },   // HP Gobi 2000 QDL device
   { USB_DEVICE(0x03f0, 0x251d) },   // HP Gobi 2000 Modem Device
   { USB_DEVICE(0x05c6, 0x9204) },   // Lenovo Gobi 2000 QDL device
   { USB_DEVICE(0x05c6, 0x9205) },   // Lenovo Gobi 2000 Modem Device
   { USB_DEVICE(0x05c6, 0x9208) },   // Generic Gobi 2000 QDL device
   { USB_DEVICE(0x05c6, 0x920b) },   // Generic Gobi 2000 Modem Device
   { USB_DEVICE(0x04da, 0x250e) },   // Panasonic Gobi 2000 QDL device
   { USB_DEVICE(0x04da, 0x250f) },   // Panasonic Gobi 2000 Modem Device
   { USB_DEVICE(0x05c6, 0x9244) },   // Samsung Gobi 2000 QDL device
   { USB_DEVICE(0x05c6, 0x9245) },   // Samsung Gobi 2000 Modem Device
   { USB_DEVICE(0x1199, 0x9000) },   // Sierra Wireless Gobi 2000 QDL device
   { USB_DEVICE(0x1199, 0x9001) },   // Sierra Wireless Gobi 2000 Modem Device
   { USB_DEVICE(0x1199, 0x9002) },   // Sierra Wireless Gobi 2000 Modem Device
   { USB_DEVICE(0x1199, 0x9003) },   // Sierra Wireless Gobi 2000 Modem Device
   { USB_DEVICE(0x1199, 0x9004) },   // Sierra Wireless Gobi 2000 Modem Device
   { USB_DEVICE(0x1199, 0x9005) },   // Sierra Wireless Gobi 2000 Modem Device
   { USB_DEVICE(0x1199, 0x9006) },   // Sierra Wireless Gobi 2000 Modem Device
   { USB_DEVICE(0x1199, 0x9007) },   // Sierra Wireless Gobi 2000 Modem Device
   { USB_DEVICE(0x1199, 0x9008) },   // Sierra Wireless Gobi 2000 Modem Device
   { USB_DEVICE(0x1199, 0x9009) },   // Sierra Wireless Gobi 2000 Modem Device
   { USB_DEVICE(0x1199, 0x900a) },   // Sierra Wireless Gobi 2000 Modem Device
   { USB_DEVICE(0x05c6, 0x9224) },   // Sony Gobi 2000 QDL device
   { USB_DEVICE(0x05c6, 0x9225) },   // Sony Gobi 2000 Modem Device
   { USB_DEVICE(0x05c6, 0x9234) },   // Top Global Gobi 2000 QDL device
   { USB_DEVICE(0x05c6, 0x9235) },   // Top Global Gobi 2000 Modem Device
   { USB_DEVICE(0x05c6, 0x9274) },   // iRex Technologies Gobi 2000 QDL device
   { USB_DEVICE(0x05c6, 0x9275) },   // iRex Technologies Gobi 2000 Modem Device

   { }                               // Terminating entry
};
MODULE_DEVICE_TABLE( usb, QCVIDPIDTable );

/*=========================================================================*/
// Struct usb_serial_driver
// Driver structure we register with the USB core
/*=========================================================================*/
static struct usb_driver QCDriver = 
{
   .name       = "QCSerial2k",
   .probe      = usb_serial_probe,
   .disconnect = usb_serial_disconnect,
   .id_table   = QCVIDPIDTable,
   .suspend    = QCSuspend,
#if (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,23 ))
   .resume     = QCResume,
#else
   .resume     = usb_serial_resume,
#endif
   .supports_autosuspend = true,
};

/*=========================================================================*/
// Struct usb_serial_driver
/*=========================================================================*/
static struct usb_serial_driver gQCDevice = 
{
   .driver = 
   {   
      .owner     = THIS_MODULE,
      .name      = "QCSerial2k driver",
   },
   .description         = "QCSerial2k",
   .id_table            = QCVIDPIDTable,
   .usb_driver          = &QCDriver,
   .num_ports           = 1,
   .probe               = QCProbe,
#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,25 ))
   .num_interrupt_in    = NUM_DONT_CARE,
   .num_bulk_in         = 1,
   .num_bulk_out        = 1,
   .read_bulk_callback  = QCReadBulkCallback,
#endif
};

//---------------------------------------------------------------------------
// USB serial core overridding Methods
//---------------------------------------------------------------------------

/*===========================================================================
METHOD:
   QCProbe (Free Method)

DESCRIPTION:
   Attach to correct interfaces

PARAMETERS:
   pSerial    [ I ] - Serial structure 
   pID        [ I ] - VID PID table

RETURN VALUE:
   int - negative error code on failure
         zero on success
===========================================================================*/
static int QCProbe(
   struct usb_serial * pSerial, 
   const struct usb_device_id * pID )
{
   // Assume failure
   int nRetval = -ENODEV;

   int nNumInterfaces;
   int nInterfaceNum;
   DBG( "\n" );

   nNumInterfaces = pSerial->dev->actconfig->desc.bNumInterfaces;
   DBG( "Num Interfaces = %d\n", nNumInterfaces );
   nInterfaceNum = pSerial->interface->cur_altsetting->desc.bInterfaceNumber;
   DBG( "This Interface = %d\n", nInterfaceNum );
   
   if (nNumInterfaces == 1)
   {
      // QDL mode?
      if (nInterfaceNum == 1) 
      {
         DBG( "QDL port found\n" );
         nRetval = usb_set_interface( pSerial->dev, 
                                      nInterfaceNum, 
                                      0 );
         if (nRetval < 0)
         {
            DBG( "Could not set interface, error %d\n", nRetval );
         }
      }
      else
      {
         DBG( "Incorrect QDL interface number\n" );
      }
   }
   else if (nNumInterfaces == 4 || nNumInterfaces == 3)
   {
      // Composite mode
      if (nInterfaceNum == 2) 
      {
         DBG( "Modem port found\n" );
         nRetval = usb_set_interface( pSerial->dev, 
                                      nInterfaceNum, 
                                      0 );
         if (nRetval < 0)
         {
            DBG( "Could not set interface, error %d\n", nRetval );
         }
      }
      else
      {
         // Not a port we want to support at this time
         DBG( "Unsupported interface number\n" );
      }
   }
   else
   {
      DBG( "Incorrect number of interfaces\n" );
   }

   return nRetval;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,25 ))

/*===========================================================================
METHOD:
   QCReadBulkCallback (Free Method)

DESCRIPTION:
   Read data from USB, push to TTY and user space

PARAMETERS:
   pURB  [ I ] - USB Request Block (urb) that called us 

RETURN VALUE:
===========================================================================*/
static void QCReadBulkCallback( struct urb * pURB )
{
   struct usb_serial_port * pPort = pURB->context;
   struct tty_struct * pTTY = pPort->tty;
   int nResult;
   int nRoom = 0;
   unsigned int pipeEP;
   
   DBG( "port %d\n", pPort->number );

   if (pURB->status != 0) 
   {
      DBG( "nonzero read bulk status received: %d\n", pURB->status );

      return;
   }

   usb_serial_debug_data( debug, 
                          &pPort->dev, 
                          __FUNCTION__, 
                          pURB->actual_length, 
                          pURB->transfer_buffer );

   // We do no port throttling

   // Push data to tty layer and user space read function
   if (pTTY != 0 && pURB->actual_length) 
   {
      nRoom = tty_buffer_request_room( pTTY, pURB->actual_length );
      DBG( "room size %d %d\n", nRoom, 512 );
      if (nRoom != 0)
      {
         tty_insert_flip_string( pTTY, pURB->transfer_buffer, nRoom );
         tty_flip_buffer_push( pTTY );
      }
   }

   pipeEP = usb_rcvbulkpipe( pPort->serial->dev, 
                             pPort->bulk_in_endpointAddress );

   // For continuous reading
   usb_fill_bulk_urb( pPort->read_urb, 
                      pPort->serial->dev,
                      pipeEP,
                      pPort->read_urb->transfer_buffer,
                      pPort->read_urb->transfer_buffer_length,
                      QCReadBulkCallback, 
                      pPort );
   
   nResult = usb_submit_urb( pPort->read_urb, GFP_ATOMIC );
   if (nResult != 0)
   {
      DBG( "failed resubmitting read urb, error %d\n", nResult );
   }
}

#endif

/*===========================================================================
METHOD:
   QCSuspend (Public Method)

DESCRIPTION:
   Set reset_resume flag

PARAMETERS
   pIntf          [ I ] - Pointer to interface
   powerEvent     [ I ] - Power management event

RETURN VALUE:
   int - 0 for success
         negative errno for failure
===========================================================================*/
int QCSuspend( 
   struct usb_interface *     pIntf,
   pm_message_t               powerEvent )
{
   struct usb_serial * pDev;
   
   if (pIntf == 0)
   {
      return -ENOMEM;
   }
   
   pDev = usb_get_intfdata( pIntf );
   if (pDev == NULL)
   {
      return -ENXIO;
   }

   // Unless this is PM_EVENT_SUSPEND, make sure device gets rescanned
   if ((powerEvent.event & PM_EVENT_SUSPEND) == 0)
   {
      pDev->dev->reset_resume = 1;
   }
   
   // Run usb_serial's suspend function
   return usb_serial_suspend( pIntf, powerEvent );
}

#if (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,23 ))

/*===========================================================================
METHOD:
   QCResume (Free Method)

DESCRIPTION:
   Restart URBs killed during usb_serial_suspend

   Fixes 2 bugs in 2.6.23 kernel
      1. pSerial->type->resume was NULL and unchecked, caused crash.
      2. set_to_generic_if_null was not run for resume.

PARAMETERS:
   pIntf  [ I ] - Pointer to interface

RETURN VALUE:
   int - 0 for success
         negative errno for failure
===========================================================================*/
int QCResume( struct usb_interface * pIntf )
{
	struct usb_serial * pSerial = usb_get_intfdata( pIntf );
	struct usb_serial_port * pPort;
   int portIndex, errors, nResult;

   if (pSerial == NULL)
   {
      DBG( "no pSerial\n" );
      return -ENOMEM;
   }
   if (pSerial->type == NULL)
   {
      DBG( "no pSerial->type\n" );
      return ENOMEM;
   }
   if (pSerial->type->resume == NULL)
   {
      // Expected behaviour in 2.6.23, in later kernels this was handled
      // by the usb-serial driver and usb_serial_generic_resume
      errors = 0;
      for (portIndex = 0; portIndex < pSerial->num_ports; portIndex++)
      {
	      pPort = pSerial->port[portIndex];
	      if (pPort->open_count > 0 && pPort->read_urb != NULL)
	      {
		      nResult = usb_submit_urb( pPort->read_urb, GFP_NOIO );
		      if (nResult < 0)
            {
               // Return first error we see
               DBG( "error %d\n", nResult );
			      return nResult;
            }
	      }
      }

      // Success
      return 0;
   }

   // Execution would only reach this point if user has
   // patched version of usb-serial driver.
	return usb_serial_resume( pIntf );
}

#endif

/*===========================================================================
METHOD:
   QCInit (Free Method)

DESCRIPTION:
   Register the driver and device

PARAMETERS:

RETURN VALUE:
   int - negative error code on failure
         zero on success
===========================================================================*/
static int __init QCInit( void )
{
   int nRetval = 0;

   gQCDevice.num_ports = NUM_BULK_EPS;

   // Registering driver to USB serial core layer 
   nRetval = usb_serial_register( &gQCDevice );
   if (nRetval != 0)
   {
      return nRetval;
   }

   // Registering driver to USB core layer
   nRetval = usb_register( &QCDriver );
   if (nRetval != 0) 
   {
      usb_serial_deregister( &gQCDevice );
      return nRetval;
   }

   // This will be shown whenever driver is loaded
   printk( KERN_INFO "%s: %s\n", DRIVER_DESC, DRIVER_VERSION );
   
   return nRetval;
}

/*===========================================================================
METHOD:
   QCExit (Free Method)

DESCRIPTION:
   Deregister the driver and device

PARAMETERS:

RETURN VALUE:
===========================================================================*/
static void __exit QCExit( void )
{
   usb_deregister( &QCDriver );
   usb_serial_deregister( &gQCDevice );
}

// Calling kernel module to init our driver
module_init( QCInit );
module_exit( QCExit );

MODULE_VERSION( DRIVER_VERSION );
MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE( "Dual BSD/GPL" );

module_param( debug, bool, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC( debug, "Debug enabled or not" );
