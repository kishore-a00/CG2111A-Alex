#include "serialize.h"
#include "packet.h"
#include "constants.h"
#include <math.h>
#include <stdarg.h>

#define PI 3.141592654

#define ALEX_LENGTH 17
#define ALEX_BREADTH 10

float alexDiagnal = sqrt(ALEX_LENGTH * ALEX_LENGTH + ALEX_BREADTH * ALEX_BREADTH);
float alexCirc = PI * alexDiagnal /2;

typedef enum {
  STOP = 0,
  FORWARD = 1,
  BACKWARD = 2,
  LEFT = 3,
  RIGHT = 4
} TDirection;

volatile TDirection dir = STOP;

/*
   Alex's configuration constants
*/

// Number of ticks per revolution from the wheel encoder.
#define COUNTS_PER_REV      220

// Wheel circumference in cm.
// We will use this to calculate forward/backward distance traveled
// by taking revs * WHEEL_CIRC
#define WHEEL_CIRC          21

// Motor control pins. You need to adjust these till
// Alex moves in the correct direction
#define LF  (1 << 5) //PD5 - OC0B
#define LR  (1 << 6) //PD6 - OC0A
#define RF  (1 << 2) //PB2 - OC1B
#define RR  (1 << 1) //PB1 - OC1A

/*
      Alex's State Variables
*/

// Store the ticks from Alex's left and
// right encoders.
volatile unsigned long leftForwardTicks;
volatile unsigned long leftReverseTicks;
volatile unsigned long rightForwardTicks;
volatile unsigned long rightReverseTicks;

volatile unsigned long leftForwardTicksTurns;
volatile unsigned long leftReverseTicksTurns;
volatile unsigned long rightForwardTicksTurns;
volatile unsigned long rightReverseTicksTurns;


// Store the revolutions on Alex's left
// and right wheels
volatile unsigned long leftRevs;
volatile unsigned long rightRevs;

// Forward and backward distance traveled
volatile unsigned long forwardDist;
volatile unsigned long reverseDist;

unsigned long deltaDist;
unsigned long newDist;

unsigned long deltaTicks;
unsigned long targetTicks;


/*

   Alex Communication Routines.

*/

TResult readPacket(TPacket *packet)
{
  // Reads in data from the serial port and
  // deserializes it.Returns deserialized
  // data in "packet".

  char buffer[PACKET_SIZE];
  int len;

  len = readSerial(buffer);

  if (len == 0)
    return PACKET_INCOMPLETE;
  else
    return deserialize(buffer, len, packet);

}

void sendStatus()
{
  // Implement code to send back a packet containing key
  // information like leftTicks, rightTicks, leftRevs, rightRevs
  // forwardDist and reverseDist
  // Use the params array to store this information, and set the
  // packetType and command files accordingly, then use sendResponse
  // to send out the packet. See sendMessage on how to use sendResponse.
  //
  TPacket statusPacket;
  statusPacket.packetType = PACKET_TYPE_RESPONSE;
  statusPacket.command = RESP_STATUS;
  statusPacket.params[0] = leftForwardTicks;
  statusPacket.params[1] = rightForwardTicks;
  statusPacket.params[2] = leftReverseTicks;
  statusPacket.params[3] = rightReverseTicks;
  statusPacket.params[4] = leftForwardTicksTurns;
  statusPacket.params[5] = rightForwardTicksTurns;
  statusPacket.params[6] = leftReverseTicksTurns;
  statusPacket.params[7] = rightReverseTicksTurns;
  statusPacket.params[8] = forwardDist;
  statusPacket.params[9] = reverseDist;
  sendResponse(&statusPacket);

}

void sendMessage(const char *message)
{
  // Sends text messages back to the Pi. Useful
  // for debugging.

  TPacket messagePacket;
  messagePacket.packetType = PACKET_TYPE_MESSAGE;
  strncpy(messagePacket.data, message, MAX_STR_LEN);
  sendResponse(&messagePacket);
}

void dbprintf(char *format, ...) {
  va_list args;
  char buffer[128];

  va_start(args, format);
  vsprintf(buffer, format, args);
  sendMessage(buffer);
}

void sendBadPacket()
{
  // Tell the Pi that it sent us a packet with a bad
  // magic number.

  TPacket badPacket;
  badPacket.packetType = PACKET_TYPE_ERROR;
  badPacket.command = RESP_BAD_PACKET;
  sendResponse(&badPacket);

}

void sendBadChecksum()
{
  // Tell the Pi that it sent us a packet with a bad
  // checksum.

  TPacket badChecksum;
  badChecksum.packetType = PACKET_TYPE_ERROR;
  badChecksum.command = RESP_BAD_CHECKSUM;
  sendResponse(&badChecksum);
}

void sendBadCommand()
{
  // Tell the Pi that we don't understand its
  // command sent to us.

  TPacket badCommand;
  badCommand.packetType = PACKET_TYPE_ERROR;
  badCommand.command = RESP_BAD_COMMAND;
  sendResponse(&badCommand);

}

void sendBadResponse()
{
  TPacket badResponse;
  badResponse.packetType = PACKET_TYPE_ERROR;
  badResponse.command = RESP_BAD_RESPONSE;
  sendResponse(&badResponse);
}

void sendOK()
{
  TPacket okPacket;
  okPacket.packetType = PACKET_TYPE_RESPONSE;
  okPacket.command = RESP_OK;
  sendResponse(&okPacket);
}

void sendResponse(TPacket *packet)
{
  // Takes a packet, serializes it then sends it out
  // over the serial port.
  char buffer[PACKET_SIZE];
  int len;

  len = serialize(buffer, packet, sizeof(TPacket));
  writeSerial(buffer, len);
}


/*
   Setup and start codes for external interrupts and
   pullup resistors.

*/
// Enable pull up resistors on pins 2 and 3
void enablePullups()
{
  // Use bare-metal to enable the pull-up resistors on pins
  // 2 and 3. These are pins PD2 and PD3 respectively.
  // We set bits 2 and 3 in DDRD to 0 to make them inputs.

  DDRD &= ~0b00001100;
  PORTD |= 0b1100;
}

// Functions to be called by INT0 and INT1 ISRs.
void leftISR()
{
  switch (dir) {
    case FORWARD:
      leftForwardTicks++;
      forwardDist = (unsigned long) ((float) leftForwardTicks / COUNTS_PER_REV * WHEEL_CIRC);
      break;
    case BACKWARD:
      leftReverseTicks++;
      reverseDist = (unsigned long) ((float) leftReverseTicks / COUNTS_PER_REV * WHEEL_CIRC);
      break;
    case LEFT:
      leftReverseTicksTurns++;
      break;
    case RIGHT:
      leftForwardTicksTurns++;
      break;
  }
}

void rightISR()
{
  switch (dir) {
    case FORWARD:
      rightForwardTicks++;
      break;
    case BACKWARD:
      rightReverseTicks++;
      break;
    case LEFT:
      rightForwardTicksTurns++;
      break;
    case RIGHT:
      rightReverseTicksTurns++;
      break;
  }
}


// Set up the external interrupt pins INT0 and INT1
// for falling edge triggered. Use bare-metal.
void setupEINT()
{
  // Use bare-metal to configure pins 2 and 3 to be
  // falling edge triggered. Remember to enable
  // the INT0 and INT1 interrupts.
  EICRA |= 0b00001010;
  EIMSK |= 0b00000011;
}

// Implement the external interrupt ISRs below.
// INT0 ISR should call leftISR while INT1 ISR
// should call rightISR.

ISR(INT0_vect) {
  leftISR();
}

ISR(INT1_vect) {
  rightISR();
}

/*
   Setup and start codes for serial communications

*/
// Set up the serial connection.
void setupSerial() {
  // Set up USART0 for 9600 bps, 8N1 configuration
  unsigned long baudRate = 9600;
  unsigned int b;
  b = (unsigned int) round( F_CPU / (16.0*baudRate) ) - 1;
  UBRR0H = (unsigned char) (b >> 8);
  UBRR0L = (unsigned char) b;

  //Asynchronous mode: [7:6]=0b00
  //No parity: [5:4]=0b00
  //1 stop bit: [3]=0b0
  //8 bits: UCSZ02/UCSZ01/UCSZ00 = 0b011 --> [2:1]=0b11
  //UCSZ02 is set to 0 in UCSR0B in startSerial()
  //UCPOL0 [0]=0b0 (always set to zero)
  UCSR0C = 0b00000110;

  //Switch off double-speed mode/multiprocessor mode
  UCSR0A = 0;
}

// Start the serial connection. 
void startSerial() {
  // Polling mode: RXCIE0/TXCIE0/UDRIE0 = [7:5]=0b000
  // Enable Rx and Tx: [4:3]=0b11
  // UCSZ02: [2] = 0b0 --> to make UCSZ0[2:0]=0b011
  // RXB80/TXB80: [1:0] = 0b00 (since we are not using 9-bit data size)
  UCSR0B = 0b00011000;
}

// Read the serial port. Returns the read character in
// ch if available. Also returns TRUE if ch is valid.
// This will be replaced later with bare-metal code.
int readSerial(char *buffer) {
  int count=0;
  //wait until bit 7 (RXC0 bit in UCSR0A register)=1
  while( (UCSR0A & 0b10000000) == 0 ); 
  buffer[count++] = UDR0; //read from UDR0 register and store in buffer
  return count;
}

// Write to the serial port
void writeSerial(const char *buffer, int len) {
  //wait until bit 5 (UDRE0 bit in UCSR0A register)=1
  while ( (UCSR0A & 0b00100000) == 0 ); 
  for (int i=0; i<len; i++) {
    UDR0 = buffer[i]; //write to UDR0 register
  }
}

/*
   Alex's motor drivers.
*/

void setupMotors()
{
  /* Our motor set up is:
        A1IN - Pin 5, PD5, OC0B
        A2IN - Pin 6, PD6, OC0A
        B1IN - Pin 10, PB2, OC1B
        B2In - pIN 11, PB3, OC2A
  */
  //Set PIN 5, 6, 9 and 10 as output pins
  DDRD |= (LF|LR);
  DDRB |= (RF|RR);
}

// Start the PWM for Alex's motors
void startMotors()
{
  //Setting up Timer 0 for Left motor
  TCNT0 = 0; //Initialise counter to start from 0
  TCCR0B = 0b00000011; // Set clk source to clk/64 --> CS0[2:0] = 0b011; WGM02 [3]=0b0
  
  //Setting up Timer 1 for Right motor
  TCNT1 = 0; //Initialise counter to start from 0
  TCCR1B = 0b00000011; // Set clk source to clk/64
}

// Convert percentages to PWM values
int pwmVal(float speed)
{
  if (speed < 0.0)
    speed = 0;

  if (speed > 100.0)
    speed = 100.0;

  return (int) ((speed / 100.0) * 255.0);
}

// Move Alex forward "dist" cm at speed "speed".
// "speed" is expressed as a percentage. E.g. 50 is
// move forward at half speed.
// Specifying a distance of 0 means Alex will
// continue moving forward indefinitely.
void forward(float dist, float speed)
{
  dir = FORWARD;

  int val = pwmVal(speed);

  if (dist == 0)
    deltaDist = 999999;
  else
    deltaDist = dist;

  newDist = forwardDist + deltaDist;

  // LF = Left forward pin, LR = Left reverse pin
  // RF = Right forward pin, RR = Right reverse pin
  OCR0A = 0;
  OCR0B = val;
  OCR1A = 0;
  OCR1B = val;
  left_motor_forward();
  right_motor_forward();
}

// Reverse Alex "dist" cm at speed "speed".
// "speed" is expressed as a percentage. E.g. 50 is
// reverse at half speed.
// Specifying a distance of 0 means Alex will
// continue reversing indefinitely.
void reverse(float dist, float speed)
{
  dir = BACKWARD;

  int val = pwmVal(speed);

  if (dist == 0)
    deltaDist = 999999;
  else
    deltaDist = dist;

  newDist = reverseDist + deltaDist;

  // LF = Left forward pin, LR = Left reverse pin
  // RF = Right forward pin, RR = Right reverse pin
  OCR0A = val;
  OCR0B = 0;
  OCR1A = val;
  OCR1B = 0;
  left_motor_reverse();
  right_motor_reverse();
}

unsigned long computeDeltaTicks(float ang)
{
  unsigned long ticks = (unsigned long) ((ang * alexCirc * COUNTS_PER_REV) / (360.0 * WHEEL_CIRC));
  return ticks;
}

// Turn Alex left "ang" degrees at speed "speed".
// "speed" is expressed as a percentage. E.g. 50 is
// turn left at half speed.
// Specifying an angle of 0 degrees will cause Alex to
// turn left indefinitely.
void left(float ang, float speed)
{
  if (ang == 0)
    deltaTicks = 999999;
  else
    deltaTicks = computeDeltaTicks(ang);

  targetTicks = leftReverseTicksTurns + deltaTicks;

  dir = LEFT;

  int val = pwmVal(speed);

  // To turn left we reverse the left wheel and move
  // the right wheel forward.
  OCR0A = val;
  OCR0B = 0;
  OCR1A = 0;
  OCR1B = val;
  left_motor_reverse();
  right_motor_forward();
}

// Turn Alex right "ang" degrees at speed "speed".
// "speed" is expressed as a percentage. E.g. 50 is
// turn left at half speed.
// Specifying an angle of 0 degrees will cause Alex to
// turn right indefinitely.
void right(float ang, float speed)
{
  if (ang == 0)
    deltaTicks = 999999;
  else
    deltaTicks = computeDeltaTicks(ang);

  targetTicks = rightReverseTicksTurns + deltaTicks;

  dir = RIGHT;

  int val = pwmVal(speed);

  // To turn right we reverse the right wheel and move
  // the left wheel forward.
  OCR0A = 0;
  OCR0B = val;
  OCR1A = val;
  OCR1B = 0;
  left_motor_forward();
  right_motor_reverse();

}

// Stop Alex
void stop()
{
  dir = STOP;

  OCR0A = 0;
  OCR0B = 0;
  OCR1A = 0;
  OCR1B = 0;
  stop_both_motors();

}

void left_motor_forward(void) {
  TCCR0A = 0b00100001; // Set COM0B to 10 and WGM to 01
}

void left_motor_reverse(void) {
  TCCR0A = 0b10000001; // Set COM0A to 10 and WGM to 01
}

void right_motor_forward(void) {
  TCCR1A = 0b00100001; // Set COM1B to 10 and WGM to 01
}

void right_motor_reverse(void) {
  TCCR1A = 0b10000001; // Set COM1A to 10 and WGM to 01
}

void stop_both_motors(void) {
  TCCR0A = 0b00000001;
  TCCR1A = 0b00000001;
}


/*
   Alex's setup and run codes

*/

// Clears all our counters
void clearCounters()
{
  leftForwardTicks = 0;
  leftReverseTicks = 0;
  rightForwardTicks = 0;
  rightReverseTicks = 0;

  leftForwardTicksTurns = 0;
  leftReverseTicksTurns = 0;
  rightForwardTicksTurns = 0;
  rightReverseTicksTurns = 0;

  forwardDist = 0;
  reverseDist = 0;
}

// Clears one particular counter
void clearOneCounter(int which)
{
  switch (which) {
    case 0: leftForwardTicks = 0;
      break;

    case 1: rightForwardTicks = 0;
      break;

    case 2: leftReverseTicks = 0;
      break;

    case 3: rightReverseTicks = 0;
      break;

    case 4: leftForwardTicksTurns = 0;
      break;

    case 5: rightForwardTicksTurns = 0;
      break;

    case 6: leftReverseTicksTurns = 0;
      break;

    case 7: rightReverseTicksTurns = 0;
      break;

    case 8: forwardDist = 0;
      break;

    case 9: reverseDist = 0;
      break;
  }
}
// Intialize Vincet's internal states

void initializeState()
{
  clearCounters();
}

void handleCommand(TPacket *command)
{
  switch (command->command)
  {
    // For movement commands, param[0] = distance, param[1] = speed.
    case COMMAND_FORWARD:
      sendOK();
      forward((float) command->params[0], (float) command->params[1]);
      break;

    case COMMAND_REVERSE:
      sendOK();
      reverse((float) command->params[0], (float) command->params[1]);
      break;

    case COMMAND_TURN_LEFT:
      sendOK();
      left((float) command->params[0], (float) command->params[1]);
      break;

    case COMMAND_TURN_RIGHT:
      sendOK();
      right((float) command->params[0], (float) command->params[1]);
      break;

    case COMMAND_STOP:
      sendOK();
      stop();
      break;

    case COMMAND_GET_STATS:
      sendStatus();
      break;

    case COMMAND_CLEAR_STATS:
      clearOneCounter((int) command->params[0]);
      break;

    default:
      sendBadCommand();
  }
}

void waitForHello()
{
  int exit = 0;

  while (!exit)
  {
    TPacket hello;
    TResult result;
    do
    {
      result = readPacket(&hello);
    } while (result == PACKET_INCOMPLETE);

    if (result == PACKET_OK)
    {
      if (hello.packetType == PACKET_TYPE_HELLO)
      {
        sendOK();
        exit = 1;
      }
      else
        sendBadResponse();
    }
    else if (result == PACKET_BAD)
    {
      sendBadPacket();
    }
    else if (result == PACKET_CHECKSUM_BAD)
      sendBadChecksum();
  } // !exit
}

void setup() {
  cli();
  setupEINT();
  setupSerial();
  startSerial();
  setupMotors();
  startMotors();
  enablePullups();
  initializeState();
  sei();
}

void handlePacket(TPacket *packet)
{
  switch (packet->packetType)
  {
    case PACKET_TYPE_COMMAND:
      handleCommand(packet);
      break;

    case PACKET_TYPE_RESPONSE:
      break;

    case PACKET_TYPE_ERROR:
      break;

    case PACKET_TYPE_MESSAGE:
      break;

    case PACKET_TYPE_HELLO:
      break;
  }
}

void loop() {

  if (deltaDist > 0)
  {
    if (dir == FORWARD) {
      if (forwardDist >= newDist) {
        deltaDist = 0;
        newDist = 0;
        stop();
      }
    }
    else if (dir == BACKWARD) {
      if (reverseDist >= newDist) {
        deltaDist = 0;
        newDist = 0;
        stop();
      }
    } else if (dir == STOP) {
      deltaDist = 0;
      newDist = 0;
      stop();
    }
  }

  if (deltaTicks > 0)
  {
    if (dir == LEFT) {
      if (leftReverseTicksTurns >= targetTicks) {
        deltaTicks = 0;
        targetTicks = 0;
        stop();
      }
    }
    else if (dir == RIGHT) {
      if (rightReverseTicksTurns >= targetTicks) {
        deltaTicks = 0;
        targetTicks = 0;
        stop();
      }
    } else if (dir == STOP) {
      deltaTicks = 0;
      targetTicks = 0;
      stop();
    }
  }

  TPacket recvPacket; // This holds commands from the Pi

  TResult result = readPacket(&recvPacket);

  if (result == PACKET_OK)
    handlePacket(&recvPacket);
  else if (result == PACKET_BAD)
  {
    sendBadPacket();
  }
  else if (result == PACKET_CHECKSUM_BAD)
  {
    sendBadChecksum();
  }
}
