#include <stdint.h>
#include <string.h>
#include <util/delay.h>
#include "main.h"
#include "led-spiout.h"

/* SPI LED driver to send data to APA102 LEDs
 *
 * Preformatted data is sent to the micro nd then
 * passed invia led_update_buffer(). The device
 * continuously outputs SPI data, refilling the SPI
 * output buffer from the SPI transfer complete interrupt.
 *
 * Data is double buffered (see notes below), however an update can
 * occur during any byte in the chain (we just guarantee it won't
 * happen mid-LED). The LED refresh rate is high enough that this
 * shouldn't matter.
 */

static volatile uint8_t led_buffer[LED_BUFSZ];
static volatile enum {
  START_FRAME,
  DATA,
  END_FRAME
} led_phase;
static volatile uint16_t index; /* next byte to transmit */

/* Update the transmit buffer with LED_BUFSZ bytes of new data */
void led_update_buffer(const uint8_t *buf)
{
  /* Double-buffering here is wasteful, but there isn't enough RAM on
     ATTiny48 to single buffer 32 LEDs and have everything else work
     unmodified. However there's enough RAM on ATTiny88 to double
     buffer 32 LEDs! And double buffering is simpler, less likely to
     flicker. */
  DISABLE_INTERRUPTS({
      memcpy((uint8_t *)led_buffer, buf, LED_BUFSZ);
  });
}

void led_init()
{
  /* Set MOSI, SCK, SS all to outputs */
  DDRB = _BV(5)|_BV(3)|_BV(2);
  PORTB &= ~(_BV(5)|_BV(3)|_BV(2));

  /* Enable SPI master, MSB first, fOSC/4 speed (2MHz)

     NB 500kHz into 32 LEDs = ~460Hz updates. Can probably slow the
     clock down further in order to reduce the SPI interrupt overhead.
   */
  SPCR = _BV(SPE)|_BV(MSTR)|_BV(SPIE);

  /* Start transmitting the first byte of the start frame */
  led_phase = START_FRAME;
  SPDR = 0x0;
  _delay_ms(10);
  index = 1;
}

/* Each time a byte finishes transmitting, queue the next one */
ISR(SPI_STC_vect)
{
  uint16_t next_index = index + 1;
  switch(led_phase) {
    case START_FRAME:
      SPDR = 0;
      if(next_index == 4) {
	led_phase = DATA;
	next_index = 0;
      }
      break;
  case DATA:
    SPDR = led_buffer[index];
    if(next_index == LED_BUFSZ) {
      led_phase = END_FRAME;
      next_index = 0;
    }
    break;
  case END_FRAME:
    SPDR = 0xFF;
    if(next_index == 4) { /* NB: increase this number if ever >64 LEDs */
      led_phase = START_FRAME;
      next_index = 0;
    }
    break;
  }
  index = next_index;
}