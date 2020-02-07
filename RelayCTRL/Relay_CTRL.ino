// Created by Albert Kravcov
// SmartAudio code is adapted from Lukas Blocher https://github.com/NightHawk32/SmartAudio-testing

#include <HardwareSerial.h>
#include <EEPROM.h>
#include <Servo.h>
#include "U8glib.h" //Install U8glib Library to be able to compile
#include "bitmaps.h"

Servo myservo;  // create servo object to control a servo D9

U8GLIB_SSD1306_128X32 u8g(U8G_I2C_OPT_NONE);  // I2C / TWI
//U8GLIB_SSD1306_128X64 u8g(U8G_I2C_OPT_DEV_0 | U8G_I2C_OPT_NO_ACK | U8G_I2C_OPT_FAST);  // Fast I2C / TWI

#define peraypwrADDR    1       // EEPROM Adress
#define R1              27000   // Resistor1 27k
#define R2              4700    // Resistor2 4.7k
#define Voltagedetect   3.24    // Min. voltage for cell detection
#define bt_ct           15      //Center Button
#define bt_le           14      //Left Button
#define bt_ri           16      //Right Button
#define vbat_pin        A0      //vsens pin
#define ir_pin          A7      //IR receiver pin
#define pwm_contr1      8       //4066 c1 
#define pwm_contr2      9       //4066 c2
#define pwm_pin         10      //Servo signal pin
#define ctrl_pin        7       //Mosfet controll - VTX power control
#define BZ_pin          5       //Optional Buzzer

int act_freq;
byte act_band = 0;
byte act_ch = 1;
byte cur_pos = 0;
byte vsens_value = 15;
byte SA_available = 0;
byte sa_update = 0;
byte parking_step = 0;
int ir_value;
byte pressedbut = 0;
byte i_butt = 0;
int vsens;
float voltage;
float cellvoltage;
byte celldetect = 0;
byte menuactive = 0;
long previousMillis = 0;
long buzzermillis = 0;
byte relaypower = 0;
byte alarmstate;


//SA SETUP -------------------------------------------
uint8_t buff[25];
uint8_t rx_len = 0;
uint8_t zeroes = 0;
int incomingByte = 0;


#define SA_GET_SETTINGS 0x01
#define SA_GET_SETTINGS_V2 0x09
#define SA_SET_POWER 0x02
#define SA_SET_CHANNEL 0x03
#define SA_SET_FREQUENCY 0x04
#define SA_SET_MODE 0x05

#define SA_POWER_25MW 0
#define SA_POWER_50MW 1
#define SA_POWER_200MW 2
#define SA_POWER_800MW 3

enum SMARTAUDIO_VERSION {
  NONE,
  SA_V1,
  SA_V2
};

static const uint8_t V1_power_lookup[] = {
  7,
  16,
  25,
  40
};

typedef struct {
  SMARTAUDIO_VERSION vtx_version;
  uint8_t channel;
  uint8_t powerLevel;
  uint8_t mode;
  uint16_t frequency;

} UNIFY;

static UNIFY unify;

uint8_t crc8(const uint8_t *data, uint8_t len)
{
#define POLYGEN 0xd5
  uint8_t crc = 0;
  uint8_t currByte;

  for (int i = 0 ; i < len ; i++) {
    currByte = data[i];
    crc ^= currByte;
    for (int i = 0; i < 8; i++) {
      if ((crc & 0x80) != 0) {
        crc = (byte)((crc << 1) ^ POLYGEN);
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

static void sa_tx_packet(uint8_t cmd, uint32_t value) {
  //here: length --> only payload, without CRC
  //here: CRC --> calculated for complete packet 0xAA ... payload
  uint8_t buff[10];
  uint8_t packetLength = 0;
  buff[0] = 0x00;
  buff[1] = 0xAA; //sync
  buff[2] = 0x55; //sync
  buff[3] = (cmd << 1) | 0x01; //cmd

  switch (cmd) {
    case SA_GET_SETTINGS:
      buff[4] = 0x00; //length
      buff[5] = crc8(&buff[1], 4);
      buff[6] = 0x00;
      packetLength = 7;
      break;
    case SA_SET_POWER:
      buff[4] = 0x01; //length
      buff[5] = (unify.vtx_version == SA_V1) ? V1_power_lookup[value] : value;
      buff[6] = crc8(&buff[1], 5);
      buff[7] = 0x00;
      packetLength = 8;
      break;
    case SA_SET_CHANNEL:
      buff[4] = 0x01; //length
      buff[5] = value;
      buff[6] = crc8(&buff[1], 5);
      buff[7] = 0x00;
      packetLength = 8;
      break;
    case SA_SET_FREQUENCY:
      buff[4] = 0x02;
      buff[5] = (value >> 8); //high byte first
      buff[6] = value;
      buff[7] = crc8(&buff[1], 6);
      buff[8] = 0x00;
      packetLength = 9;
      break;
    case SA_SET_MODE: //supported for V2 only: UNIFY HV and newer
      if (unify.vtx_version == SA_V2) {
        //TBD --> Pit mode
        /*
          buffer[4] = 0x01; //length
          buffer[5] = value;
          buffer[6] = crc8(&buffer[1], 5);
          buffer[7] = 0x00;
          packetLength = 8;s
        */
      }
      break;
  }
  for (int i = 0; i < packetLength; i++) {
    Serial1.write(buff[i]);
  }
}

static void sa_rx_packet(uint8_t *buff, uint8_t len) {
  //verify packet
  uint8_t packetStart = 0;
  for (int i = 0; i < len - 3; i++) {
    if (buff[i] == 0xAA && buff[i + 1] == 0x55 && buff[i + 3] < len) {
      packetStart = i + 2;
      uint8_t len = buff[i + 3];
      uint8_t crcCalc = crc8(&buff[i + 2], len + 1);
      uint8_t crc = buff[i + 3 + len];

      if (crcCalc == crc) {
        Serial.println("CRC match");
        switch (buff[packetStart]) {
          case SA_GET_SETTINGS: //fall-through
          case SA_GET_SETTINGS_V2:
            Serial.println("SA_GET_SETTINGS");
            unify.vtx_version = (buff[packetStart] == SA_GET_SETTINGS) ? SA_V1 : SA_V2;
            packetStart += 2; //skip cmd and length
            unify.channel = buff[packetStart++];
            unify.powerLevel = buff[packetStart++];
            unify.mode = buff[packetStart++];
            unify.frequency = ((uint16_t)buff[packetStart++] << 8) | buff[packetStart++];
            break;
          case SA_SET_POWER:
            Serial.println("SA_SET_POWER");
            packetStart += 2;
            unify.powerLevel = buff[packetStart++];
            break;
          case SA_SET_CHANNEL:
            Serial.println("SA_SET_CHANNEL");
            packetStart += 2;
            unify.channel = buff[packetStart++];
            break;
          case SA_SET_FREQUENCY:
            Serial.println("SA_SET_FREQUENCY");
            //TBD: Pit mode Freq
            packetStart += 2;
            unify.frequency = ((uint16_t)buff[packetStart++] << 8) | buff[packetStart++];
            break;
          case SA_SET_MODE:
            //SA V2 only!
            break;
        }
        return;

      } else {
        Serial.println("CRC mismatch");
        return;
      }
    }
  }

}

//SA SETUP END -------------------------------------------




void setup(void) {
  if ( u8g.getMode() == U8G_MODE_R3G3B2 ) {
    u8g.setColorIndex(255);     // white
  }
  else if ( u8g.getMode() == U8G_MODE_GRAY2BIT ) {
    u8g.setColorIndex(3);         // max intensity
  }
  else if ( u8g.getMode() == U8G_MODE_BW ) {
    u8g.setColorIndex(1);         // pixel on
  }
  else if ( u8g.getMode() == U8G_MODE_HICOLOR ) {
    u8g.setHiColorByRGB(255, 255, 255);
  }

  analogReference(INTERNAL); //INTERNAL 2.56 or DEFAULT 3.3

  Serial.begin(9600);
  // while (!Serial);             // Leonardo: wait for serial monitor
  //Serial.println("SerialReady");

  Serial1.begin(4900, SERIAL_8N2);
  UCSR1B &= ~(1 << TXEN1);


  pinMode(bt_ri, INPUT_PULLUP); //BT right
  pinMode(bt_ct, INPUT_PULLUP); //BT center
  pinMode(bt_le, INPUT_PULLUP); //BT left
  pinMode(ctrl_pin, OUTPUT); //VTX SW
  pinMode(vbat_pin, INPUT); //VSENS A0
  pinMode(ir_pin, INPUT); //IR INPUT (D6)
  pinMode(pwm_contr1, OUTPUT); //CONTROL A 4066
  pinMode(pwm_contr2, OUTPUT); //CONTROL B 4066
  //pinMode(BZ_pin, OUTPUT); //VTX SW


  pinMode(17, OUTPUT); //BULDIN LED RX
  pinMode(30, OUTPUT); //BULDIN LED TX
  digitalWrite(17, LOW);

  // turn ON SIG A (FC PAN) and SIG B (SW PWM) OFF for PAN PWM on 4066
  digitalWrite(pwm_contr1, HIGH);
  digitalWrite(pwm_contr2, LOW);

  myservo.attach(pwm_pin); //SW SERVO OUTPUT

  relaypower = EEPROM.read(peraypwrADDR);

  if (relaypower > 1)
  {
    relaypower = 1;
  }

  clearOLED();
}



byte buttoncheck()
{
  int i_butt = 0;
  byte buttonz = 0;
  if (digitalRead(bt_ri) != 1)
  {
    while (digitalRead(bt_ri) != 1)
    {
      delay(2);
      i_butt++;
    }
    buttonz = 1;
  }

  else if (digitalRead(bt_ct) != 1)
  {
    while (digitalRead(bt_ct) != 1)
    {
      delay(2);
      i_butt++;
    }
    buttonz = 2;
  }
  else if (digitalRead(bt_le) != 1)
  {
    while (digitalRead(bt_le) != 1)
    {
      delay(2);
      i_butt++;
    }
    buttonz = 3;
  }

  pressedbut = buttonz;
  return buttonz;
}



void clearOLED()
{
  u8g.firstPage();
  do
  {
  }
  while ( u8g.nextPage() );
}





//MAIN LOOP

void loop(void) {
  u8g.firstPage();
  do {

    buttoncheck();
    SAcontrol();
    ReadVoltage();
    relay_ctrl();
    parking_ctrl();

    //u8g.drawFrame (0, 0, 128, 32);




    u8g.setFont(u8g_font_helvB12r);

    if (celldetect == 0) {
      u8g.setPrintPos(65, 16);
      u8g.setFont(u8g_font_6x10r);
      u8g.print("READING");
      u8g.setPrintPos(65, 27);
      u8g.print("VOLTAGE");
    }

    else {
      u8g.setPrintPos(73, 17);
      u8g.print(voltage);
      u8g.print("V");

      u8g.setPrintPos(75, 28);
      u8g.setFont(u8g_font_6x10r);
      u8g.print(celldetect);
      u8g.print("S LIPO");

      u8g.drawFrame (58, 8, 10, 19);
      u8g.drawBox (61, 6, 4, 2);

      if (cellvoltage >= 3.97) {
        u8g.drawBox (60, 10, 6, 3);
        u8g.drawBox (60, 14, 6, 3);
        u8g.drawBox (60, 18, 6, 3);
        u8g.drawBox (60, 22, 6, 3);
      }
      else if ((cellvoltage < 3.97) && (cellvoltage >= 3.75)) {
        u8g.drawBox (60, 14, 6, 3);
        u8g.drawBox (60, 18, 6, 3);
        u8g.drawBox (60, 22, 6, 3);
      }
      else if ((cellvoltage < 3.75) && (cellvoltage >= 3.52)) {
        u8g.drawBox (60, 18, 6, 3);
        u8g.drawBox (60, 22, 6, 3);
      }
      else if ((cellvoltage < 3.52) && (cellvoltage >= 3.33)) {
        u8g.drawBox (60, 22, 6, 3);
      }
      else if (cellvoltage < 3.33) {


        unsigned long buzzertime = millis();
        if (buzzertime - buzzermillis > 1000) { // one second loop
          buzzermillis = buzzertime;
          if (alarmstate == 0) {
            alarmstate = 1;
            tone(BZ_pin, 200, 200); //beep for 200ms
          }
          else {
            alarmstate = 0;
            noTone(BZ_pin); //no beep

          }
        }
      }


      //ALARM INDICATOR FUNCTION

      if (alarmstate == 0) {
        digitalWrite(30, HIGH);
      }
      else if (alarmstate == 1) {
        digitalWrite(30, LOW);
        u8g.drawBox (60, 10, 6, 15);
      }


    }

    if (relaypower == 1) {

      u8g.drawBitmapP(3, 0, 3, 30, bitmap_relayON);

      if (SA_available == 0) {
        u8g.setPrintPos(28, 15);
        u8g.setFont(u8g_font_6x10r);
        u8g.print("NO");
        u8g.setPrintPos(28, 26);
        u8g.print("SA");
      }
      else {

        u8g.setPrintPos(26, 17);
        u8g.setFont(u8g_font_helvB12r);
        if (act_band == 0) {
          u8g.print("A");
        }
        else if (act_band == 1) {
          u8g.print("B");
        }
        else if (act_band == 2) {
          u8g.print("E");
        }
        else if (act_band == 3) {
          u8g.print("F");
        }
        else if (act_band == 4) {
          u8g.print("R");
        }

        u8g.setPrintPos(39, 17);
        u8g.print(act_ch);
        u8g.setPrintPos(26, 28);
        u8g.setFont(u8g_font_6x10r);
        u8g.print(act_freq);
      }
    }

    else {
      u8g.drawBitmapP(3, 2, 3, 30, bitmap_relayOFF);
      u8g.setPrintPos(25, 15);
      u8g.setFont(u8g_font_6x10r);
      u8g.print("VTX");
      u8g.setPrintPos(25, 26);
      u8g.print("OFF");
    }

    if (pressedbut == 2) {
      menu();
    }


  } while ( u8g.nextPage() );
}

//LOOP END





void menu() {
  menuactive = 1;
  byte exit = 0;
  clearOLED();
  SAcontrol();

  while (exit == 0) {

    u8g.firstPage();
    do {
      buttoncheck();
      u8g.drawFrame (0, 0, 128, 32);

      if (menuactive == 1) {
        u8g.setFont(u8g_font_6x10r);
        u8g.setPrintPos(10, 19);
        u8g.print("AV-RELAY POWER >");

        if (pressedbut == 1) {
          menuactive = 2;
        }
        if (pressedbut == 2) {
          pwr_screen();
        }
        if (pressedbut == 3) {
          menuactive = 4;
        }
      }


      else if (menuactive == 2) {
        u8g.setFont(u8g_font_6x10r);
        u8g.setPrintPos(10, 19);
        u8g.print("AV-RELAY CHANNEL >");

        if (pressedbut == 1) {
          menuactive = 3;
        }
        if (pressedbut == 3) {
          menuactive = 1;
        }
        if (pressedbut == 2) {
          vtx_screen();
        }
      }


      else if (menuactive == 3) {
        u8g.setFont(u8g_font_6x10);
        u8g.setPrintPos(10, 19);
        u8g.print("TRACKER PARKING >");

        if (pressedbut == 1) {
          menuactive = 4;
        }
        if (pressedbut == 3) {
          menuactive = 2;
        }
        if (pressedbut == 2) {
          parking_screen();
        }
      }


      else if (menuactive == 4) {
        u8g.setFont(u8g_font_6x10);
        u8g.setPrintPos(10, 19);
        u8g.print("MANUAL PAN >");

        if (pressedbut == 1) {
          menuactive = 5;
        }
        if (pressedbut == 3) {
          menuactive = 3;
        }
        if (pressedbut == 2) {
          menuactive = 0;
          manctrl_screen();

        }
      }

      else if (menuactive == 5) {
        u8g.setFont(u8g_font_6x10);
        u8g.setPrintPos(10, 19);
        u8g.print("EXIT");

        if (pressedbut == 1) {
          menuactive = 1;
        }
        if (pressedbut == 3) {
          menuactive = 4;
        }
        if (pressedbut == 2) {
          return;
        }
      }

      if (menuactive == 0) {
        return;
      }


    } while ( u8g.nextPage() );


  }
}


void pwr_screen(void) {
  byte exit = 0;
  menuactive = 0;
  cur_pos = 0;
  clearOLED();

  while (exit == 0) {

    u8g.firstPage();
    do {

      buttoncheck();
      relay_ctrl();

      u8g.setPrintPos(8, 20);
      u8g.setFont(u8g_font_helvB12r);

      if (relaypower == 1) {

        u8g.print("VTX ON");
      }
      else {
        u8g.print("VTX OFF");
      }

      if (cur_pos == 0) {
        u8g.setFont(u8g_font_6x10);
        u8g.drawFrame (88, 6, 33, 19);
        u8g.setPrintPos(93, 19);
        u8g.print("EXIT");
        u8g.drawBox (8, 24, 70, 2);
      }


      else if (cur_pos == 1) {
        u8g.setFont(u8g_font_6x10);
        u8g.drawBox (88, 6, 33, 19);
        u8g.setColorIndex(0);
        u8g.setPrintPos(93, 19);
        u8g.print("EXIT");
        u8g.setColorIndex(1);
      }

      if (pressedbut == 1) {
        if (cur_pos < 1 ) {
          cur_pos = cur_pos + 1;
        }
        else {
          cur_pos = 0;
        }
      }

      if (pressedbut == 3) {
        if (cur_pos > 0 ) {
          cur_pos = cur_pos - 1;
        }
        else {
          cur_pos = 1;
        }
      }

      if (pressedbut == 2) {
        if (cur_pos == 1) {
          sa_update = 0;
          EEPROM.write(peraypwrADDR, relaypower);
          return;

        }
        else if (cur_pos == 0) {
          if (relaypower == 1) {
            relaypower = 0;
          }
          else {
            relaypower = 1;
          }
        }
      }


    } while ( u8g.nextPage() );

  }
}



void parking_screen(void) {
  byte exit = 0;
  menuactive = 0;
  cur_pos = 0;
  clearOLED();
  parking_step = 0;

  while (exit == 0) {
    u8g.firstPage();
    do {

      buttoncheck();
      parking_ctrl();


      if (parking_step == 0) {
        u8g.setPrintPos(5, 13);
        u8g.print("START");
        u8g.setPrintPos(5, 24);
        u8g.print("PARKING?");


        if (cur_pos == 0) {
          u8g.drawBox (63, 6, 25, 19);
          u8g.setColorIndex(0);
          u8g.setPrintPos(70, 19);
          u8g.print("NO");
          u8g.setColorIndex(1);
          u8g.setFont(u8g_font_6x10);
          u8g.drawFrame (88, 6, 33, 19);
          u8g.setPrintPos(96, 19);
          u8g.print("YES");

          if (pressedbut == 2) {
            return;
          }
        }


        else if (cur_pos == 1) {
          u8g.drawFrame (63, 6, 25, 19);
          u8g.setPrintPos(70, 19);
          u8g.print("NO");
          u8g.setFont(u8g_font_6x10);
          u8g.drawBox (88, 6, 33, 19);
          u8g.setColorIndex(0);
          u8g.setPrintPos(96, 19);
          u8g.print("YES");
          u8g.setColorIndex(1);

          if (pressedbut == 2) {
            parking_step = 1;
          }
        }

        if (pressedbut == 1) {
          if (cur_pos < 1 ) {
            cur_pos = cur_pos + 1;
          }
          else {
            cur_pos = 0;
          }
        }

        if (pressedbut == 3) {
          if (cur_pos > 0 ) {
            cur_pos = cur_pos - 1;
          }
          else {
            cur_pos = 1;
          }
        }
      }
      else if (parking_step == 1) {

        u8g.setPrintPos(7, 18);
        u8g.print("PARKING IN PROGRESS");
        //u8g.setPrintPos(10, 50);
        //u8g.setFont(u8g_font_6x10r);
        //u8g.print("IR_VAL=");
        //u8g.print(ir_value);
      }
      else if (parking_step == 2) {

        u8g.setPrintPos(15, 13);
        u8g.print("PARKING COMPLETE");
        u8g.setPrintPos(6, 26);
        u8g.print("PRESS CENTER TO EXIT");


        if (pressedbut == 2) {
          parking_step = 0;
          return;
        }
      }

    } while ( u8g.nextPage() );

  }
}

void manctrl_screen(void) {
  byte exit = 0;
  clearOLED();
  while (exit == 0) {

    u8g.firstPage();
    do {
      //buttoncheck();

      // turn OFF SIG_A (FC PAN) and SIG_B (SW PWM) ONF for PAN PWM on 4066
      digitalWrite(pwm_contr2, LOW);
      digitalWrite(pwm_contr1, HIGH);
      digitalWrite(17, LOW); // just for testing

      u8g.setFont(u8g_font_6x10);
      u8g.setPrintPos(8, 20);
      u8g.print("PAN CTRL");
      u8g.drawBox (88, 6, 33, 19);
      u8g.setColorIndex(0);
      u8g.setPrintPos(93, 19);
      u8g.print("EXIT");
      u8g.setColorIndex(1);


      if (digitalRead(bt_le) != 1) {
        //move left
        myservo.writeMicroseconds(1600);
      }

      else if (digitalRead(bt_ri) != 1) {
        //move right
        myservo.writeMicroseconds(1400);
      }
      else {
        myservo.writeMicroseconds(1500);
      }

      if (digitalRead(bt_ct) != 1) {
        digitalWrite(pwm_contr2, HIGH);
        digitalWrite(pwm_contr1, LOW);
        digitalWrite(17, HIGH); // just for testing

        menuactive = 5;

        return;
      }




    } while ( u8g.nextPage() );
  }



}

void vtx_screen(void) {
  byte exit = 0;
  sa_update = 0;
  menuactive = 2;
  cur_pos = 0;
  clearOLED();
  while (exit == 0) {

    u8g.firstPage();
    do {
      buttoncheck();
      SAcontrol();

      u8g.setFont(u8g_font_6x10);

      if (relaypower == 1) {

        if (SA_available == 0) {
          u8g.setPrintPos(8, 20);
          u8g.print("NO VTX FOUND");
          u8g.drawBox (88, 6, 33, 19);
          u8g.setColorIndex(0);
          u8g.setPrintPos(93, 19);
          u8g.print("EXIT");
          u8g.setColorIndex(1);
        }

        else {
          u8g.setScale2x2();
          u8g.setPrintPos(4, 11);

          if (act_band == 0) {
            u8g.print("A");
          }
          else if (act_band == 1) {
            u8g.print("B");
          }
          else if (act_band == 2) {
            u8g.print("E");
          }
          else if (act_band == 3) {
            u8g.print("F");
          }
          else if (act_band == 4) {
            u8g.print("R");
          }

          u8g.setPrintPos(12, 11);
          u8g.print(act_ch);
          u8g.setFont(u8g_font_6x10);
          u8g.undoScale();

          u8g.setPrintPos(45, 19);
          u8g.print(act_freq);
          u8g.print("MHz");

          u8g.drawFrame (95, 6, 27, 19);
          u8g.setPrintPos(100, 19);
          u8g.print("SET");

          if (cur_pos == 0) {
            u8g.drawBox (7, 24, 12, 2);
          }

          else if (cur_pos == 1) {
            u8g.drawBox (23, 24, 12, 2);
          }

          else if (cur_pos == 2) {
            u8g.drawBox (95, 6, 27, 19);
            u8g.setColorIndex(0);
            u8g.setPrintPos(100, 19);
            u8g.print("SET");
            u8g.setColorIndex(1);
          }
        }
      }

      else {
        u8g.setPrintPos(10, 19);
        u8g.print("TURN VTX ON");
        u8g.drawBox (88, 6, 33, 19);
        u8g.setColorIndex(0);
        u8g.setPrintPos(93, 19);
        u8g.print("EXIT");
        u8g.setColorIndex(1);
        cur_pos = 2;
      }

      u8g.drawFrame (0, 0, 128, 32);

      if (cur_pos == 2) {
        if (pressedbut == 2) {
          menuactive = 0;
          return;
        }
      }

      if (SA_available == 0) {
        if (pressedbut == 2) {
          menuactive = 0;
          return;
        }
      }

      if (pressedbut == 1) {
        if (cur_pos < 2 ) {
          cur_pos = cur_pos + 1;
        }
        else {
          cur_pos = 0;
        }
      }

      if (pressedbut == 3) {
        if (cur_pos > 0 ) {
          cur_pos = cur_pos - 1;
        }
        else {
          cur_pos = 2;
        }
      }


    } while ( u8g.nextPage() );

  }

}



void ReadVoltage(void) {

  vsens = analogRead(vbat_pin);

  unsigned long updatetime = millis();

  if (updatetime - previousMillis > 1500) { // update voltage reading after one second
    previousMillis = updatetime;

    sa_update = 0; //also update smart audio status
    voltage = vsens * (2.56 / 1023.0) * ((R1 + R2) / R2); // Convert the analog reading (which goes from 0 - 1023) to a voltage, considering the voltage divider:

    if (celldetect == 0) {
      //detect cell count
      if (voltage > (Voltagedetect * 5.0)) {
        celldetect = 5;
      }
      else if (voltage > (Voltagedetect * 4.0)) {
        celldetect = 4;
      }
      else if (voltage > (Voltagedetect * 3.0)) {
        celldetect = 3;
      }
      else if (voltage > (Voltagedetect * 2.0)) {
        celldetect = 2;
      }
      else {
        celldetect = 1;
      }
    }

    cellvoltage = voltage / celldetect;

  }
}



void SAcontrol(void) {

  if (menuactive == 2) {
    if (pressedbut == 2) {
      if (cur_pos == 0) {
        if (act_band < 4) {
          act_band = act_band + 1;
        }
        else {
          act_band = 0;
        }
      }

      else if (cur_pos == 1) {
        if (act_ch < 8) {
          act_ch = act_ch + 1;
        }
        else {
          act_ch = 1;
        }
      }

      else if (cur_pos == 2) {
        Serial1.begin(4900, SERIAL_8N2);
        if (act_band == 0) {
          sa_tx_packet(SA_SET_CHANNEL, unify.channel = (act_ch - 1));
          //sa_tx_packet(SA_SET_POWER, unify.powerLevel = 2);
        }
        else if (act_band == 1) {
          sa_tx_packet(SA_SET_CHANNEL, unify.channel = (act_ch - 1) + 8);
          //sa_tx_packet(SA_SET_POWER, unify.powerLevel = 1);
        }
        else if (act_band == 2) {
          sa_tx_packet(SA_SET_CHANNEL, unify.channel = (act_ch - 1) + 16);
          //sa_tx_packet(SA_SET_POWER,unify.powerLevel = 0);
        }
        else if (act_band == 3) {
          sa_tx_packet(SA_SET_CHANNEL, unify.channel = (act_ch - 1) + 24);
          //sa_tx_packet(SA_SET_POWER, unify.powerLevel = 0);
        }
        else if (act_band == 4) {
          sa_tx_packet(SA_SET_CHANNEL, unify.channel = (act_ch - 1) + 32);
          //sa_tx_packet(SA_SET_POWER, unify.powerLevel = 0);
        }
        Serial1.end();//clear buffer, otherwise sa_tx_packet is received
        Serial1.begin(4900, SERIAL_8N2);
        UCSR1B &= ~(1 << TXEN1); //deactivate tx --> rx mode listening for response
      }

      if (SA_available == 0) {
        sa_update = 0;
      }
    }
  }

  delay(10);

  while (Serial1.available()) {
    buff[rx_len] = Serial1.read();

    if (buff[rx_len] == 0) {
      zeroes++;
    }
    rx_len++;
  }

  if (rx_len > 6) {
    //because rx is low in idle 0 is received
    //when calculating crc of 0 we have a crc match, so
    //when all bytes are 0 we should avoid parsing the input data
    if (rx_len == zeroes) {

      while (Serial1.available()) {
        Serial1.read();
      }
    } else {
      SA_available = 1;
      sa_rx_packet(buff, rx_len);
      Serial.print("Version:");
      Serial.print(unify.vtx_version);
      Serial.print(", Channel:");
      Serial.print(unify.channel);
      Serial.print(", PowerLevel:");
      Serial.print(unify.powerLevel);
      Serial.print(", Mode:");
      Serial.print(unify.mode);
      //Serial.print(", Frequency:");
      //Serial.println(unify.frequency);
    }
    zeroes = 0;
    rx_len = 0;
  }

  if (sa_update == 0) {
    Serial1.begin(4900, SERIAL_8N2);
    sa_tx_packet(SA_GET_SETTINGS, 0);
    Serial1.end();//clear buffer, otherwise sa_tx_packet is received
    Serial1.begin(4900, SERIAL_8N2);

    UCSR1B &= ~(1 << TXEN1); //deactivate tx --> rx mode listening for response
    delay(100);

    while (Serial1.available()) {
      buff[rx_len] = Serial1.read();
      if (buff[rx_len] == 0) {
        zeroes++;
      }
      rx_len++;
    }

    if (rx_len > 6) {
      //because rx is low in idle 0 is received
      //when calculating crc of 0 we have a crc match, so
      //when all bytes are 0 we should avoid parsing the input data
      if (rx_len == zeroes) {

        while (Serial1.available()) {
          Serial1.read();
        }
      } else {
        SA_available = 1;
        sa_rx_packet(buff, rx_len);
        Serial.print("Version:");
        Serial.print(unify.vtx_version);
        Serial.print(", Channel:");
        Serial.print(unify.channel);
        Serial.print(", PowerLevel:");
        Serial.print(unify.powerLevel);
        Serial.print(", Mode:");
        Serial.print(unify.mode);
        Serial.print(", Frequency:");
        Serial.println(unify.frequency);
        Serial.println(sa_update);
      }
      zeroes = 0;
      rx_len = 0;

    }
    if ((unify.channel >= 0) && (unify.channel <= 7)) {
      act_ch = unify.channel + 1;
      act_band = 0;
    }
    else if  ((unify.channel >= 8) && (unify.channel <= 15)) {
      act_ch = unify.channel - 7;
      act_band = 1;
    }
    else if  ((unify.channel >= 16) && (unify.channel <= 23)) {
      act_ch = unify.channel - 15;
      act_band = 2;
    }
    else if  ((unify.channel >= 24) && (unify.channel <= 31)) {
      act_ch = unify.channel - 23;
      act_band = 3;
    }
    else if  ((unify.channel >= 32) && (unify.channel <= 39)) {
      act_ch = unify.channel - 31;
      act_band = 4;
    }

    sa_update = 1;
  }

  //QREQUENCY SETUP
  //A BAND
  if (act_band == 0) {
    if (act_ch == 1) {
      act_freq = 5865;
    }
    else if (act_ch == 2) {
      act_freq = 5845;
    }
    else if (act_ch == 3) {
      act_freq = 5825;
    }
    else if (act_ch == 4) {
      act_freq = 5805;
    }
    else if (act_ch == 5) {
      act_freq = 5785;
    }
    else if (act_ch == 6) {
      act_freq = 5765;
    }
    else if (act_ch == 7) {
      act_freq = 5745;
    }
    else if (act_ch == 8) {
      act_freq = 5725;
    }
  }
  //B BAND
  else if (act_band == 1) {
    if (act_ch == 1) {
      act_freq = 5733;
    }
    else if (act_ch == 2) {
      act_freq = 5752;
    }
    else if (act_ch == 3) {
      act_freq = 5771;
    }
    else if (act_ch == 4) {
      act_freq = 5790;
    }
    else if (act_ch == 5) {
      act_freq = 5809;
    }
    else if (act_ch == 6) {
      act_freq = 5828;
    }
    else if (act_ch == 7) {
      act_freq = 5847;
    }
    else if (act_ch == 8) {
      act_freq = 5866;
    }
  }

  //E BAND
  else if (act_band == 2) {
    if (act_ch == 1) {
      act_freq = 5705;
    }
    else if (act_ch == 2) {
      act_freq = 5685;
    }
    else if (act_ch == 3) {
      act_freq = 5665;
    }
    else if (act_ch == 4) {
      act_freq = 5645;
    }
    else if (act_ch == 5) {
      act_freq = 5885;
    }
    else if (act_ch == 6) {
      act_freq = 5905;
    }
    else if (act_ch == 7) {
      act_freq = 5925;
    }
    else if (act_ch == 8) {
      act_freq = 5945;
    }
  }
  //F BAND
  else if (act_band == 3) {
    if (act_ch == 1) {
      act_freq = 5740;
    }
    else if (act_ch == 2) {
      act_freq = 5760;
    }
    else if (act_ch == 3) {
      act_freq = 5780;
    }
    else if (act_ch == 4) {
      act_freq = 5800;
    }
    else if (act_ch == 5) {
      act_freq = 5820;
    }
    else if (act_ch == 6) {
      act_freq = 5840;
    }
    else if (act_ch == 7) {
      act_freq = 5860;
    }
    else if (act_ch == 8) {
      act_freq = 5880;
    }
  }
  //R BAND

  else if (act_band == 4) {
    if (act_ch == 1) {

      act_freq = 5658;
    }
    else if (act_ch == 2) {
      act_freq = 5695;
    }
    else if (act_ch == 3) {
      act_freq = 5732;
    }
    else if (act_ch == 4) {
      act_freq = 5769;
    }
    else if (act_ch == 5) {
      act_freq = 5806;
    }
    else if (act_ch == 6) {
      act_freq = 5843;
    }
    else if (act_ch == 7) {
      act_freq = 5880;
    }
    else if (act_ch == 8) {
      act_freq = 5917;
    }
  }
  else {
    act_freq = 0;
  }
}

void relay_ctrl() {
  if (relaypower == 0) {
    digitalWrite (ctrl_pin, LOW); //turn OFF VTX
  }
  else {
    digitalWrite (ctrl_pin, HIGH); //turn ON VTX
  }
}

void parking_ctrl(void) {

  if (parking_step == 0) {
    // turn ON SIG_A (FC PAN) and SIG_B (SW PWM) OFF for PAN PWM on 4066
    digitalWrite(17, HIGH); // just for testing
    digitalWrite(pwm_contr2, HIGH);
    digitalWrite(pwm_contr1, LOW);
    myservo.writeMicroseconds(1500);
  }

  else if (parking_step == 1) {
    // turn OFF SIG_A (FC PAN) and SIG_B (SW PWM) ONF for PAN PWM on 4066
    digitalWrite(pwm_contr2, LOW);
    digitalWrite(pwm_contr1, HIGH);
    digitalWrite(17, LOW); // just for testing

    ir_value = analogRead(ir_pin); //D6
    //delay(10);

    if (ir_value > 41) {
      myservo.writeMicroseconds(1600);
    }
    else {
      myservo.writeMicroseconds(1500);
      parking_step = 2; //halt the rotation of the 360 servo
    }

  }



}
