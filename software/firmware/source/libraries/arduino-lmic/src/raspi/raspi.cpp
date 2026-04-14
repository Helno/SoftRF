// raspi.cpp
//
// Routines for implementing Arduino-LIMC on Raspberry Pi
// using BCM2835 library for GPIO and spidev for SPI transfers
// This code has been grabbed from excellent RadioHead Library

#ifdef RASPBERRY_PI
#include <sys/time.h>
#include <time.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include "raspi.h"

//Initialize the values for sanity
static uint64_t epochMilli ;
static uint64_t epochMicro ;

/* File descriptor for /dev/spidev0.0 — opened before bcm2835_init() so the
 * kernel retains ownership of the CE0/CE1 GPIO output state.  SPI_NO_CS mode
 * tells the kernel driver never to touch the chip-select pin; we drive NSS
 * manually with bcm2835_gpio_write() (no fsel call — GPIO8 stays a
 * kernel-managed OUTPUT). */
static int spidev_fd = -1;

SPIClass::SPIClass(uint8_t spi_bus)
    :_spi_num(spi_bus)
{}

void SPIClass::begin() {
  if (_spi_num == SPI_AUX) {
    initialiseEpoch();
    if (!bcm2835_aux_spi_begin()) {
      printf("bcm2835_aux_spi_begin() failed. Are you running as root??\n");
    }
    return;
  }

  /* Open spidev BEFORE initialiseEpoch() so the kernel fd is established
   * before any bcm2835 GPIO calls.  SPI_NO_CS tells the kernel never to
   * pulse CE0/CE1 during transfers; NSS is driven manually via
   * bcm2835_gpio_write() without any fsel call. */
  spidev_fd = open("/dev/spidev0.0", O_RDWR);
  if (spidev_fd < 0) {
    perror("open /dev/spidev0.0");
    return;
  }

  uint8_t mode  = SPI_MODE_0 | SPI_NO_CS;
  uint8_t bits  = 8;
  uint32_t speed = 2000000;
  if (ioctl(spidev_fd, SPI_IOC_WR_MODE,          &mode)  < 0 ||
      ioctl(spidev_fd, SPI_IOC_WR_BITS_PER_WORD,  &bits)  < 0 ||
      ioctl(spidev_fd, SPI_IOC_WR_MAX_SPEED_HZ,  &speed) < 0) {
    perror("spidev ioctl setup");
  }
  uint8_t actual_mode = 0;
  ioctl(spidev_fd, SPI_IOC_RD_MODE, &actual_mode);
  fprintf(stderr, "DBG SPI.begin: fd=%d mode=0x%02x(want 0x%02x)\n",
          spidev_fd, actual_mode, mode); fflush(stderr);

  initialiseEpoch();
}

void SPIClass::end() {
  if (_spi_num == SPI_AUX) {
    bcm2835_aux_spi_end();
    return;
  }
  if (spidev_fd >= 0) {
    close(spidev_fd);
    spidev_fd = -1;
  }
}

void SPIClass::beginTransaction(SPISettings settings) {
  if (_spi_num == SPI_AUX) {
    bcm2835_aux_spi_setClockDivider(settings.divider);
    return;
  }

  /* Update spidev speed to match requested clock divider.
   * BCM2835 core clock is 250 MHz; convert divider to Hz. */
  if (spidev_fd >= 0) {
    uint32_t speed = (settings.divider > 0) ? (250000000UL / settings.divider)
                                             : 2000000;
    ioctl(spidev_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
  }

  /* Do NOT call bcm2835_gpio_fsel(7/8, ...) — GPIO8 (CE0) must stay in
   * kernel-managed OUTPUT mode.  Calling fsel corrupts the kernel GPIO state
   * and breaks subsequent spidev transfers.
   *
   * Only configure the NSS pin if it is NOT GPIO7 or GPIO8 (which are already
   * managed by the kernel spi_bcm2835 driver). */
  uint8_t cs = lmic_pins.nss;
  if (cs != 7 && cs != 8 && cs != LMIC_UNUSED_PIN) {
    bcm2835_gpio_fsel(  cs, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_write( cs, HIGH);
  }
}

void SPIClass::endTransaction() {
}

byte SPIClass::transfer(byte _data) {
  if (_spi_num == SPI_AUX) {
    byte data = _data;
    bcm2835_aux_spi_transfern((char *)&data, 1);
    return data;
  }

  if (spidev_fd < 0) {
    fprintf(stderr, "DBG transfer: spidev_fd<0!\n"); fflush(stderr);
    return 0;
  }

  byte rx = 0;
  struct spi_ioc_transfer xfer;
  memset(&xfer, 0, sizeof(xfer));
  xfer.tx_buf       = (uint64_t)(uintptr_t)&_data;
  xfer.rx_buf       = (uint64_t)(uintptr_t)&rx;
  xfer.len          = 1;
  xfer.bits_per_word = 8;
  uint8_t nss_lev = bcm2835_gpio_lev(8);
  int ret = ioctl(spidev_fd, SPI_IOC_MESSAGE(1), &xfer);
  fprintf(stderr, "DBG transfer: fd=%d nss=%d tx=0x%02x rx=0x%02x ret=%d\n",
          spidev_fd, nss_lev, _data, rx, ret); fflush(stderr);
  return rx;
}

SPIClass SPI0(SPI_PRI);
SPIClass SPI1(SPI_AUX);

/* I2C is not implemented yet */
TwoWire::TwoWire()
{}

void TwoWire::begin() {
}

void TwoWire::setClock(uint32_t clock) {
}

void TwoWire::beginTransmission(uint8_t byte) {
}

uint8_t TwoWire::endTransmission() {
  return 0;
}

size_t TwoWire::write(uint8_t byte) {
}

TwoWire Wire;
 
void pinMode(unsigned char pin, unsigned char mode) {
  if (pin == LMIC_UNUSED_PIN) {
    return;
  }
  /* GPIO7 (CE1) and GPIO8 (CE0) are managed by the kernel spi_bcm2835 driver
   * as OUTPUT HIGH.  Calling bcm2835_gpio_fsel on them via /dev/mem writes
   * corrupts the kernel GPIO state and breaks subsequent spidev transfers.
   * They are already OUTPUT; no configuration is needed. */
  if (pin == 7 || pin == 8) {
    return;
  }
  if (mode == OUTPUT) {
    bcm2835_gpio_fsel(pin,BCM2835_GPIO_FSEL_OUTP);
  } else {
    bcm2835_gpio_fsel(pin,BCM2835_GPIO_FSEL_INPT);
  }
}

void digitalWrite(unsigned char pin, unsigned char value) {
  if (pin == LMIC_UNUSED_PIN) {
    return;
  }
  fprintf(stderr, "DBG: digitalWrite(%d, %d)\n", pin, value);
  fflush(stderr);
  bcm2835_gpio_write(pin, value);
}

unsigned char digitalRead(unsigned char pin) {
  if (pin == LMIC_UNUSED_PIN) {
    return 0;
  }
  return bcm2835_gpio_lev(pin);
}

//Initialize a timestamp for millis/micros calculation
// Grabbed from WiringPi
void initialiseEpoch() {
  struct timeval tv ;
  gettimeofday (&tv, NULL) ;
  epochMilli = (uint64_t)tv.tv_sec * (uint64_t)1000    + (uint64_t)(tv.tv_usec / 1000) ;
  epochMicro = (uint64_t)tv.tv_sec * (uint64_t)1000000 + (uint64_t)(tv.tv_usec) ;
  pinMode(lmic_pins.nss, OUTPUT);
  digitalWrite(lmic_pins.nss, HIGH);
}

unsigned int millis() {
  struct timeval tv ;
  uint64_t now ;
  gettimeofday (&tv, NULL) ;
  now  = (uint64_t)tv.tv_sec * (uint64_t)1000 + (uint64_t)(tv.tv_usec / 1000) ;
  return (uint32_t)(now - epochMilli) ;
}

unsigned int micros() {
  struct timeval tv ;
  uint64_t now ;
  gettimeofday (&tv, NULL) ;
  now  = (uint64_t)tv.tv_sec * (uint64_t)1000000 + (uint64_t)tv.tv_usec ;
  return (uint32_t)(now - epochMicro) ;
}

char * getSystemTime(char * time_buff, int len) {
time_t t;
struct tm* tm_info;

t = time(NULL); 
tm_info = localtime(&t);
if (tm_info) {
if (strftime(time_buff, len, "%H:%M:%S", tm_info)) {
} else {
strncpy(time_buff, "strftime() ERR", len);
}
} else {
strncpy(time_buff, "localtime() ERR", len);
}
return time_buff;
}

void printConfig(const uint8_t led) {
printf( "RFM95 device configuration\n" );
if (lmic_pins.nss ==LMIC_UNUSED_PIN ) {
printf( "!! CS pin is not defined !!\n" );
} else {
printf( "CS=GPIO%d", lmic_pins.nss );
}

printf( " RST=" );
if (lmic_pins.rst==LMIC_UNUSED_PIN ) {
printf( "Unused" );
} else {
printf( "GPIO%d", lmic_pins.rst );
}

printf( " LED=" );
if ( led==LMIC_UNUSED_PIN ) {
printf( "Unused" );
} else {
printf( "GPIO%d", led );
}

// DIO 
for (uint8_t i=0; i<3 ; i++) {
printf( " DIO%d=", i );
if (lmic_pins.dio[i]==LMIC_UNUSED_PIN ) {
printf( "Unused" );
} else {
printf( "GPIO%d", lmic_pins.dio[i] );
}
}
printf( "\n" );
}

// Display a Key
// =============
void printKey(const char * name, const uint8_t * key, uint8_t len, bool lsb) 
{
  uint8_t start=lsb?len:0;
  uint8_t end = lsb?0:len;
  const uint8_t * p ;

  printf("%s : ", name);
  for (uint8_t i=0; i<len ; i++) {
    p = lsb ? key+len-i-1 : key+i;
    printf("%02X", *p);
  }
  printf("\n");
}

// Display OTAA Keys
// =================
void printKeys(void) 
{
// LMIC may not have used callback to fill 
// all EUI buffer so we do it to a temp
// buffer to be able to display them
uint8_t buf[32];
os_getDevEui((u1_t*) buf);
printKey("DevEUI", buf, 8, true);
os_getArtEui((u1_t*) buf);
printKey("AppEUI", buf, 8, true);
os_getDevKey((u1_t*) buf);
printKey("AppKey", buf, 16, false);
}



bool getDevEuiFromMac(uint8_t * pdeveui) {
  struct ifaddrs *ifaddr=NULL;
  struct ifaddrs *ifa = NULL;
  int family = 0;
  int i = 0;
  bool gotit = false;

  // get linked list of the network interfaces
  if (getifaddrs(&ifaddr) == -1) {
    return false;
  } else {
    // Loop thru interfaces list
    for ( ifa=ifaddr; ifa!=NULL; ifa=ifa->ifa_next) {
      // Ethernet
      if ( (ifa->ifa_addr) && (ifa->ifa_addr->sa_family==AF_PACKET) ) {
        // Not loopback interface
        if (! (ifa->ifa_flags & IFF_LOOPBACK)) {
          char fname[128];
          int fd;
          int up=0;
          struct sockaddr_ll *s = (struct sockaddr_ll*)ifa->ifa_addr;
         
          // Get interface status 
          // Interface can be up with no cable connected and to be sure
          // It's up, active and connected we need to get operstate
          // 
          // if up + cable    if up + NO cable    if down + cable
          // =============    ==========          ==================
          // carrier:1        carrier:0           carrier:Invalid
          // dormant:0        dormant:0           dormant:Invalid
          // operstate:up     operstate:down      operstate :own
          sprintf(fname, "/sys/class/net/%s/operstate", ifa->ifa_name);
          if ( (fd = open( fname, O_RDONLY)) > 0 ){
            char buf[2];
            if ( read(fd, buf, 2) > 0 ) {
              // only first active interface "up"
              if ( buf[0]=='u' && buf[1]=='p' ) {
                uint8_t * p = pdeveui;
                // deveui is LSB to we reverse it so TTN display 
                // will remain the same as MAC address
                // MAC is 6 bytes, devEUI 8, set first 2 ones 
                // with an arbitrary value
                *p++ = 0x00;
                *p++ = 0x04;
                // Then next 6 bytes are mac address still reversed
                for ( i=0; i<6 ; i++) {
                  *p++ = s->sll_addr[5-i];
                }
                
                gotit = true;
                close(fd);
                break;
              } 
            }
            close(fd);
          } 
        }
      }
    }
    // Free our Linked list
    freeifaddrs(ifaddr);
  }

  // just in case of error put deveui to 0102030405060708
  if (!gotit) {
    for (i=1; i<=8; i++) {
      *pdeveui++=i;
    }
  }
  return gotit;
}

void SerialSimulator::begin(int baud) {
  // No implementation neccesary - Serial emulation on Linux = standard console
  // Initialize a timestamp for millis calculation - we do this here as well in case SPI
  // isn't used for some reason
  initialiseEpoch();
}

size_t SerialSimulator::println(void) {
  fprintf(stdout, "\n");
}

size_t SerialSimulator::println(const char* s) {
  fprintf( stdout, "%s\n",s);
}

size_t SerialSimulator::print(String s) {
  fprintf( stdout, "%s",s.c_str());
}

size_t SerialSimulator::println(String s) {
  fprintf( stdout, "%s\n",s.c_str());
}

size_t SerialSimulator::print(const char* s) {
  fprintf( stdout, "%s",s);
}

size_t SerialSimulator::println(short signed int n) {
  fprintf(stdout, "%d\n", n);
}

size_t SerialSimulator::println(bool b) {
  return println(b ? "1" : "0");
}

size_t SerialSimulator::println(unsigned int n) {
  return println((unsigned long)n);
}

#include <stdarg.h>
#include <stdio.h>
size_t SerialSimulator::printf(const char* fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  return print(buf);
}

size_t SerialSimulator::println(short unsigned int n) {
  fprintf(stdout, "%u\n", n);
}

size_t SerialSimulator::print(ostime_t n) {
  fprintf(stdout, "%d", n);
}

size_t SerialSimulator::print(unsigned long n) {
  fprintf(stdout, "%lu", n);
}

size_t SerialSimulator::println(unsigned long n) {
  fprintf(stdout, "%lu\n", n);
}

size_t SerialSimulator::print(unsigned int n, int base) {
  if (base == DEC)
    fprintf(stdout, "%d", n);
  else if (base == HEX)
    fprintf(stdout, "%02x", n);
  else if (base == OCT)
    fprintf(stdout, "%o", n);
  // TODO: BIN
}

size_t SerialSimulator::print(char ch) {
  fprintf(stdout, "%c", ch);
}

size_t SerialSimulator::println(char ch) {
  fprintf(stdout, "%c\n", ch);
}

size_t SerialSimulator::println(int8_t n) {
  fprintf(stdout, "%d\n", n);
}

size_t SerialSimulator::print(unsigned char ch, int base) {
  return print((unsigned int)ch, base);
}

size_t SerialSimulator::println(unsigned char ch, int base) {
  print((unsigned int)ch, base);
  fprintf( stdout, "\n");
}

size_t SerialSimulator::write(char ch) {
  fprintf( stdout, "%c", ch);
}

size_t SerialSimulator::write(unsigned char* s, size_t len) {
  for (int i=0; i<len; i++) {
    fprintf(stdout, "%c", s[i]);
  }
}

size_t SerialSimulator::write(const char* s) {
  for (int i=0; i<strlen(s); i++) {
    fprintf(stdout, "%c", s[i]);
  }
}

size_t SerialSimulator::available(void) {
  return 0;
}

size_t SerialSimulator::read(void) {
  return -1;
}

void SerialSimulator::flush(void) {
  fflush(stdout);
}

void randomSeed(unsigned long seed)
{
  if (seed != 0) {
    srandom(seed);
  }
}

long random(long howbig)
{
  if (howbig == 0) {
    return 0;
  }
  return random() % howbig;
}

long random(long howsmall, long howbig)
{
  if (howsmall >= howbig) {
    return howsmall;
  }
  long diff = howbig - howsmall;
  return random(diff) + howsmall;
}

#endif // RASPBERRY_PI

size_t SerialSimulator::write(const uint8_t* buf, size_t size) {
  return write((uint8_t*)buf, size);
}
