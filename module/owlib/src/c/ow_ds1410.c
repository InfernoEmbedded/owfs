/*
$Id$
    OWFS -- One-Wire filesystem
    OWHTTPD -- One-Wire Web Server
    Written 2003 Paul H Alfille
    email: palfille@earthlink.net
    Released under the GPL
    See the header file: ow.h for full attribution
    1wire/iButton system from Dallas Semiconductor
*/

#include "owfs_config.h"
#include "ow.h"

#include <sys/ioctl.h>
#include <linux/ppdev.h>
#include <linux/parport.h>

void DS1410_setroutines( struct interface_routines * const f ) ;
static int DS1410_PowerByte(const unsigned char byte, const unsigned int delay) ;
static int DS1410_ProgramPulse( void ) ;
static int DS1410_next_both(unsigned char * serialnumber, unsigned char search) ;
//int DS1410_detect( void ) ;
static int DS1410_reset( void ) ;
static int DS1410_read(unsigned char * const buf, const int size ) ;
static int DS1410_write( const unsigned char * const bytes, const size_t num ) ;
static int DS1410_level(int new_level) ;
static int DS1410_read_bits( unsigned char * const bits , const int length ) ;
static int DS1410_sendback_bits( const unsigned char * const outbits , unsigned char * const inbits , const int length ) ;
static int DS1410_sendback_data( const unsigned char * const data , unsigned char * const resp , const int len ) ;
static int DS1410_send_bit( const unsigned char data, unsigned char * const resp ) ;
static int DS1410_status_loop( const unsigned int flag, const int * const waits ) ;

static int normal_busy[] = {  1,1,1,1, 1,1,1,1, 1,1,0, } ;
static int normal_slot[] = { 50,5,5,5, 5,5,5,5, 5,5,0, } ;

#define PIN2  0x01
#define PIN3  0x02
#define PIN14 DS1410_ENI
#define PIN11 DS1410_O1BSY
#define PIN13 DS1410_O2BSY

/* Data */
#define DS1410_Bit0  (0xFC | 0x02 | 0x00 )
#define DS1410_Bit1  (0xFC | 0x02 | 0x01 )
#define DS1410_Reset (0xFC | 0x00 | 0x01 )
/* Control */
#define DS1410_ENI PARPORT_CONTROL_AUTOFD /* inverted */
/* Status */
#define DS1410_O1BSY PARPORT_STATUS_BUSY /* inverted */
#define DS1410_O2BSY PARPORT_STATUS_SELECT

//--------------------------------------------------------------------------
// Send 8 bits of communication to the 1-Wire Net and verify that the
// 8 bits read from the 1-Wire Net is the same (write operation).
// The parameter 'byte' least significant 8 bits are used.  After the
// 8 bits are sent change the level of the 1-Wire net.
// Delay delay msec and return to normal
//
/* Returns 0=good
   bad = -EIO
 */
static int DS1410_PowerByte(unsigned char byte, unsigned int delay) {
    int ret ;

    // flush the buffers
    COM_flush();

    // send the packet
    BUS_lock() ;
        ret=BUS_send_data(&byte,1) ;
    BUS_unlock() ;
    if (ret) return ret ;

// indicate the port is now at power delivery
    ULevel = MODE_STRONG5;

    // delay
    UT_delay( delay ) ;

    // return to normal level
    BUS_lock() ;
        ret=BUS_level(MODE_NORMAL) ;
    BUS_unlock() ;

    return ret ;
}

static int DS1410_ProgramPulse( void ) {
    return -EIO; /* not available */
}


static int DS1410_next_both(unsigned char * serialnumber, unsigned char search) {
    unsigned char search_direction = 0 ; /* initilization just to forestall incorrect compiler warning */
    unsigned char bit_number ;
    unsigned char last_zero ;
    unsigned char bits[3] ;
    int ret ;

    // initialize for search
    last_zero = 0;

    // if the last call was not the last one
    if ( !AnyDevices ) LastDevice = 1 ;
    if ( LastDevice ) return -ENODEV ;

    /* Appropriate search command */
    if ( (ret=BUS_send_data(&search,1)) ) return ret ;
      // loop to do the search
    for ( bit_number=0; 1 ; ++bit_number ) {
        bits[1] = bits[2] = 0xFF ;
        if ( bit_number==0 ) {
            if ( (ret=DS1410_sendback_bits(&bits[1],&bits[1],2)) ) return ret ;
        } else {
            bits[0] = search_direction ;
            if ( bit_number<64 ) {
                if ( (ret=DS1410_sendback_bits(bits,bits,3)) ) return ret ;
            } else {
                if ( (ret=DS1410_sendback_bits(bits,bits,1)) ) return ret ;
                break ;
            }
        }
        if ( bits[1] ) {
            if ( bits[2] ) {
                break ; /* No devices respond */
            } else {
                search_direction = 1;  // bit write value for search
            }
        } else {
            if ( bits[2] ) {
                search_direction = 0;  // bit write value for search
            } else  if (bit_number < LastDiscrepancy) {
                // if this discrepancy if before the Last Discrepancy
                // on a previous next then pick the same as last time
                search_direction = UT_getbit(serialnumber,bit_number) ;
            } else if (bit_number == LastDiscrepancy) {
                search_direction = 1 ; // if equal to last pick 1, if not then pick 0
            } else {
                search_direction = 0 ;
                // if 0 was picked then record its position in LastZero
                last_zero = bit_number;
                // check for Last discrepancy in family
                if (last_zero < 9) LastFamilyDiscrepancy = last_zero;
            }
        }
        UT_setbit(serialnumber,bit_number,search_direction) ;

        // serial number search direction write bit
        //if ( (ret=DS1410_sendback_bits(&search_direction,bits,1)) ) return ret ;
    } // loop until through serial number bits

    if ( CRC8(serialnumber,8) || (bit_number<64) || (serialnumber[0] == 0)) return -EIO ;
      // if the search was successful then

    LastDiscrepancy = last_zero;
    LastDevice = (last_zero == 0);
    return 0 ;
}

/* Set the 1-Wire Net line level.  The values for new_level are
// 'new_level' - new level defined as
//                MODE_NORMAL     0x00
//                MODE_STRONG5    0x02
//                MODE_PROGRAM    0x04
//                MODE_BREAK      0x08 (not supported)
//
// Returns:    0 GOOD, !0 Error
   Actually very simple for passive adapter
*/
/* return 0=good
  -EIO not supported
 */
static int DS1410_level(int new_level) {
    if (new_level == ULevel) {     // check if need to change level
        return 0 ;
    }
    switch (new_level) {
    case MODE_NORMAL:
    case MODE_STRONG5:
        ULevel = new_level ;
        return 0 ;
    case MODE_PROGRAM:
        ULevel = MODE_NORMAL ;
        return -EIO ;
    }
    return -EIO ;
}

/* Open a DS1410 after an unsucessful DS2480_detect attempt */
/* _detect is a bit of a misnomer, no detection is actually done */
/* Note, devfd alread allocated */
/* Note, terminal settings already saved */
int DS1410_detect( void ) {
    int mode = IEEE1284_MODE_BYTE ;
    struct timeval t = { 1 , 0 } ; /* 1 second */
    int ret = ioctl( devfd, PPCLAIM) ;
//    ret = ioctl( devfd, PPNEGOT, &mode) ;
    ret = ioctl( devfd, PPSETMODE, &mode) ;
printf("SETMODE=%d\n",ret);
    ret = ioctl( devfd, PPGETMODE, &mode) ;
printf("GETMODE=%d\n",ret);
printf("NEGOT=%d\n",ret);
    ret = ioctl( devfd, PPSETTIME, &t) ;
printf("SETTIME=%d\n",ret);
    ioctl( devfd , PPRELEASE ) ;
switch(mode) {
case IEEE1284_MODE_COMPAT: printf("COMPAT\n");break;
case IEEE1284_MODE_NIBBLE: printf("NIBBLE\n");break;
case IEEE1284_MODE_BYTE: printf("BYTE\n");break;
case IEEE1284_MODE_EPP: printf("EPP\n");break;
case IEEE1284_MODE_ECP: printf("ECP\n");break;
default: printf("NO MODE\n");
}
    /* Set up low-level routines */
    DS1410_setroutines( & iroutines ) ;
    /* Reset the bus */
    return DS1410_reset() ;
}

/* DS1410 Reset -- A little different frolm DS2480B */
/* Puts in 9600 baud, sends 11110000 then reads response */
static int DS1410_reset( void ) {
    unsigned char c;
    int ret ;

    if ( (ret=DS1410_send_bit( DS1410_Reset , &c )) ) return ret ;
    switch(c) {
    case 0:
        syslog(LOG_INFO,"1-wire bus short circuit.\n") ;
        /* fall through */
    case 0xF0:
        AnyDevices = 0 ;
        break ;
    default:
        AnyDevices = 1 ;
        ProgramAvailable = 0 ; /* from digitemp docs */
        UT_delay(5); //delay for DS1994
    }

    Version2480 = 1 ; /* dummy value */
    return 0 ;
}

/* Assymetric */
/* Send a byte to the 1410 passive adapter */
/* return 0 valid, else <0 error */
/* no matching read */
static int DS1410_write( const unsigned char * const bytes, const size_t num ) {
    int i ;
    int remain = num - (UART_FIFO_SIZE>>3) ;
    int num8 = num<<3 ;
    if ( remain>0 ) return DS1410_write(bytes,UART_FIFO_SIZE>>3) || DS1410_write(&bytes[UART_FIFO_SIZE>>3],remain) ;
    for ( i=0;i<num8;++i) combuffer[i] = UT_getbit(bytes,i)?0xFF:0xC0;
    return BUS_send_and_get(combuffer,num8,NULL,0) ;
}

/* Assymetric */
/* Read bytes from the 1410 passive adapter */
/* Time out on each byte */
/* return 0 valid, else <0 error */
/* No matching read */
static int DS1410_read( unsigned char * const byte, const int num ) {
    int i,ret ;
    int remain = num-(UART_FIFO_SIZE>>3) ;
    int num8 = num<<3 ;
    if ( remain > 0 ) {
        return DS1410_read( byte , UART_FIFO_SIZE>>3 ) || DS1410_read( &byte[UART_FIFO_SIZE>>3],remain) ;
    }
    if ( (ret=BUS_send_and_get(NULL,0,combuffer,num8)) ) return ret ;
    for ( i=0 ; i<num8 ; ++i ) UT_setbit(byte,i,combuffer[i]&0x01) ;
    return 0 ;
}

/* Symmetric */
/* send bits -- read bits */
/* Actually uses bit zero of each byte */
int DS1410_sendback_bits( const unsigned char * const outbits , unsigned char * const inbits , const int length ) {
    int ret ;
    int i ;
    if ( length > UART_FIFO_SIZE ) return DS1410_sendback_bits(outbits,inbits,UART_FIFO_SIZE)||DS1410_sendback_bits(&outbits[UART_FIFO_SIZE],&inbits[UART_FIFO_SIZE],length-UART_FIFO_SIZE) ;
    for ( i=0 ; i<length ; ++i ) combuffer[i] = outbits[i] ? 0xFF : 0xC0 ;
    if ( (ret= BUS_send_and_get(combuffer,length,inbits,length)) ) return ret ;
    for ( i=0 ; i<length ; ++i ) inbits[i] &= 0x01 ;
    return 0 ;
}

/* Symmetric */
/* send a bit -- read a bit */
static int DS1410_sendback_data( const unsigned char * data, unsigned char * const resp , const int len ) {
     if ( ioctl( devfd, PPCLAIM ) ) {
printf("PPCLAIM\n");
        return -EFAULT ;
     } else {
        int bits = len<<3 ;
        int i, ret = 0 ;
        for ( i=0 ; i<bits ; ++i ) {
            unsigned char out ;
            if ( (ret=DS1410_send_bit( UT_getbit( data, i ), &out )) ) break ;
            UT_setbit( resp, i , out ) ;
        }
        ioctl( devfd , PPRELEASE ) ;
        return ret ;
    }
}

void teststatus( void ) {
    unsigned int st ;
    ioctl( devfd, PPRSTATUS, &st ) ;
    printf("Status %.2X PIN11=%.2X PIN13=%.2X\n",st,st&PIN11,st&PIN13);
}


static int DS1410_status_loop( const unsigned int flag, const int * const waits ) {
/*
    int * sl ;
    unsigned char st ;
    for ( sl = waits ; *sl ; ++sl ) {
        usleep(*sl) ;
        ioctl( devfd, PPRSTATUS, &st ) ;
        if ( st & 0x90 == flag ) return 0 ;
    }
    return 1 ;
*/
    int i ;
    unsigned char st ;
printf("LOOP flag=%.2X ",flag);
    for ( i=0 ; i<10 ; ++i ) {
        usleep(100) ;
        ioctl( devfd, PPRSTATUS, &st ) ;
printf(" %.2X",st) ;
        if ( (st & 0x90) == flag ) {
printf("LOOP flag=%.2X reps=%d\n",flag,i);
            return 0 ;
        }
    }
printf("BAD LOOP flag=%.2X reps=%d\n",flag,i);
    return 1 ;
}

/* Symmetric */
/* send a bit -- read a bit */
static int DS1410_send_bit( const unsigned char data, unsigned char * const resp ) {
    unsigned char save, res ;
    int w=0;
    int r=1;
    int ret ;
    int i ;
    unsigned char ff = 0xFF ;
    unsigned char fe = 0xFE ;
    unsigned char ct, rsp ;
    struct ppdev_frob_struct c[] = {
        {DS1410_ENI,0x00},
        {DS1410_ENI,DS1410_ENI},
    } ;

    if ( ioctl( devfd, PPCLAIM ) ) {
teststatus();
printf("PPCLAIM\n");
       return -EFAULT ;
    }
teststatus();

    ret =ioctl( devfd, PPRDATA,    &save ) ;
printf("DATA(read) ret=%d save=%.2X\n",ret,save);
teststatus();

    ret = ioctl( devfd, PPWDATA,    &data ) ;
printf("DATA(write) ret=%d data=%.2X\n",ret,data);
teststatus();

    ret = ioctl( devfd, PPRCONTROL, &ct ) ;
printf("CONTROL(read)=%d ctl=%.2X\n",ret,ct) ;
teststatus();

    ct &= ~(PIN14) ;
    ret = ioctl( devfd, PPWCONTROL, &ct ) ;
printf("CONTROL(write)=%d ctl=%.2X\n",ret,ct) ;
teststatus();

    ct |= PIN14 ;
    ret = ioctl( devfd, PPWCONTROL, &ct ) ;
printf("CONTROL(write)=%d ctl=%.2X\n",ret,ct) ;
teststatus();

    i=0 ;
    do {
        if ( (i++%100) == 0 ) teststatus();
        usleep(1) ;
        ioctl( devfd, PPRSTATUS, &rsp ) ;
    } while( !(rsp&PIN11) && (rsp&PIN13) && i<1000 ) ;
printf("LOOP i=%d\n",i) ;
teststatus();

    ret = ioctl( devfd, PPWDATA,    &ff   ) ;
printf("DATA(write) ret=%d data=%.2X\n",ret,ff);
teststatus();

    i=0 ;
    do {
        if ( (i++%100) == 0 ) teststatus();
        usleep(1) ;
        ioctl( devfd, PPRSTATUS, &rsp ) ;
    } while( (rsp&PIN11) && !(rsp&PIN13) && i<1000 ) ;
printf("LOOP i=%d\n",i) ;
teststatus();

    ret = ioctl( devfd, PPWDATA,    &fe   ) ;
printf("DATA(write) ret=%d data=%.2X\n",ret,fe);
teststatus();

    usleep(2) ;
teststatus();

    ret =ioctl( devfd, PPRDATA,    &rsp ) ;
printf("DATA(read) ret=%d resp=%.2X\n",ret,rsp);
teststatus();

    ret = ioctl( devfd, PPWDATA,    &ff   ) ;
printf("DATA(write) ret=%d data=%.2X\n",ret,ff);
teststatus();

    resp[0] = ((rsp^0x80)&0x90)?1:0 ;
printf("RESP = %.2X\n",resp[0]) ;

    ct &= ~(PIN14) ;
    ret = ioctl( devfd, PPWCONTROL, &ct ) ;
printf("CONTROL(write)=%d ctl=%.2X\n",ret,ct) ;
teststatus();

    ret = ioctl( devfd, PPWDATA,    &save   ) ;
printf("DATA(write) ret=%d data=%.2X\n",ret,save);
teststatus();

    ret = ioctl( devfd , PPRELEASE ) ;
printf("RELEASE ret=%d\n",ret);
    return ret ;
}

/* Symetric */
/* gets specified number of bits, one per return byte */
static int DS1410_read_bits( unsigned char * const bits , const int length ) {
    int i, ret ;
    if ( length > UART_FIFO_SIZE ) return DS1410_read_bits(bits,UART_FIFO_SIZE)||DS1410_read_bits(&bits[UART_FIFO_SIZE],length-UART_FIFO_SIZE) ;
    memset( combuffer,0xFF,(size_t)length) ;
    if ( (ret=BUS_send_and_get(combuffer,length,bits,length)) ) return ret ;
    for ( i=0;i<length;++i) bits[i]&=0x01 ;
    return 0 ;
}

void DS1410_setroutines( struct interface_routines * const f ) {
    f->write = DS1410_write ;
    f->read  = DS1410_read ;
    f->reset = DS1410_reset ;
    f->next_both = DS1410_next_both ;
    f->level = DS1410_level ;
    f->PowerByte = DS1410_PowerByte ;
    f->ProgramPulse = DS1410_ProgramPulse ;
    f->sendback_data = DS1410_sendback_data ;
}
