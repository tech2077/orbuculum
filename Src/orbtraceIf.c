/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Orbtrace Interface Module
 * =========================
 *
 */

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "orbtraceIf.h"

/* List of device VID/PID pairs this library works with */
static const struct OrbtraceInterfaceType _validDevices[] =
{
    {
        0x1209, 0x3443, ( int [] )
        {
            900, 1000, 1050, 1100, 1200, 1250, 1350, 1500, 1670, 1800, 1900, 2000, 2100, 2200, 2300, 2400, 2500, 2600, 2700, 2800,
            2900, 3000, 3100, 3200, 3300, 3400, 3500, 3600, 4000, 4100, 4200, 4350, 5000, 0
        }
    },
    { 0, 0, NULL }
};

#define MIN_GENERIC_VOLTAGE_MV (900)
#define MAX_GENERIC_VOLTAGE_MV (5000)
#define MAX_VOLTAGE_DIFF_MV    (10)

/* Maximum descriptor length from USB specification */
#define MAX_USB_DESC_LEN (256)

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static void _flushDeviceList( struct OrbtraceIf *o )

/* Flush out any old device records */

{
    for ( size_t i = 0; i < o->numDevices; i++ )
    {
        if ( o->devices[i].sn )
        {
            free( o->devices[i].sn );
        }

        if ( o->devices[i].manufacturer )
        {
            free( o->devices[i].manufacturer );
        }

        if ( o->devices[i].product )
        {
            free( o->devices[i].product );
        }
    }

    o->numDevices = 0;
    o->devices = NULL;
}
// ====================================================================================================
static int _strcmpint( char *s1, char *s2 )

/* Version of strcmp that accomodates NULLs */

{
    if ( ( s1 ) && ( !s2 ) )
    {
        return -1;
    }

    if ( ( !s1 ) && ( s2 ) )
    {
        return 1;
    }

    return strcmp( s1, s2 );
}
// ====================================================================================================
static int _compareFunc( const void *vd1, const void *vd2 )

/* Return a comparison for two devices, used for qsort ordering */

{
    const struct OrbtraceIfDevice *d1 = ( const struct OrbtraceIfDevice * )vd1;
    const struct OrbtraceIfDevice *d2 = ( const struct OrbtraceIfDevice * )vd2;
    int r = 0;

    if ( ( r = _strcmpint( d1->manufacturer, d2->manufacturer ) ) )
    {
        return r;
    }

    if ( ( r = _strcmpint( d1->product, d2->product ) ) )
    {
        return r;
    }

    if ( ( r = strcmp( d1->sn, d2->sn ) ) )
    {
        return r;
    }

    if ( ( r = d1->vid - d2->vid ) )
    {
        return r;
    }

    return d1->pid - d2->pid;
}
// ====================================================================================================
uint16_t _getInterface( struct OrbtraceIf *o, char intType )

{
    struct libusb_config_descriptor *config;
    int iface = NO_INTERFACE;

    if ( ( libusb_get_active_config_descriptor( o->dev, &config ) ) >= 0 )
    {
        for ( int if_num = 0; if_num < config->bNumInterfaces; if_num++ )
        {
            const struct libusb_interface_descriptor *i = &config->interface[if_num].altsetting[0];

            if ( ( i->bInterfaceClass == 0xff ) && ( i->bInterfaceSubClass == intType ) )
            {
                iface = i->bInterfaceNumber;
                libusb_free_config_descriptor( config );
                break;
            }
        }
    }

    return iface;
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
int OrbtraceIfValidateVoltage( struct OrbtraceIf *o, int vmv )

/* Return matching voltage or zero if none can be found */

{
    /* If we don't have specific interface to reference use generic values */
    if ( ( !o ) || ( !o->dev ) )
    {
        return ( ( vmv >= MIN_GENERIC_VOLTAGE_MV ) && ( vmv <= ( MAX_GENERIC_VOLTAGE_MV + MAX_VOLTAGE_DIFF_MV ) ) ) ? vmv : 0;
    }
    else
    {
        /* We do have an interface identified, so use its values for match-checking */
        int *mv = o->devices[OrbtraceIfGetActiveDevnum( o )].type->voltageListmv;

        while ( ( *mv ) && ( *mv <= vmv ) )
        {
            mv++;
        }

        return ( ( vmv - * ( mv - 1 ) ) <= MAX_VOLTAGE_DIFF_MV ) ? *( mv - 1 ) : 0;
    }
}
// ====================================================================================================
struct OrbtraceIf *OrbtraceIfCreateContext( void )

{
    struct OrbtraceIf *o = ( struct OrbtraceIf * )calloc( 1, sizeof( struct OrbtraceIf ) );

    if ( libusb_init( &o->context ) < 0 )
    {
        free( o );
        return NULL;
    }
    else
    {
        //        libusb_set_debug(o->context, LIBUSB_LOG_LEVEL_DEBUG);
        return o;
    }
}

// ====================================================================================================
void OrbtraceIfDestroyContext( struct OrbtraceIf *o )

{
    assert( o );
    /* Flush out any old scans we might be holding */
    _flushDeviceList( o );

    if ( o->list )
    {
        libusb_free_device_list( o->list, 1 );
    }

    free( o );
}
// ====================================================================================================
int OrbtraceIfGetDeviceList( struct OrbtraceIf *o, char *sn )

/* Get list of devices that match (partial) serial number, VID and PID */

{
    size_t y;
    char tfrString[MAX_USB_DESC_LEN];
    struct OrbtraceIfDevice *d;

    assert( o );

    /* Close any active device */
    OrbtraceIfCloseDevice( o );

    /* Flush out any old scans we might be holding */
    _flushDeviceList( o );

    if ( o->list )
    {
        libusb_free_device_list( o->list, true );
    }

    int count = libusb_get_device_list( o->context, &o->list );

    for ( size_t i = 0; i < count; i++ )
    {
        o->dev = o->list[i];
        struct libusb_device_descriptor desc = { 0 };
        libusb_get_device_descriptor( o->dev, &desc );

        /* Loop through known devices to see if this one is one we recognise */
        for ( y = 0; ( ( _validDevices[y].vid ) &&
                       ( ( _validDevices[y].vid != desc.idVendor ) || ( _validDevices[y].pid != desc.idProduct ) ) ); y++ );

        /* If it's one we're interested in then process further */
        if ( _validDevices[y].vid )
        {
            /* We'll store this match for access later */
            o->devices = realloc( o->devices, ( o->numDevices + 1 ) * sizeof( struct OrbtraceIfDevice ) );
            d = &o->devices[o->numDevices];
            o->numDevices++;
            memset( d, 0, sizeof( struct OrbtraceIfDevice ) );

            if ( !libusb_open( o->list[i], &o->handle ) )
            {
                d->type = &_validDevices[y];
                d->powerIf = _getInterface( o, 'P' );
                d->traceIf = _getInterface( o, 'T' );

                if ( desc.iSerialNumber )
                {
                    libusb_get_string_descriptor_ascii( o->handle, desc.iSerialNumber, ( unsigned char * )tfrString, MAX_USB_DESC_LEN );
                }

                /* This is a match if no S/N match was requested or if there is a S/N and they part-match */
                if ( ( !sn ) || ( ( desc.iSerialNumber ) && ( strstr( tfrString, sn ) ) ) )
                {
                    d->sn = strdup( desc.iSerialNumber ? tfrString : "" );

                    if ( desc.iManufacturer )
                    {
                        libusb_get_string_descriptor_ascii( o->handle, desc.iManufacturer, ( unsigned char * )tfrString, MAX_USB_DESC_LEN );
                    }

                    d->manufacturer = strdup( desc.iManufacturer ? tfrString : "" );

                    if ( desc.iProduct )
                    {
                        libusb_get_string_descriptor_ascii( o->handle, desc.iProduct, ( unsigned char * )tfrString, MAX_USB_DESC_LEN );
                    }

                    d->product = strdup( desc.iProduct ? tfrString : "" );

                    d->devIndex = i;
                }

                libusb_close( o->handle );
                o->handle = NULL;
            }
        }
    }

    /* Now sort matching devices into defined order, so they're always the same way up */
    qsort( o->devices, o->numDevices, sizeof( struct OrbtraceIfDevice ), _compareFunc );

    return o->numDevices;
}
// ====================================================================================================
bool OrbtraceIfOpenDevice( struct OrbtraceIf *o, unsigned int entry )

{
    if ( entry >= o->numDevices )
    {
        return false;
    }

    o->dev = o->list[ o->devices[entry].devIndex];

    if ( libusb_open( o->dev, &o->handle ) )
    {
        o->dev = NULL;
        o->handle = NULL;
        return false;
    }

    o->activeDevice = entry;
    return true;
}
// ====================================================================================================
#include <stdio.h>
bool OrbtraceIfSetTraceWidth( struct OrbtraceIf *o, int width )

{
    uint16_t d = ( width != 4 ) ? width : 3;

    if ( ( ( d < 1 ) || ( d > 3 ) ) || ( OrbtraceIfGetTraceIF( o, OrbtraceIfGetActiveDevnum( o ) ) == NO_INTERFACE ) || ( !o->handle ) )
    {
        return false;
    }

    printf( "Sending libusb_control_transfer( %p, %02x, %02x, %d, %d, %p, %d, %d )\n", o->handle, 0x41, 0x01, d, OrbtraceIfGetTraceIF( o, OrbtraceIfGetActiveDevnum( o ) ), NULL, 0, 0 );
    libusb_control_transfer( o->handle, 0x41, 0x01, d, OrbtraceIfGetTraceIF( o, OrbtraceIfGetActiveDevnum( o ) ), NULL, 0, 0 );
    return true;
}
// ====================================================================================================
void OrbtraceIfCloseDevice( struct OrbtraceIf *o )

{
    if ( !o->handle )
    {
        return;
    }

    libusb_close( o->handle );
    o->activeDevice = NO_DEVICE;
    o->handle = NULL;
    o->dev = NULL;
}
// ====================================================================================================