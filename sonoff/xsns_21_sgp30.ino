/*
  xsns_21_sgp30.ino - SGP30 gas and air quality sensor support for Sonoff-Tasmota

  Copyright (C) 2019  Theo Arends

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_I2C
#ifdef USE_SGP30
/*********************************************************************************************\
 * SGP30 - Gas (TVOC - Total Volatile Organic Compounds) and Air Quality (CO2)
 *
 * Source: Gerhard Mutz and Adafruit Industries
 *
 * I2C Address: 0x58
\*********************************************************************************************/

#define XSNS_21             21

#include "Adafruit_SGP30.h"
Adafruit_SGP30 sgp;

uint8_t sgp30_type = 0;
uint8_t sgp30_ready = 0;
float sgp30_abshum;

/********************************************************************************************/

void sgp30_Init(void) {
  if (sgp.begin()) {
    sgp30_type = 1;
//      snprintf_P(log_data, sizeof(log_data), PSTR("SGP: Serialnumber 0x%04X-0x%04X-0x%04X"), sgp.serialnumber[0], sgp.serialnumber[1], sgp.serialnumber[2]);
//      AddLog(LOG_LEVEL_DEBUG);
    snprintf_P(log_data, sizeof(log_data), S_LOG_I2C_FOUND_AT, "SGP30", 0x58);
    AddLog(LOG_LEVEL_DEBUG);
  }
}

//#define POW_FUNC pow
#define POW_FUNC FastPrecisePow

float sgp30_AbsoluteHumidity(float temperature, float humidity,char tempUnit) {
  //taken from https://carnotcycle.wordpress.com/2012/08/04/how-to-convert-relative-humidity-to-absolute-humidity/
  //precision is about 0.1°C in range -30 to 35°C
  //August-Roche-Magnus 	6.1094 exp(17.625 x T)/(T + 243.04)
  //Buck (1981) 		6.1121 exp(17.502 x T)/(T + 240.97)
  //reference https://www.eas.ualberta.ca/jdwilson/EAS372_13/Vomel_CIRES_satvpformulae.html
  float temp = NAN;
  const float mw = 18.01534; 	// molar mass of water g/mol
  const float r = 8.31447215; 	// Universal gas constant J/mol/K

  if (isnan(temperature) || isnan(humidity) ) {
    return NAN;
  }

  if (tempUnit != 'C') {
        temperature = (temperature - 32.0) * (5.0 / 9.0); /*conversion to [°C]*/
  }

  temp = POW_FUNC(2.718281828, (17.67 * temperature) / (temperature + 243.5));



  //return (6.112 * temp * humidity * 2.1674) / (273.15 + temperature); 	//simplified version
  return (6.112 * temp * humidity * mw) / ((273.15 + temperature) * r); 	//long version
}

#define SAVE_PERIOD 30

void Sgp30Update(void)  // Perform every second to ensure proper operation of the baseline compensation algorithm
{
  sgp30_ready = 0;
  if (!sgp.IAQmeasure() || !sgp30_type) {
    // retry to init every 100 seconds
    if (21 == (uptime %100)) {
      sgp30_Init();
    }
    return;  // Measurement failed
  }
  if (global_update && global_humidity>0 && global_temperature!=9999) {
    // abs hum in mg/m3
    sgp30_abshum=sgp30_AbsoluteHumidity(global_temperature,global_humidity,TempUnit());
    sgp.setHumidity(sgp30_abshum*1000);
  }
  sgp30_ready = 1;

  // these should normally be stored permanently and used for fast restart
  if (!(uptime%SAVE_PERIOD)) {
    // store settings every N seconds
    uint16_t TVOC_base;
    uint16_t eCO2_base;

    if (!sgp.getIAQBaseline(&eCO2_base, &TVOC_base)) return;  // Failed to get baseline readings
//      snprintf_P(log_data, sizeof(log_data), PSTR("SGP: Baseline values eCO2 0x%04X, TVOC 0x%04X"), eCO2_base, TVOC_base);
//      AddLog(LOG_LEVEL_DEBUG);

  }
}


#ifdef USE_WEBSERVER
const char HTTP_SNS_SGP30[] PROGMEM =
  "{s}SGP30 " D_ECO2 "{m}%d " D_UNIT_PARTS_PER_MILLION "{e}"                // {s} = <tr><th>, {m} = </th><td>, {e} = </td></tr>
  "{s}SGP30 " D_TVOC "{m}%d " D_UNIT_PARTS_PER_BILLION "{e}";
const char HTTP_SNS_AHUM[] PROGMEM = "{s}SGP30 " "Abs Humidity" "{m}%s g/m3{e}";
#endif

#define D_JSON_AHUM "aHumidity"

void Sgp30Show(bool json)
{
  if (sgp30_ready) {
    char abs_hum[33];

    if (global_update && global_humidity>0 && global_temperature!=9999) {
      // has humidity + temperature
      dtostrfd(sgp30_abshum,4,abs_hum);
    }

    if (json) {
      ResponseAppend_P(PSTR(",\"SGP30\":{\"" D_JSON_ECO2 "\":%d,\"" D_JSON_TVOC "\":%d"), sgp.eCO2, sgp.TVOC);
      if (global_update) {
        ResponseAppend_P(PSTR(",\"" D_JSON_AHUM "\":%s"),abs_hum);
      }
      ResponseAppend_P(PSTR("}"));
#ifdef USE_DOMOTICZ
      if (0 == tele_period) DomoticzSensor(DZ_AIRQUALITY, sgp.eCO2);
#endif  // USE_DOMOTICZ
#ifdef USE_WEBSERVER
    } else {
      WSContentSend_PD(HTTP_SNS_SGP30, sgp.eCO2, sgp.TVOC);
      if (global_update) {
        WSContentSend_PD(HTTP_SNS_AHUM, abs_hum);
      }
#endif
    }
  }
}


/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xsns21(uint8_t function)
{
  bool result = false;

  if (i2c_flg) {
    switch (function) {
      case FUNC_INIT:
        sgp30_Init();
        break;
      case FUNC_EVERY_SECOND:
        Sgp30Update();
        break;
      case FUNC_JSON_APPEND:
        Sgp30Show(1);
        break;
#ifdef USE_WEBSERVER
      case FUNC_WEB_SENSOR:
        Sgp30Show(0);
        break;
#endif  // USE_WEBSERVER
    }
  }
  return result;
}

#endif  // USE_SGP30
#endif  // USE_I2C
